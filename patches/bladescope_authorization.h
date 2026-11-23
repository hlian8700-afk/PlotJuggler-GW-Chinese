#pragma once

#ifdef _WIN32
#if __has_include("bladescope_authorization_config.h")
#include "bladescope_authorization_config.h"
#else
#define BLADESCOPE_AUTH_URL ""
#define BLADESCOPE_AUTH_RSA_N ""
#define BLADESCOPE_AUTH_RSA_E ""
#endif
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSysInfo>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <winhttp.h>
#include <memory>
#include <utility>

namespace BladeAuthorization
{
inline constexpr bool configured = sizeof(BLADESCOPE_AUTH_URL) > 1;

class Controller final : public QObject
{
public:
  explicit Controller(QWidget* owner) : QObject(owner), owner(owner)
  {
    const auto machine = QSysInfo::machineUniqueId();
    if (!machine.isEmpty())
      device = QCryptographicHash::hash("BladeScope/waveform/" + machine,
                                       QCryptographicHash::Sha256).toHex();
    QTimer::singleShot(0, this, [this] { refresh(); });
  }

  bool enter()
  {
    if (claims(token()).isEmpty()) manage();
    if (claims(token()).isEmpty()) return false;
    refresh();
    return true;
  }

  void manage()
  {
    if (device.isEmpty())
    {
      QMessageBox::warning(owner, QStringLiteral("BladeScope 授权"), QStringLiteral("无法识别此 Windows 设备。"));
      return;
    }

    QDialog dialog(owner);
    dialog.setWindowTitle(QStringLiteral("BladeScope 授权管理"));
    auto* form = new QFormLayout(&dialog);
    auto* email = new QLineEdit(&dialog);
    const auto current = claims(token());
    email->setText(current.value("email").toString());
    email->setMaxLength(254);
    auto* code = new QLineEdit(&dialog);
    code->setMaxLength(6);
    code->setEchoMode(QLineEdit::Password);
    auto* status = new QLabel(&dialog);
    status->setWordWrap(true);
    const auto expiry = current.value("exp").toVariant().toLongLong();
    status->setText(expiry ? QStringLiteral("离线授权有效至 %1").arg(
        QDateTime::fromSecsSinceEpoch(expiry).toLocalTime().toString("yyyy-MM-dd HH:mm"))
        : QStringLiteral("请输入管理员已授权的个人邮箱。"));
    form->addRow(QStringLiteral("个人邮箱"), email);
    form->addRow(QStringLiteral("验证码"), code);
    form->addRow(status);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto* request = buttons->addButton(QStringLiteral("获取验证码"), QDialogButtonBox::ActionRole);
    auto* verify = buttons->addButton(QStringLiteral("验证 / 续期"), QDialogButtonBox::ActionRole);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    const auto send = [=, &dialog](bool verifying) {
      const auto address = email->text().trimmed().toLower();
      if (address.isEmpty() || (verifying && code->text().size() != 6))
      {
        status->setText(QStringLiteral("请填写个人邮箱和六位验证码。"));
        return;
      }
      request->setEnabled(false);
      verify->setEnabled(false);
      status->setText(QStringLiteral("正在联系授权服务…"));
      post(verifying ? "/v1/verify" : "/v1/code",
           {{"email", address}, {"code", verifying ? code->text() : QString()}},
           &dialog, [=, &dialog](int http, const QJsonObject& response) {
        request->setEnabled(true);
        verify->setEnabled(true);
        if (http == 202 && !verifying)
          status->setText(QStringLiteral("若邮箱已获授权，请查收验证码。"));
        else if (http == 200 && verifying && acceptToken(response.value("token").toString()))
          dialog.accept();
        else if (http == 401)
          status->setText(QStringLiteral("验证码无效或已过期。"));
        else if (http == 429)
          status->setText(QStringLiteral("请求过多，请稍后重试。"));
        else
          status->setText(QStringLiteral("验证未完成，请检查网络或联系管理员。"));
      });
    };
    connect(request, &QPushButton::clicked, &dialog, [=] { send(false); });
    connect(verify, &QPushButton::clicked, &dialog, [=] { send(true); });
    dialog.exec();
  }

private:
  static QByteArray decode(const QByteArray& text)
  {
    return QByteArray::fromBase64(text, QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
  }

  static bool verifySignature(QByteArray payload, QByteArray signature)
  {
    auto n = decode(BLADESCOPE_AUTH_RSA_N);
    auto e = decode(BLADESCOPE_AUTH_RSA_E);
    if (n.size() != 384 || e.isEmpty() || e.size() > 8 || signature.size() != n.size()) return false;
    const BCRYPT_RSAKEY_BLOB header{BCRYPT_RSAPUBLIC_MAGIC, ULONG(n.size() * 8),
                                   ULONG(e.size()), ULONG(n.size()), 0, 0};
    QByteArray blob(reinterpret_cast<const char*>(&header), sizeof(header));
    blob += e;
    blob += n;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_RSA_ALGORITHM, nullptr, 0) != 0) return false;
    BCRYPT_KEY_HANDLE key = nullptr;
    const bool imported = BCryptImportKeyPair(algorithm, nullptr, BCRYPT_RSAPUBLIC_BLOB, &key,
        reinterpret_cast<PUCHAR>(blob.data()), ULONG(blob.size()), 0) == 0;
    bool valid = false;
    if (imported)
    {
      auto hash = QCryptographicHash::hash(payload, QCryptographicHash::Sha256);
      BCRYPT_PKCS1_PADDING_INFO padding{BCRYPT_SHA256_ALGORITHM};
      valid = BCryptVerifySignature(key, &padding, reinterpret_cast<PUCHAR>(hash.data()),
          ULONG(hash.size()), reinterpret_cast<PUCHAR>(signature.data()),
          ULONG(signature.size()), BCRYPT_PAD_PKCS1) == 0;
      BCryptDestroyKey(key);
    }
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return valid;
  }

  QJsonObject claims(const QString& value) const
  {
    if (device.isEmpty() || value.size() > 3000) return {};
    const auto parts = value.toLatin1().split('.');
    if (parts.size() != 2) return {};
    const auto payload = decode(parts.at(0));
    if (payload.isEmpty() || !verifySignature(payload, decode(parts.at(1)))) return {};
    const auto data = QJsonDocument::fromJson(payload).object();
    const auto issued = data.value("iat").toVariant().toLongLong();
    const auto expiry = data.value("exp").toVariant().toLongLong();
    const auto now = QDateTime::currentSecsSinceEpoch();
    if (data.value("scope").toString() != "bladescope.waveform" ||
        data.value("device").toString().toLatin1() != device ||
        issued <= 0 || issued > now + 120 || now >= expiry || expiry - issued != 15 * 86400)
      return {};
    return data;
  }

  static QString token()
  {
    auto encrypted = QSettings().value("BladeScopeAuthorization/tokenV1").toByteArray();
    if (encrypted.isEmpty() || encrypted.size() > 8192) return {};
    DATA_BLOB input{DWORD(encrypted.size()), reinterpret_cast<BYTE*>(encrypted.data())};
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output))
      return {};
    const auto value = QString::fromLatin1(reinterpret_cast<const char*>(output.pbData), int(output.cbData));
    LocalFree(output.pbData);
    return value;
  }

  bool acceptToken(const QString& value)
  {
    if (claims(value).isEmpty()) return false;
    auto bytes = value.toLatin1();
    DATA_BLOB input{DWORD(bytes.size()), reinterpret_cast<BYTE*>(bytes.data())};
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"BladeScope", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output))
      return false;
    const QByteArray encrypted(reinterpret_cast<const char*>(output.pbData), int(output.cbData));
    LocalFree(output.pbData);
    QSettings settings;
    settings.setValue("BladeScopeAuthorization/tokenV1", encrypted);
    settings.sync();
    return settings.status() == QSettings::NoError;
  }

  struct InternetHandle
  {
    HINTERNET value;
    ~InternetHandle() { if (value) WinHttpCloseHandle(value); }
  };

  struct Response
  {
    int status = 0;
    QJsonObject object;
  };

  static Response request(const QString& path, QJsonObject data)
  {
    const QUrl origin{QString::fromLatin1(BLADESCOPE_AUTH_URL)};
    if (origin.scheme() != "https" || origin.host().isEmpty()) return {};
    const auto host = origin.host().toStdWString();
    const auto target = path.toStdWString();
    const InternetHandle session{WinHttpOpen(L"BladeScope", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.value || !WinHttpSetTimeouts(session.value, 5000, 5000, 5000, 10000)) return {};
    const InternetHandle connection{WinHttpConnect(session.value, host.c_str(),
        INTERNET_PORT(origin.port(INTERNET_DEFAULT_HTTPS_PORT)), 0)};
    if (!connection.value) return {};
    const InternetHandle call{WinHttpOpenRequest(connection.value, L"POST", target.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
    DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if (!call.value || !WinHttpSetOption(call.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects)))
      return {};
    auto json = QJsonDocument(data).toJson(QJsonDocument::Compact);
    if (!WinHttpSendRequest(call.value, L"Content-Type: application/json\r\n", DWORD(-1),
            json.data(), DWORD(json.size()), DWORD(json.size()), 0) ||
        !WinHttpReceiveResponse(call.value, nullptr))
      return {};
    DWORD status = 0, size = sizeof(status);
    if (!WinHttpQueryHeaders(call.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX))
      return {};
    QByteArray response;
    char chunk[2048];
    DWORD read = 0;
    do
    {
      if (!WinHttpReadData(call.value, chunk, sizeof(chunk), &read) || response.size() + int(read) > 8192)
        return {};
      response.append(chunk, int(read));
    } while (read);
    return {int(status), QJsonDocument::fromJson(response).object()};
  }

  template <typename Handler>
  void post(const char* path, QJsonObject data, QObject* context, Handler handler)
  {
    data.insert("device", QString::fromLatin1(device));
    auto response = std::make_shared<Response>();
    auto* worker = QThread::create([target = QString::fromLatin1(path), data = std::move(data), response] {
      *response = request(target, data);
    });
    connect(worker, &QThread::finished, context, [response, handler] {
      handler(response->status, response->object);
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
  }

  void refresh()
  {
    const auto original = token();
    if (refreshing || claims(original).isEmpty()) return;
    refreshing = true;
    post("/v1/refresh", {{"token", original}}, this, [this, original](int http, const QJsonObject& response) {
      refreshing = false;
      if (token() != original) return;
      if (http == 200) acceptToken(response.value("token").toString());
      else if (http == 403 && response.value("error").toString() == "revoked")
        QSettings().remove("BladeScopeAuthorization/tokenV1");
    });
  }

  QWidget* owner;
  QByteArray device;
  bool refreshing = false;
};
}
#endif

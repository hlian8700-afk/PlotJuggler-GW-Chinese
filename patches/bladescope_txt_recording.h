#pragma once
#include "bladescope_sqlite_recording.h"
#include <QSet>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>

namespace BladeTxtRecording
{
struct ReadResult : BladeSqliteRecording::ReadResult
{
  qsizetype duplicateTime = 0;
};

constexpr qint64 maxLine = 4 * 1024 * 1024;

inline QStringList readHeader(QFile& file)
{
  // The controller metadata precedes the comma-separated measurement header.
  for (int lineNumber = 0; lineNumber < 256 && !file.atEnd(); ++lineNumber) {
    auto bytes = file.readLine(maxLine);
    if (bytes.size() == maxLine - 1 && !bytes.endsWith('\n')) break;
    if (lineNumber == 0 && bytes.startsWith("\xef\xbb\xbf")) bytes.remove(0, 3);
    auto fields = BladeRecordingIndex::cells(QString::fromUtf8(bytes), ',');
    const auto found = std::find_if(fields.cbegin(), fields.cend(), [](const QString& field) {
      return field.compare("TimeStamp", Qt::CaseInsensitive) == 0;
    });
    if (fields.size() < 2) continue;
    if (found != fields.cend()) return fields;
    // Generic TXT tables may use another name for their timestamp column.
    const auto position = file.pos();
    const auto nextLine = file.readLine(maxLine);
    if (!file.seek(position)) return {};
    const auto row = BladeRecordingIndex::cells(QString::fromUtf8(nextLine.trimmed()), ',');
    if (!fields.front().contains(':') && row.size() == fields.size() &&
        std::any_of(row.cbegin(), row.cend(), [](const QString& cell) {
          bool numeric = false;
          cell.toDouble(&numeric);
          return numeric;
        }))
      return fields;
  }
  return {};
}

inline ReadResult read(const QString& path, int timeColumn, std::unordered_set<std::string> names,
                       const std::atomic_bool& canceled)
{
  ReadResult result;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    result.error = file.errorString();
    return result;
  }
  const auto header = readHeader(file);
  if (timeColumn < 0 || timeColumn >= header.size()) {
    result.error = QStringLiteral("\u627e\u4e0d\u5230\u6240\u9009\u65f6\u95f4\u5217\u3002");
    return result;
  }

  struct Signal {
    int column;
    QString name;
    std::vector<std::pair<double,double>> samples;
  };
  std::vector<Signal> signalsToRead;
  for (int i = 0; i < header.size(); ++i)
    if (i != timeColumn && !header[i].isEmpty())
      signalsToRead.push_back({i, header[i], {}});
  QSet<double> seenTimes;
  while (!file.atEnd() && !canceled) {
    const auto bytes = file.readLine(maxLine);
    if (bytes.size() == maxLine - 1 && !bytes.endsWith('\n')) {
      result.error = QStringLiteral("录波数据行过长，无法完整读取。");
      return result;
    }
    if (bytes.trimmed().isEmpty()) continue;
    const auto row = BladeRecordingIndex::cells(QString::fromUtf8(bytes), ',');
    auto date = QDateTime::fromString(row.value(timeColumn), "yyyy-MM-dd-HH:mm:ss.zzz");
    if (!date.isValid()) date = QDateTime::fromString(row.value(timeColumn), "yyyy-MM-dd-HH:mm:ss");
    if (!date.isValid()) date = QDateTime::fromString(row.value(timeColumn), Qt::ISODateWithMs);
    if (!date.isValid()) date = QDateTime::fromString(row.value(timeColumn), Qt::ISODate);
    bool numeric = false;
    const double timestamp = date.isValid() ? date.toMSecsSinceEpoch() / 1000.0
                                            : row.value(timeColumn).toDouble(&numeric);
    if ((!date.isValid() && !numeric) || !std::isfinite(timestamp)) {
      ++result.invalidTime;
      continue;
    }
    if (seenTimes.contains(timestamp)) ++result.duplicateTime;
    else seenTimes.insert(timestamp);
    ++result.rows;
    for (auto& signal : signalsToRead) {
      const auto text = row.value(signal.column);
      bool ok = false;
      double value = text.toDouble(&ok);
      if (!ok && text.compare("true", Qt::CaseInsensitive) == 0) { value = 1; ok = true; }
      if (!ok && text.compare("false", Qt::CaseInsensitive) == 0) { value = 0; ok = true; }
      if (ok && std::isfinite(value)) signal.samples.emplace_back(timestamp,value);
      else ++result.invalidValues;
    }
  }
  if (file.error() != QFileDevice::NoError) {
    result.error = file.errorString();
    return result;
  }
  const auto byTime = [](const auto& a,const auto& b){return a.first < b.first;};
  for (auto& signal : signalsToRead) {
    if (canceled) return {};
    if (signal.samples.empty()) continue;
    if (!std::is_sorted(signal.samples.cbegin(),signal.samples.cend(),byTime))
      std::stable_sort(signal.samples.begin(),signal.samples.end(),byTime);
    const auto base = signal.name.toStdString();
    auto name = base;
    for (int suffix=2;!names.insert(name).second;++suffix) name=base+" ["+std::to_string(suffix)+"]";
    auto& series = result.data.addNumeric(name)->second;
    for (const auto& point : signal.samples) {
      if (canceled) return {};
      series.pushBack({point.first,point.second});
    }
    std::vector<std::pair<double,double>>().swap(signal.samples);
  }
  return result;
}

inline ReadResult open(QWidget* parent, const QString& path, const std::unordered_set<std::string>& names)
{
  QFile preview(path);
  const auto header = preview.open(QIODevice::ReadOnly | QIODevice::Text)
                          ? readHeader(preview) : QStringList();
  if (header.isEmpty()) {
    QMessageBox::warning(parent, QStringLiteral("TXT"),
                         QStringLiteral("\u65e0\u6cd5\u8bfb\u53d6 TXT \u5f55\u6ce2\u8868\u5934\u3002"));
    return {};
  }
  const auto firstRow = BladeRecordingIndex::cells(QString::fromUtf8(preview.readLine(maxLine)), ',');
  preview.close();
  QDialog dialog(parent);
  dialog.setWindowTitle(QStringLiteral("TXT \u5f55\u6ce2\u5bfc\u5165"));
  QFormLayout form(&dialog);
  QComboBox columns;
  columns.setMinimumWidth(320);
  columns.setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  columns.setPlaceholderText(QStringLiteral("\u8bf7\u9009\u62e9\u65f6\u95f4\u5217"));
  for (int i = 0; i < header.size(); ++i)
    if (!header[i].isEmpty())
      columns.addItem(QStringLiteral("%1  (%2)").arg(header[i], firstRow.value(i)), i);
  columns.setCurrentIndex(-1);
  form.addRow(QStringLiteral("\u6587\u4ef6"), new QLabel(QFileInfo(path).fileName()));
  form.addRow(QStringLiteral("\u65f6\u95f4\u5217"), &columns);
  QDialogButtonBox actions(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  actions.button(QDialogButtonBox::Ok)->setText(QStringLiteral("\u5bfc\u5165"));
  actions.button(QDialogButtonBox::Cancel)->setText(QStringLiteral("\u53d6\u6d88"));
  actions.button(QDialogButtonBox::Ok)->setEnabled(false);
  QObject::connect(&columns, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog,
                   [&](int index) { actions.button(QDialogButtonBox::Ok)->setEnabled(index >= 0); });
  QObject::connect(&actions, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(&actions, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  form.addRow(&actions);
  if (dialog.exec() != QDialog::Accepted || columns.currentIndex() < 0) return {};
  const int timeColumn = columns.currentData().toInt();

  std::atomic_bool canceled{false};
  ReadResult result;
  QProgressDialog progress(QStringLiteral("正在读取 TXT 录波…"),QStringLiteral("取消"),0,0,parent);
  progress.setWindowModality(Qt::WindowModal);
  auto worker = std::unique_ptr<QThread>(QThread::create([&] {
    try { result=read(path,timeColumn,names,canceled); }
    catch(const std::exception& ex) { result.error=QString::fromUtf8(ex.what()); }
  }));
  QEventLoop loop;
  QObject::connect(worker.get(),&QThread::finished,&loop,&QEventLoop::quit);
  QObject::connect(&progress,&QProgressDialog::canceled,&loop,[&]{canceled=true;});
  progress.show();
  worker->start();
  loop.exec();
  worker->wait();
  progress.hide();
  if (canceled) return {};
  if (!result.error.isEmpty() || result.data.numeric.empty()) {
    QMessageBox::warning(parent,QStringLiteral("TXT 录波导入"),
                         result.error.isEmpty()?QStringLiteral("未找到有效的数值测点。"):result.error);
    return {};
  }
  return result;
}

inline bool isRecordingExtension(const QString& path)
{
  return QFileInfo(path).suffix().compare("txt",Qt::CaseInsensitive)==0;
}
}

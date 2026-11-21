#pragma once
#include <QtWidgets>
#include <QDirIterator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextCodec>
#include <QThread>
#include <algorithm>
#include <atomic>
#include <memory>
#include <cmath>
#include <functional>
#include <limits>

namespace BladeRecordingIndex
{
inline QStringList cells(const QString& row, QChar delimiter)
{
  QStringList values;
  QString field;
  bool quoted = false;
  for (int i = 0; i < row.size(); ++i)
  {
    const QChar ch = row.at(i);
    if (ch == '"')
    {
      if (quoted && i + 1 < row.size() && row.at(i + 1) == '"')
      {
        field += ch;
        ++i;
      }
      else
        quoted = !quoted;
    }
    else if (ch == delimiter && !quoted)
    {
      values << field.trimmed();
      field.clear();
    }
    else
      field += ch;
  }
  values << field.trimmed();
  return values;
}

inline double timestamp(const QString& value)
{
  bool ok = false;
  const auto n = value.toDouble(&ok);
  if (ok)
    return std::isfinite(n) ? n : std::numeric_limits<double>::quiet_NaN();
  auto date = QDateTime::fromString(value, Qt::ISODateWithMs);
  if (!date.isValid())
    date = QDateTime::fromString(value, Qt::ISODate);
  if (!date.isValid())
    date = QDateTime::fromString(value, "yyyy-MM-dd HH:mm:ss.zzz");
  if (!date.isValid())
    date = QDateTime::fromString(value, "yyyy-MM-dd HH:mm:ss");
  return date.isValid() ? date.toMSecsSinceEpoch() / 1000.0
                        : std::numeric_limits<double>::quiet_NaN();
}

inline QJsonObject inspect(const QFileInfo& info, const std::atomic_bool& stop)
{
  QJsonObject record{{"path", info.absoluteFilePath()}, {"size", QString::number(info.size())},
                     {"mtime", QString::number(info.lastModified().toMSecsSinceEpoch())}};
  QFile file(info.absoluteFilePath());
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
  {
    record.insert("unreadable", true);
    return record;
  }
  QByteArray bytes;
  while (!file.atEnd() && !stop && bytes.trimmed().isEmpty())
    bytes = file.readLine(1024 * 1024);
  if (bytes.startsWith("\xEF\xBB\xBF"))
    bytes.remove(0, 3);
  QTextCodec::ConverterState state;
  auto* codec = QTextCodec::codecForName("UTF-8");
  auto header = codec->toUnicode(bytes.constData(), bytes.size(), &state);
  if (state.invalidChars)
  {
    if (auto* gb = QTextCodec::codecForName("GB18030"))
    {
      codec = gb;
      header = codec->toUnicode(bytes);
    }
  }
  QChar delimiter = ',';
  QStringList fields = cells(header, delimiter);
  for (QChar candidate : {QChar(';'), QChar('\t')})
  {
    auto split = cells(header, candidate);
    if (split.size() > fields.size())
    {
      delimiter = candidate;
      fields = split;
    }
  }
  record.insert("columns", QJsonArray::fromStringList(fields));
  record.insert("encoding", QString::fromLatin1(codec->name()));
  const QStringList knownTimes = {"time", "timestamp", "datetime", "date_time", "time(s)",
      "date", QString::fromUtf8("时间"), QString::fromUtf8("时间戳"), QString::fromUtf8("采样时间")};
  int timeColumn = -1;
  for (int i = 0; i < fields.size(); ++i)
    if (knownTimes.contains(fields.at(i).simplified(), Qt::CaseInsensitive))
    {
      timeColumn = i;
      break;
    }
  if (timeColumn < 0)
    return record;

  record.insert("timeColumn", fields.at(timeColumn));
  double low = std::numeric_limits<double>::infinity(), high = -low;
  double previous = low, interval = 0;
  bool irregular = false;
  qint64 valid = 0, invalid = 0;
  while (!file.atEnd() && !stop)
  {
    const auto data = cells(codec->toUnicode(file.readLine(1024 * 1024)), delimiter);
    const double t = timestamp(data.value(timeColumn));
    if (!std::isfinite(t))
    {
      ++invalid;
      continue;
    }
    low = std::min(low, t);
    high = std::max(high, t);
    if (valid)
    {
      const double d = t - previous;
      if (valid == 1)
        interval = d;
      irregular |= d <= 0 || std::abs(d - interval) > std::max(1e-6, std::abs(interval) * 1e-5);
    }
    previous = t;
    ++valid;
  }
  if (valid)
  {
    record.insert("start", low);
    record.insert("end", high);
    record.insert("valid", QString::number(valid));
    record.insert("invalid", QString::number(invalid));
    record.insert("irregular", irregular);
    if (valid > 1 && !irregular)
      record.insert("interval", interval);
  }
  return record;
}

inline QString cachePath()
{
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/csv-recording-index.json";
}

inline void open(QWidget* parent, const std::function<void(QStringList)>& loadFiles)
{
  QDialog dialog(parent);
  dialog.setWindowTitle(QString::fromUtf8("CSV 录波目录索引"));
  dialog.resize(1050, 610);
  auto* layout = new QVBoxLayout(&dialog);
  auto* directory = new QLineEdit;
  directory->setReadOnly(true);
  auto* browse = new QPushButton(QString::fromUtf8("选择目录"));
  auto* scanButton = new QPushButton(QString::fromUtf8("扫描／更新索引"));
  auto* rootRow = new QHBoxLayout;
  rootRow->addWidget(directory, 1);
  rootRow->addWidget(browse);
  rootRow->addWidget(scanButton);
  layout->addLayout(rootRow);
  auto* search = new QLineEdit;
  auto* first = new QLineEdit;
  auto* last = new QLineEdit;
  search->setPlaceholderText(QString::fromUtf8("搜索文件名、目录或测点标签"));
  first->setPlaceholderText(QString::fromUtf8("起始时间（原始值或 ISO）"));
  last->setPlaceholderText(QString::fromUtf8("结束时间（原始值或 ISO）"));
  auto* filters = new QHBoxLayout;
  filters->addWidget(search, 2);
  filters->addWidget(first, 1);
  filters->addWidget(last, 1);
  layout->addLayout(filters);
  auto* table = new QTableWidget(0, 6);
  table->setHorizontalHeaderLabels({QString::fromUtf8("文件"), QString::fromUtf8("起始"),
      QString::fromUtf8("结束"), QString::fromUtf8("采样间隔"), QString::fromUtf8("列数"),
      QString::fromUtf8("状态")});
  table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  layout->addWidget(table, 1);
  auto* status = new QLabel;
  layout->addWidget(status);
  auto* actions = new QHBoxLayout;
  actions->addStretch();
  auto* openButton = new QPushButton(QString::fromUtf8("打开选中文件"));
  auto* close = new QPushButton(QString::fromUtf8("关闭"));
  actions->addWidget(openButton);
  actions->addWidget(close);
  layout->addLayout(actions);

  QFile cache(cachePath());
  QJsonObject saved;
  if (cache.open(QIODevice::ReadOnly))
  {
    saved = QJsonDocument::fromJson(cache.readAll()).object();
    cache.close();
  }
  if (saved.value("version").toInt() != 1)
    saved = {};
  directory->setText(saved.value("root").toString());
  QJsonArray entries = saved.value("files").toArray();
  const QVariantMap aliases = qApp->property("bladeScopeTagAliases").toMap();
  const auto n = [](QJsonValue v) {
    return v.isDouble() ? QString::number(v.toDouble(), 'g', 16) : QStringLiteral("—");
  };
  const auto populate = [&] {
    table->setRowCount(0);
    const double from = timestamp(first->text()), to = timestamp(last->text());
    if ((!first->text().trimmed().isEmpty() && !std::isfinite(from)) ||
        (!last->text().trimmed().isEmpty() && !std::isfinite(to)) ||
        (std::isfinite(from) && std::isfinite(to) && from > to))
    {
      status->setText(QString::fromUtf8("请输入有效时间，起始时间不能晚于结束时间。"));
      openButton->setEnabled(false);
      return;
    }
    const auto needle = search->text().trimmed();
    for (const auto value : entries)
    {
      const auto e = value.toObject();
      const auto path = e.value("path").toString();
      if ((std::isfinite(from) && (!e.contains("end") || e.value("end").toDouble() < from)) ||
          (std::isfinite(to) && (!e.contains("start") || e.value("start").toDouble() > to)))
        continue;
      const auto columns = e.value("columns").toArray();
      bool matched = path.contains(needle, Qt::CaseInsensitive);
      for (const auto column : columns)
      {
        if (matched)
          break;
        const auto raw = column.toString();
        matched = raw.contains(needle, Qt::CaseInsensitive) ||
                  aliases.value(raw).toString().contains(needle, Qt::CaseInsensitive);
      }
      if (!matched)
        continue;
      const int row = table->rowCount();
      table->insertRow(row);
      auto* name = new QTableWidgetItem(QDir(directory->text()).relativeFilePath(path));
      name->setData(Qt::UserRole, path);
      name->setToolTip(path);
      table->setItem(row, 0, name);
      const QString state = e.value("unreadable").toBool() ? QString::fromUtf8("无法读取")
          : !e.contains("start") ? QString::fromUtf8("时间未识别")
          : e.value("irregular").toBool() ? QString::fromUtf8("间隔不均或时间重复")
          : e.value("invalid").toString() != "0" ? QString::fromUtf8("含无效时间")
          : QString::fromUtf8("已索引");
      const QStringList values{n(e.value("start")), n(e.value("end")), n(e.value("interval")),
                               QString::number(columns.size()), state};
      for (int col = 0; col < values.size(); ++col)
        table->setItem(row, col + 1, new QTableWidgetItem(values.at(col)));
    }
    status->setText(QString::fromUtf8("显示 %1 / %2 个 CSV。时间列无法识别的文件仍可打开。")
                    .arg(table->rowCount()).arg(entries.size()));
    openButton->setEnabled(table->rowCount() > 0);
  };
  for (auto* field : { search, first, last })
    QObject::connect(field, &QLineEdit::textChanged, &dialog, populate);
  const auto scan = [&] {
    const QString root = directory->text();
    if (root.isEmpty())
      return;
    QProgressDialog progress(QString::fromUtf8("正在更新目录索引…"), QString::fromUtf8("取消"), 0, 0, &dialog);
    progress.setWindowModality(Qt::WindowModal);
    QHash<QString, QJsonObject> previous;
    if (saved.value("root").toString() == root)
      for (const auto item : saved.value("files").toArray())
      {
        const auto entry = item.toObject();
        previous.insert(entry.value("path").toString(), entry);
      }
    std::atomic_bool canceled{false};
    QJsonArray found;
    std::unique_ptr<QThread> worker(QThread::create([&] {
      QDirIterator iter(root, QDir::Files, QDirIterator::Subdirectories);
      while (iter.hasNext() && !canceled)
      {
        iter.next();
        const auto info = iter.fileInfo();
        if (info.suffix().compare("csv", Qt::CaseInsensitive) != 0)
          continue;
        const auto cached = previous.value(info.absoluteFilePath());
        if (cached.value("size").toString() == QString::number(info.size()) &&
            cached.value("mtime").toString() == QString::number(info.lastModified().toMSecsSinceEpoch()))
          found.append(cached);
        else
          found.append(inspect(info, canceled));
      }
    }));
    QEventLoop loop;
    QObject::connect(worker.get(), &QThread::finished, &loop, &QEventLoop::quit);
    QObject::connect(&progress, &QProgressDialog::canceled, &dialog, [&] { canceled = true; });
    progress.show();
    worker->start();
    loop.exec();
    worker->wait();
    if (canceled)
    {
      directory->setText(saved.value("root").toString());
      return;
    }
    entries = found;
    saved = QJsonObject{{"version", 1}, {"root", root}, {"files", entries}};
    QDir().mkpath(QFileInfo(cachePath()).absolutePath());
    QSaveFile output(cachePath());
    const auto bytes = QJsonDocument(saved).toJson(QJsonDocument::Compact);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit())
      QMessageBox::warning(&dialog, dialog.windowTitle(), QString::fromUtf8("索引已生成，但无法保存到本机。"));
    populate();
  };
  QObject::connect(browse, &QPushButton::clicked, &dialog, [&] {
    const auto root = QFileDialog::getExistingDirectory(&dialog, QString::fromUtf8("选择录波目录"), directory->text());
    if (!root.isEmpty())
    {
      directory->setText(root);
      scan();
    }
  });
  QObject::connect(scanButton, &QPushButton::clicked, &dialog, scan);
  QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
  QStringList selected;
  QObject::connect(openButton, &QPushButton::clicked, &dialog, [&] {
    for (const auto& row : table->selectionModel()->selectedRows())
    {
      const auto path = table->item(row.row(), 0)->data(Qt::UserRole).toString();
      if (QFileInfo::exists(path))
        selected << path;
    }
    if (selected.isEmpty())
      QMessageBox::information(&dialog, dialog.windowTitle(), QString::fromUtf8("请选择存在的录波文件。"));
    else
      dialog.accept();
  });
  populate();
  if (dialog.exec() == QDialog::Accepted)
    loadFiles(selected);
}
}

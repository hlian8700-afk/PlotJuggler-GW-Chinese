#pragma once
#include <QtWidgets>
#include <PlotJuggler/plotdata.h>
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
#include <vector>
#include <utility>

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

struct CsvHeader
{
  QStringList fields;
  QTextCodec* codec;
  QChar delimiter;
};

inline CsvHeader readHeader(QFile& file, const std::atomic_bool& stop)
{
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
  return {fields, codec, delimiter};
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
  const auto header = readHeader(file, stop);
  const auto& fields = header.fields;
  auto* codec = header.codec;
  const auto delimiter = header.delimiter;
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

struct SelectedSeries
{
  QString name;
  std::vector<std::pair<double, double>> points;
};

inline PJ::PlotDataMapRef readSelection(QWidget* parent, const QJsonObject& entry,
                                        const QVariantMap& aliases, double from, double to,
                                        std::unordered_set<std::string> names)
{
  const QString path = entry.value("path").toString();
  const QFileInfo info(path);
  QFile file(path);
  if (!info.exists() || entry.value("size").toString() != QString::number(info.size()) ||
      entry.value("mtime").toString() != QString::number(info.lastModified().toMSecsSinceEpoch()) ||
      !file.open(QIODevice::ReadOnly | QIODevice::Text))
  {
    QMessageBox::warning(parent, parent->windowTitle(), QString::fromUtf8("文件已变更或无法读取，请更新索引。"));
    return {};
  }

  std::atomic_bool canceled{false};
  const auto header = readHeader(file, canceled);
  const auto timeColumn = header.fields.indexOf(entry.value("timeColumn").toString());
  if (!entry.contains("start") || timeColumn < 0)
  {
    QMessageBox::information(parent, parent->windowTitle(), QString::fromUtf8("此文件没有可识别的时间列。"));
    return {};
  }

  QDialog picker(parent);
  picker.setWindowTitle(QString::fromUtf8("选择要加载的测点"));
  picker.resize(620, 520);
  auto* layout = new QVBoxLayout(&picker);
  auto* search = new QLineEdit;
  search->setPlaceholderText(QString::fromUtf8("搜索中文标签或测点编码"));
  layout->addWidget(search);
  auto* list = new QListWidget;
  layout->addWidget(list);
  for (int i = 0; i < header.fields.size(); ++i)
  {
    const auto name = header.fields.at(i);
    if (i == timeColumn || name.isEmpty())
      continue;
    const auto alias = aliases.value(name).toString();
    auto* item = new QListWidgetItem(alias.isEmpty() ? name : QString("%1 (%2)").arg(alias, name), list);
    item->setData(Qt::UserRole, i);
    item->setCheckState(Qt::Unchecked);
  }
  auto* buttons = new QDialogButtonBox;
  auto* accept = buttons->addButton(QString::fromUtf8("加载"), QDialogButtonBox::AcceptRole);
  auto* cancel = buttons->addButton(QString::fromUtf8("取消"), QDialogButtonBox::RejectRole);
  accept->setEnabled(false);
  layout->addWidget(buttons);
  QObject::connect(search, &QLineEdit::textChanged, &picker, [list](const QString& value) {
    for (int i = 0; i < list->count(); ++i)
      list->item(i)->setHidden(!list->item(i)->text().contains(value.trimmed(), Qt::CaseInsensitive));
  });
  QObject::connect(list, &QListWidget::itemChanged, &picker, [list, accept] {
    for (int i = 0; i < list->count(); ++i)
      if (list->item(i)->checkState() == Qt::Checked)
      {
        accept->setEnabled(true);
        return;
      }
    accept->setEnabled(false);
  });
  QObject::connect(accept, &QPushButton::clicked, &picker, &QDialog::accept);
  QObject::connect(cancel, &QPushButton::clicked, &picker, &QDialog::reject);
  file.close();
  if (picker.exec() != QDialog::Accepted)
    return {};
  std::vector<int> columns;
  std::vector<SelectedSeries> result;
  for (int i = 0; i < list->count(); ++i)
    if (list->item(i)->checkState() == Qt::Checked)
    {
      const auto column = list->item(i)->data(Qt::UserRole).toInt();
      columns.push_back(column);
      result.push_back({header.fields.at(column), {}});
    }

  QProgressDialog progress(QString::fromUtf8("正在读取选中的测点…"), QString::fromUtf8("取消"), 0, 0, parent);
  progress.setWindowTitle(parent->windowTitle());
  progress.setWindowModality(Qt::WindowModal);
  bool failed = false;
  PJ::PlotDataMapRef data;
  std::unique_ptr<QThread> worker(QThread::create([&] {
    const QFileInfo current(path);
    QFile input(path);
    if (current.size() != info.size() || current.lastModified() != info.lastModified() ||
        !input.open(QIODevice::ReadOnly | QIODevice::Text) ||
        readHeader(input, canceled).fields != header.fields)
    {
      failed = true;
      return;
    }
    while (!input.atEnd() && !canceled)
    {
      const auto row = cells(header.codec->toUnicode(input.readLine(1024 * 1024)), header.delimiter);
      const auto t = timestamp(row.value(timeColumn));
      if (!std::isfinite(t) || (std::isfinite(from) && t < from) || (std::isfinite(to) && t > to))
        continue;
      for (std::size_t i = 0; i < columns.size(); ++i)
      {
        bool ok = false;
        const auto y = row.value(columns[i]).toDouble(&ok);
        if (ok && std::isfinite(y))
          result[i].points.emplace_back(t, y);
      }
    }
    const auto byTime = [](const auto& a, const auto& b) { return a.first < b.first; };
    failed = input.error() != QFileDevice::NoError;
    if (failed)
      return;
    for (auto& series : result)
    {
      if (canceled)
        return;
      if (series.points.empty())
        continue;
      if (!std::is_sorted(series.points.begin(), series.points.end(), byTime))
        std::stable_sort(series.points.begin(), series.points.end(), byTime);
      const auto base = series.name.toStdString();
      auto name = base;
      for (int suffix = 2; !names.insert(name).second; ++suffix)
        name = base + " [" + std::to_string(suffix) + "]";
      auto& curve = data.addNumeric(name)->second;
      for (const auto& point : series.points)
      {
        if (canceled)
          return;
        curve.pushBack({point.first, point.second});
      }
      series.points = {};
    }
  }));
  QEventLoop loop;
  QObject::connect(worker.get(), &QThread::finished, &loop, &QEventLoop::quit);
  QObject::connect(&progress, &QProgressDialog::canceled, &loop, [&] { canceled = true; });
  progress.show();
  worker->start();
  loop.exec();
  worker->wait();
  progress.hide();
  if (canceled)
    return {};
  if (failed)
  {
    QMessageBox::warning(parent, parent->windowTitle(), QString::fromUtf8("读取失败，请更新索引后重试。"));
    return {};
  }
  if (data.numeric.empty())
    QMessageBox::information(parent, parent->windowTitle(), QString::fromUtf8("所选时间范围内没有可加载的数值样本。"));
  return data;
}

inline void open(QWidget* parent, const std::function<void(QStringList)>& loadFiles,
                 const std::function<void(PJ::PlotDataMapRef&)>& loadSelection,
                 const std::function<std::unordered_set<std::string>()>& existingNames)
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
  auto* partial = new QPushButton(QString::fromUtf8("按时段和测点加载…"));
  partial->setEnabled(false);
  actions->addWidget(partial);
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
  const auto selectedEntry = [&] {
    const auto rows = table->selectionModel()->selectedRows();
    return rows.size() == 1 ? table->item(rows.front().row(), 0)->data(Qt::UserRole + 1).toJsonObject()
                            : QJsonObject{};
  };
  QObject::connect(table->selectionModel(), &QItemSelectionModel::selectionChanged, &dialog, [&] {
    partial->setEnabled(selectedEntry().contains("start"));
  });
  const auto populate = [&] {
    table->setRowCount(0);
    partial->setEnabled(false);
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
      name->setData(Qt::UserRole + 1, QVariant::fromValue(e));
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
  PJ::PlotDataMapRef selectedSeries;
  QObject::connect(partial, &QPushButton::clicked, &dialog, [&] {
    selectedSeries = readSelection(&dialog, selectedEntry(), aliases,
                                   timestamp(first->text()), timestamp(last->text()), existingNames());
    if (!selectedSeries.numeric.empty())
      dialog.accept();
  });
  populate();
  if (dialog.exec() == QDialog::Accepted)
  {
    if (selectedSeries.numeric.empty())
      loadFiles(selected);
    else
      loadSelection(selectedSeries);
  }
}
}

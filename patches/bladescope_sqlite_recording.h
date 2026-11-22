#pragma once
#include "bladescope_analysis.h"
#include "bladescope_recording_index.h"
#include "bladescope_time_display.h"
#include <QCompleter>
#include <QHash>
#include <functional>
#include <optional>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace BladeSqliteRecording
{
inline QString identifier(QString name)
{
  return QStringLiteral("\"") + name.replace('"', "\"\"") + QStringLiteral("\"");
}

inline bool isRecordingExtension(const QString& path)
{
  const auto suffix = QFileInfo(path).suffix().toLower();
  return suffix == "data" || suffix == "db" || suffix == "sqlite" || suffix == "sqlite3";
}

class Connection
{
  const QString name = QUuid::createUuid().toString();
public:
  QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", name);
  explicit Connection(const QString& path)
  {
    db.setConnectOptions("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=2000");
    db.setDatabaseName(path);
    if (db.open()) {
      QSqlQuery query(db);
      query.exec("PRAGMA trusted_schema=OFF");
    }
  }
  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;
  ~Connection()
  {
    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(name);
  }
};

inline bool validHeader(const QString& path)
{
  QFile file(path);
  return file.open(QIODevice::ReadOnly) && file.read(16) == QByteArray("SQLite format 3\0",16);
}

inline QStringList tables(const QSqlDatabase& db)
{
  QSqlQuery query(db);
  QStringList out;
  if (query.exec("SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%' ORDER BY name"))
    while (query.next()) out << query.value(0).toString();
  return out;
}

inline QStringList columns(const QSqlDatabase& db, const QString& table)
{
  QSqlQuery query(db);
  QStringList out;
  if (query.exec("PRAGMA table_info(" + identifier(table) + ")"))
    while (query.next()) out << query.value(1).toString();
  return out;
}

struct Selection
{
  QString table;
  QString time;
  QStringList measurements;
  double from;
  double until;
  bool milliseconds = false;
};

inline double timeValue(const QVariant& value, bool milliseconds)
{
  const auto text = value.toString().trimmed();
  bool numeric = false;
  const double number = text.toDouble(&numeric);
  if (numeric) return milliseconds ? number / 1000.0 : number;
  return BladeRecordingIndex::timestamp(text);
}

struct ReadResult
{
  PJ::PlotDataMapRef data;
  QString error;
  qsizetype rows = 0;
  qsizetype invalidTime = 0;
  qsizetype invalidValues = 0;
};

using Sample = std::pair<double, double>;
using Samples = std::vector<std::vector<Sample>>;

inline ReadResult readSamples(const QString& path, const Selection& selection,
                              Samples& samples, const std::atomic_bool& canceled)
{
  ReadResult result;
  Connection connection(path);
  if (!connection.db.isOpen()) {
    result.error = connection.db.lastError().text();
    return result;
  }
  QStringList selected{identifier(selection.time)};
  for (const auto& field : selection.measurements) selected << identifier(field);
  // SQLite can silently treat a missing double-quoted column as a string literal.
  const auto schema = columns(connection.db, selection.table);
  if (!schema.contains(selection.time) ||
      std::any_of(selection.measurements.cbegin(), selection.measurements.cend(),
                  [&](const QString& field) { return !schema.contains(field); })) {
    result.error = QStringLiteral("所选表或测点已变更，请重新选择。");
    return result;
  }
  QSqlQuery query(connection.db);
  query.setForwardOnly(true);
  if (!query.exec("SELECT " + selected.join(", ") + " FROM " + identifier(selection.table))) {
    result.error = query.lastError().text();
    return result;
  }
  std::vector<size_t> initialSizes;
  initialSizes.reserve(samples.size());
  for (const auto& channel : samples) initialSizes.push_back(channel.size());
  while (!canceled && query.next()) {
    const double t = timeValue(query.value(0),selection.milliseconds);
    if (!std::isfinite(t)) { ++result.invalidTime; continue; }
    if (t < selection.from || t > selection.until) continue;
    ++result.rows;
    for (int i=0;i<selection.measurements.size();++i) {
      const auto value = query.value(i+1);
      if (value.type() == QVariant::ByteArray || value.isNull()) { ++result.invalidValues; continue; }
      bool ok = false;
      double y = value.toDouble(&ok);
      if (!ok && value.type() == QVariant::String) {
        const auto text = value.toString().trimmed();
        if (text.compare("true", Qt::CaseInsensitive)==0) { y=1; ok=true; }
        if (text.compare("false", Qt::CaseInsensitive)==0) { y=0; ok=true; }
      }
      if (ok && std::isfinite(y)) samples[size_t(i)].emplace_back(t,y);
      else ++result.invalidValues;
    }
  }
  if (query.lastError().isValid()) {
    result.error = query.lastError().text();
    for (size_t i = 0; i < samples.size(); ++i) samples[i].resize(initialSizes[i]);
    return result;
  }
  return result;
}

inline void buildSeries(PJ::PlotDataMapRef& data, const Selection& selection, Samples& samples,
                        std::unordered_set<std::string>& names, const std::atomic_bool& canceled)
{
  const auto byTime = [](const Sample& a,const Sample& b){return a.first < b.first;};
  for (int i=0;i<selection.measurements.size() && !canceled;++i) {
    auto& points = samples[size_t(i)];
    if (points.empty()) continue;
    if (!std::is_sorted(points.cbegin(),points.cend(),byTime))
      std::stable_sort(points.begin(),points.end(),byTime);
    const auto base = selection.measurements[i].toStdString();
    auto name = base;
    for (int suffix=2;!names.insert(name).second;++suffix) name=base+" ["+std::to_string(suffix)+"]";
    auto& series = data.addNumeric(name)->second;
    for (const auto& point : points) {
      if (canceled) break;
      series.pushBack({point.first,point.second});
    }
    std::vector<Sample>().swap(points);
  }
}

inline ReadResult read(const QString& path, const Selection& selection,
                       std::unordered_set<std::string> names, const std::atomic_bool& canceled)
{
  Samples samples(static_cast<size_t>(selection.measurements.size()));
  auto result = readSamples(path, selection, samples, canceled);
  if (result.error.isEmpty() && !canceled)
    buildSeries(result.data, selection, samples, names, canceled);
  return result;
}

struct RecordingOptions
{
  QVector<Selection> selections;
  int precision;
};

inline std::optional<RecordingOptions> selectRecording(
    QWidget* parent, const QString& title, const QString& path, const QStringList& available,
    const std::function<QStringList(const QString&)>& fieldsForTable)
{
  QDialog dialog(parent);
  dialog.setWindowTitle(title + QStringLiteral(" · ") + QFileInfo(path).fileName());
  dialog.resize(620,580);
  QVBoxLayout layout(&dialog);
  auto* form = new QFormLayout;
  layout.addLayout(form);
  auto* table = new QComboBox(&dialog);
  table->addItems(available);
  if(available.contains("oneseconddata")) table->setCurrentText("oneseconddata");
  auto* time = new QComboBox;
  time->setEditable(true);
  time->setInsertPolicy(QComboBox::NoInsert);
  time->lineEdit()->setPlaceholderText(QStringLiteral("\u8f93\u5165\u5173\u952e\u5b57\u641c\u7d22\u65f6\u95f4\u5217"));
  time->completer()->setCaseSensitivity(Qt::CaseInsensitive);
  time->completer()->setFilterMode(Qt::MatchContains);
  time->completer()->setCompletionMode(QCompleter::PopupCompletion);
  auto* precision = new QComboBox;
  precision->addItems({QStringLiteral("\u7cfb\u7edf\u9ed8\u8ba4\uff08\u81ea\u52a8\uff09"),
                       QStringLiteral("\u79d2"), QStringLiteral("\u6beb\u79d2")});
  precision->setCurrentIndex(BladeTimeDisplay::precision());
  precision->setToolTip(QStringLiteral("\u4ec5\u63a7\u5236\u523b\u5ea6\u663e\u793a\uff0c\u539f\u59cb\u91c7\u6837\u65f6\u95f4\u4fdd\u7559\u3002"));
  auto* numericUnit = new QComboBox;
  numericUnit->addItems({QStringLiteral("秒"),QStringLiteral("毫秒")});
  auto* from = new QLineEdit;
  auto* until = new QLineEdit;
  for(auto* value:{from,until}) value->setPlaceholderText(QStringLiteral("留空不限，日期时间或秒数"));
  if (!available.isEmpty())
    form->addRow(QStringLiteral("配置时间列的数据表"), table);
  else
    table->hide();
  form->addRow(QStringLiteral("时间列"),time);
  form->addRow(QStringLiteral("数字时间列单位"),numericUnit);
  form->addRow(QStringLiteral("\u5f55\u6ce2\u65f6\u95f4\u663e\u793a\u7cbe\u5ea6"), precision);
  form->addRow(QStringLiteral("\u8d77\u59cb\u65f6\u95f4"), from);
  form->addRow(QStringLiteral("结束时间"),until);
  auto* search = new QLineEdit;
  search->setPlaceholderText(QStringLiteral("搜索测点名或已有中文标签"));
  layout.addWidget(search);
  auto* groupRow = new QHBoxLayout;
  layout.addLayout(groupRow);
  auto* groupCombo = new QComboBox;
  groupCombo->setEditable(true);
  groupCombo->setInsertPolicy(QComboBox::NoInsert);
  groupCombo->lineEdit()->setPlaceholderText(QStringLiteral("选择或输入测点组名称"));
  groupRow->addWidget(groupCombo, 1);
  auto* loadGroup = new QPushButton(QStringLiteral("载入测点组"));
  auto* saveGroup = new QPushButton(QStringLiteral("保存测点组"));
  auto* manageGroups = new QPushButton(QStringLiteral("管理测点组"));
  for (auto* button : {loadGroup, saveGroup, manageGroups})
  {
    button->setAutoDefault(false);
    groupRow->addWidget(button);
  }
  QSettings settings;
  const auto key = QString::fromLatin1(BladeAnalysis::measurementGroupsKey);
  auto savedGroups = QJsonDocument::fromJson(settings.value(key).toByteArray()).object();
  const auto refreshGroups = [&](const QString& name) {
    groupCombo->clear();
    groupCombo->addItems(savedGroups.keys());
    groupCombo->setCurrentIndex(groupCombo->findText(name, Qt::MatchExactly));
  };
  refreshGroups({});
  const auto persistGroups = [&](const QJsonObject& groups) {
    settings.setValue(key, QJsonDocument(groups).toJson(QJsonDocument::Compact));
  };

  if (!available.isEmpty())
    layout.addWidget(new QLabel(QStringLiteral("可跨表勾选测点。上方数据表用于设置各表的时间列和数字时间单位。")));
  auto* measurements = new QListWidget;
  layout.addWidget(measurements,1);
  auto* actions = new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
  auto* all = actions->addButton(QStringLiteral("勾选可见测点"),QDialogButtonBox::ActionRole);
  auto* none = actions->addButton(QStringLiteral("清空勾选"),QDialogButtonBox::ActionRole);
  actions->button(QDialogButtonBox::Ok)->setText(QStringLiteral("\u5bfc\u5165"));
  actions->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("\u53d6\u6d88"));
  layout.addWidget(actions);
  const auto aliases = qApp->property("bladeScopeTagAliases").toMap();
  const auto timeField = [&] {
    const int index = time->findText(time->currentText().trimmed(), Qt::MatchFixedString);
    return index >= 0 ? time->itemText(index) : QString();
  };
  const auto tableNames = available.isEmpty() ? QStringList{QString()} : available;
  QMap<QString, QStringList> fields;
  QMap<QString, QString> timeColumns;
  QMap<QString, int> units;
  constexpr int tableRole = Qt::UserRole + 1;
  for (const auto& tableName : tableNames)
  {
    fields.insert(tableName, fieldsForTable(tableName));
    for (const auto& preferred : QStringList{"WTUR.Tm.Rw.Dt", "timestamp", "time", "datetime"})
    {
      const auto& columns = fields[tableName];
      const auto match = std::find_if(columns.cbegin(), columns.cend(), [&](const QString& field) {
        return field.compare(preferred, Qt::CaseInsensitive) == 0;
      });
      if (!available.isEmpty() && match != columns.cend())
      {
        timeColumns.insert(tableName, *match);
        break;
      }
    }
    for (const auto& tag : fields[tableName])
    {
      const auto alias = aliases.value(tag).toString();
      auto label = alias.isEmpty() ? tag : tag + QStringLiteral(" — ") + alias;
      if (tableNames.size() > 1)
        label += QStringLiteral("  [%1]").arg(tableName);
      auto* item = new QListWidgetItem(label, measurements);
      item->setData(Qt::UserRole, tag);
      item->setData(tableRole, tableName);
      item->setCheckState(Qt::Unchecked);
    }
  }
  const auto isTimeItem = [&](const QListWidgetItem* item) {
    return item->data(Qt::UserRole).toString() == timeColumns.value(item->data(tableRole).toString());
  };
  const auto filter = [&] {
    const auto term = search->text().trimmed();
    for (int i = 0; i < measurements->count(); ++i)
    {
      auto* item = measurements->item(i);
      item->setHidden(isTimeItem(item) || !item->text().contains(term, Qt::CaseInsensitive));
    }
  };
  const auto updateTimeItems = [&] {
    for (int i = 0; i < measurements->count(); ++i)
    {
      auto* item = measurements->item(i);
      if (isTimeItem(item))
        item->setCheckState(Qt::Unchecked);
    }
    filter();
  };
  QString editingTable = table->currentText();
  const auto saveTime = [&] {
    timeColumns.insert(editingTable, timeField());
    units.insert(editingTable, numericUnit->currentIndex());
    updateTimeItems();
  };
  const auto showTable = [&] {
    editingTable = table->currentText();
    const QSignalBlocker block(time);
    time->clear();
    time->addItems(fields.value(editingTable));
    time->setCurrentIndex(time->findText(timeColumns.value(editingTable), Qt::MatchExactly));
    numericUnit->setCurrentIndex(units.value(editingTable));
    updateTimeItems();
  };
  QObject::connect(table, &QComboBox::currentTextChanged, &dialog, [&] {
    saveTime();
    showTable();
  });
  QObject::connect(time, QOverload<int>::of(&QComboBox::activated), &dialog, saveTime);
  QObject::connect(time->lineEdit(), &QLineEdit::editingFinished, &dialog, saveTime);
  QObject::connect(search,&QLineEdit::textChanged,&dialog,filter);
  QObject::connect(all,&QPushButton::clicked,&dialog,[&]{
    for(int i=0;i<measurements->count();++i)
      if(!measurements->item(i)->isHidden()) measurements->item(i)->setCheckState(Qt::Checked);
  });
  QObject::connect(none,&QPushButton::clicked,&dialog,[&]{
    for(int i=0;i<measurements->count();++i) measurements->item(i)->setCheckState(Qt::Unchecked);
  });
  const auto warn = [&](const QString& message) {
    QMessageBox::warning(&dialog, dialog.windowTitle(), message);
  };
  QObject::connect(loadGroup, &QPushButton::clicked, &dialog, [&] {
    const auto name = groupCombo->currentText().trimmed();
    if (!savedGroups.contains(name))
      return warn(QStringLiteral("请选择已保存的测点组。"));

    saveTime();
    QMultiHash<QString, QListWidgetItem*> items;
    for (int i = 0; i < measurements->count(); ++i)
    {
      auto* item = measurements->item(i);
      const auto tag = item->data(Qt::UserRole).toString();
      if (!isTimeItem(item))
        items.insert(tag, item);
    }

    QStringList missing;
    QSet<QString> matched;
    for (const auto member : savedGroups.value(name).toArray())
    {
      const auto tag = member.toString();
      if (items.contains(tag))
        matched.insert(tag);
      else
        missing << tag;
    }
    if (!matched.isEmpty())
    {
      for (auto it = items.cbegin(); it != items.cend(); ++it)
        it.value()->setCheckState(matched.contains(it.key()) ? Qt::Checked : Qt::Unchecked);
    }
    if (!missing.isEmpty() || matched.isEmpty())
    {
      QMessageBox notice(&dialog);
      notice.setIcon(QMessageBox::Information);
      notice.setWindowTitle(QStringLiteral("载入测点组"));
      notice.setText(matched.isEmpty()
                         ? QStringLiteral("当前录波没有可勾选的组内测点，已保留原勾选。")
                         : QStringLiteral("已勾选 %1 个测点，另有 %2 个测点在当前录波中不可用。")
                               .arg(matched.size()).arg(missing.size()));
      if (!missing.isEmpty())
        notice.setDetailedText(missing.join(QLatin1Char('\n')));
      notice.exec();
    }
  });
  QObject::connect(saveGroup, &QPushButton::clicked, &dialog, [&] {
    const auto name = groupCombo->currentText().trimmed();
    saveTime();
    QStringList tags;
    for (int i = 0; i < measurements->count(); ++i)
    {
      const auto* item = measurements->item(i);
      const auto tag = item->data(Qt::UserRole).toString();
      if (item->checkState() == Qt::Checked && !isTimeItem(item) && !tags.contains(tag))
        tags << tag;
    }
    if (name.isEmpty() || tags.isEmpty())
      return warn(QStringLiteral("请输入组名并勾选测点。"));
    if (savedGroups.contains(name) &&
        QMessageBox::question(&dialog, dialog.windowTitle(), QStringLiteral("覆盖同名测点组？"),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
      return;
    savedGroups.insert(name, QJsonArray::fromStringList(tags));
    persistGroups(savedGroups);
    refreshGroups(name);
  });
  QObject::connect(manageGroups, &QPushButton::clicked, &dialog, [&] {
    const auto name = groupCombo->currentText().trimmed();
    saveTime();
    QStringList tags;
    for (int i = 0; i < measurements->count(); ++i)
    {
      const auto* item = measurements->item(i);
      if (!isTimeItem(item))
        tags << item->data(Qt::UserRole).toString();
    }
    tags.removeDuplicates();
    BladeAnalysis::manageGroups(&dialog, tags, savedGroups, persistGroups);
    refreshGroups(name);
  });

  QVector<Selection> selections;
  QObject::connect(actions, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  QObject::connect(actions, &QDialogButtonBox::accepted, &dialog, [&] {
    saveTime();
    const double inf = std::numeric_limits<double>::infinity();
    const double start = from->text().trimmed().isEmpty() ? -inf : BladeRecordingIndex::timestamp(from->text().trimmed());
    const double end = until->text().trimmed().isEmpty() ? inf : BladeRecordingIndex::timestamp(until->text().trimmed());
    QMap<QString, QStringList> checked;
    for (int i = 0; i < measurements->count(); ++i)
    {
      const auto* item = measurements->item(i);
      if (item->checkState() == Qt::Checked && !isTimeItem(item))
        checked[item->data(tableRole).toString()] << item->data(Qt::UserRole).toString();
    }
    if (checked.isEmpty() || std::isnan(start) || std::isnan(end) || start > end)
      return warn(QStringLiteral("请勾选测点，并检查起止时间。"));
    selections.clear();
    for (auto it = checked.cbegin(); it != checked.cend(); ++it)
    {
      const auto field = timeColumns.value(it.key());
      if (field.isEmpty())
      {
        table->setCurrentText(it.key());
        time->setFocus();
        return warn(it.key().isEmpty() ? QStringLiteral("请选择时间列。")
                                     : QStringLiteral("请为 %1 选择时间列。").arg(it.key()));
      }
      selections.push_back({it.key(), field, it.value(), start, end, units.value(it.key()) == 1});
    }
    dialog.accept();
  });
  showTable();
  time->setFocus();
  if (dialog.exec() != QDialog::Accepted) return {};
  return RecordingOptions{selections, precision->currentIndex()};

}

inline ReadResult open(QWidget* parent, const QString& path, const std::unordered_set<std::string>& names)
{
  auto warn = [&](const QString& message) {
    QMessageBox::warning(parent,QStringLiteral("SQLite 录波导入"),message);
  };
  if (!validHeader(path)) {
    warn(QStringLiteral("文件不是可读取的 SQLite 3 数据库。"));
    return {};
  }
  Connection connection(path);
  if (!connection.db.isOpen()) { warn(connection.db.lastError().text()); return {}; }
  const auto available = tables(connection.db);
  if (available.isEmpty()) { warn(QStringLiteral("数据库内没有可用数据表。")); return {}; }

  const auto options = selectRecording(parent, QStringLiteral("SQLite 录波导入"), path, available,
      [&](const QString& table) { return columns(connection.db, table); });
  if (!options) return {};
  connection.db.close();
  auto usedNames = names;

  std::atomic_bool canceled{false};
  ReadResult result;
  QProgressDialog progress(QStringLiteral("正在读取 SQLite 测点…"),QStringLiteral("取消"),0,0,parent);
  progress.setWindowModality(Qt::WindowModal);
  auto worker = std::unique_ptr<QThread>(QThread::create([&] {
    try {
      for (const auto& selection : options->selections)
      {
        if (canceled) return;
        Samples samples(static_cast<size_t>(selection.measurements.size()));
        const auto part = readSamples(path, selection, samples, canceled);
        if (canceled) return;
        if (!part.error.isEmpty())
        {
          result.error = selection.table + QStringLiteral(": ") + part.error;
          return;
        }
        result.rows += part.rows;
        result.invalidTime += part.invalidTime;
        result.invalidValues += part.invalidValues;
        buildSeries(result.data, selection, samples, usedNames, canceled);
      }
    }
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
  if(canceled) return {};
  if(!result.error.isEmpty()) { warn(result.error); return {}; }
  if(result.data.numeric.empty()) { warn(QStringLiteral("选定范围内没有有效的数值测点。")); return {}; }
  BladeTimeDisplay::setPrecision(options->precision);
  return result;
}
}

#pragma once
#include "bladescope_recording_index.h"
#include "bladescope_time_display.h"
#include <QCompleter>
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

inline ReadResult read(const QString& path, const Selection& selection,
                       std::unordered_set<std::string> names, const std::atomic_bool& canceled)
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
  using Sample = std::pair<double,double>;
  std::vector<std::vector<Sample>> samples(static_cast<size_t>(selection.measurements.size()));
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
    return result;
  }
  const auto byTime = [](const Sample& a,const Sample& b){return a.first < b.first;};
  for (int i=0;i<selection.measurements.size() && !canceled;++i) {
    auto& points = samples[size_t(i)];
    if (points.empty()) continue;
    if (!std::is_sorted(points.cbegin(),points.cend(),byTime))
      std::stable_sort(points.begin(),points.end(),byTime);
    const auto base = selection.measurements[i].toStdString();
    auto name = base;
    for (int suffix=2;!names.insert(name).second;++suffix) name=base+" ["+std::to_string(suffix)+"]";
    auto& series = result.data.addNumeric(name)->second;
    for (const auto& point : points) {
      if (canceled) break;
      series.pushBack({point.first,point.second});
    }
    std::vector<Sample>().swap(points);
  }
  return result;
}

struct RecordingOptions
{
  Selection selection;
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
    form->addRow(QStringLiteral("数据表"), table);
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
  auto* measurements = new QListWidget;
  layout.addWidget(measurements,1);
  auto* actions = new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
  auto* all = actions->addButton(QStringLiteral("勾选可见测点"),QDialogButtonBox::ActionRole);
  auto* none = actions->addButton(QStringLiteral("清空勾选"),QDialogButtonBox::ActionRole);
  actions->button(QDialogButtonBox::Ok)->setText(QStringLiteral("\u5bfc\u5165"));
  actions->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("\u53d6\u6d88"));
  layout.addWidget(actions);
  const auto aliases = qApp->property("bladeScopeTagAliases").toMap();
  auto filter=[&]{
    for(int i=0;i<measurements->count();++i)
      measurements->item(i)->setHidden(!measurements->item(i)->text().contains(search->text().trimmed(),Qt::CaseInsensitive));
  };
  auto populate=[&] {
    QSet<QString> checked;
    for(int i=0;i<measurements->count();++i) {
      auto* item=measurements->item(i);
      if(item->checkState()==Qt::Checked) checked.insert(item->data(Qt::UserRole).toString());
    }
    measurements->clear();
    for(int i=0;i<time->count();++i) {
      if(i==time->currentIndex()) continue;
      const auto tag=time->itemText(i), alias=aliases.value(tag).toString();
      auto* item=new QListWidgetItem(alias.isEmpty()?tag:tag+" — "+alias,measurements);
      item->setData(Qt::UserRole,tag);
      item->setCheckState(checked.contains(tag)?Qt::Checked:Qt::Unchecked);
    }
    filter();
  };
  auto changeTable=[&]{
    const QSignalBlocker block(time);
    time->clear();
    time->addItems(fieldsForTable(table->currentText()));
    time->setCurrentIndex(-1);
    if (!available.isEmpty()) for(const auto& preferred:QStringList{"WTUR.Tm.Rw.Dt","timestamp","time","datetime"}) {
      const int i=time->findText(preferred,Qt::MatchFixedString);
      if(i>=0) { time->setCurrentIndex(i); break; }
    }
    measurements->clear();
    populate();
  };
  QObject::connect(table,&QComboBox::currentTextChanged,&dialog,changeTable);
  QObject::connect(time,QOverload<int>::of(&QComboBox::activated),&dialog,populate);
  QObject::connect(search,&QLineEdit::textChanged,&dialog,filter);
  QObject::connect(all,&QPushButton::clicked,&dialog,[&]{
    for(int i=0;i<measurements->count();++i)
      if(!measurements->item(i)->isHidden()) measurements->item(i)->setCheckState(Qt::Checked);
  });
  QObject::connect(none,&QPushButton::clicked,&dialog,[&]{
    for(int i=0;i<measurements->count();++i) measurements->item(i)->setCheckState(Qt::Unchecked);
  });
  Selection selection;
  QObject::connect(actions,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
  QObject::connect(actions,&QDialogButtonBox::accepted,&dialog,[&]{
    const double inf=std::numeric_limits<double>::infinity();
    const int index = time->findText(time->currentText().trimmed(), Qt::MatchFixedString);
    selection={table->currentText(),index >= 0 ? time->itemText(index) : QString(),{},
               from->text().trimmed().isEmpty()?-inf:BladeRecordingIndex::timestamp(from->text().trimmed()),
               until->text().trimmed().isEmpty()?inf:BladeRecordingIndex::timestamp(until->text().trimmed()),
               numericUnit->currentIndex()==1};
    for(int i=0;i<measurements->count();++i) {
      auto* item=measurements->item(i);
      if(item->checkState()==Qt::Checked && item->data(Qt::UserRole).toString()!=selection.time)
        selection.measurements<<item->data(Qt::UserRole).toString();
    }
    if (selection.time.isEmpty() || selection.measurements.isEmpty() || std::isnan(selection.from) ||
        std::isnan(selection.until) || selection.from>selection.until) {
      QMessageBox::warning(&dialog,dialog.windowTitle(),QStringLiteral("请选中时间列和测点，并检查起止时间。"));
      return;
    }
    dialog.accept();
  });
  changeTable();
  time->setFocus();
  if(dialog.exec()!=QDialog::Accepted) return {};
  return RecordingOptions{selection, precision->currentIndex()};

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
  const auto& selection = options->selection;

  std::atomic_bool canceled{false};
  ReadResult result;
  QProgressDialog progress(QStringLiteral("正在读取 SQLite 测点…"),QStringLiteral("取消"),0,0,parent);
  progress.setWindowModality(Qt::WindowModal);
  auto worker = std::unique_ptr<QThread>(QThread::create([&] {
    try { result=read(path,selection,names,canceled); }
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

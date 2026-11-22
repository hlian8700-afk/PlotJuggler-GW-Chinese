#pragma once
#include "PlotJuggler/plotdata.h"
#include "bladescope_time_view_math.h"
#include <QtWidgets>
#include <QMap>
#include <QClipboard>
#include <vector>
#include "bladescope_fit_xlsx.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>
#include <QSignalBlocker>
#include <QJsonObject>
#include <QSaveFile>
#include <QFile>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace BladeAnalysis
{
inline constexpr auto measurementGroupsKey = "BladeScope/measurementGroups";
inline QString number(double value)
{
  return QString::number(value, 'g', 17);
}

inline QString csvCell(QString value)
{
  value.replace('"', "\"\"");
  return '"' + value + '"';
}

struct Statistics
{
  size_t count = 0;
  size_t unmatched = 0;
  size_t invalid = 0;
  double low = 0, high = 0, mean = 0, m2 = 0;

  void add(double value)
  {
    if (!count)
      low = high = value;
    else
    {
      low = std::min(low, value);
      high = std::max(high, value);
    }
    const double delta = value - mean;
    mean += delta / double(++count);
    m2 += delta * (value - mean);
  }
};

inline void appendStatistics(QStringList& rows, const QString& name, const Statistics& v,
                         std::vector<double>* values = nullptr)
{
  rows << QStringLiteral("%1：有效 %2，无法匹配 %3，无效 %4")
              .arg(name).arg(qulonglong(v.count)).arg(qulonglong(v.unmatched)).arg(qulonglong(v.invalid));
  if (!v.count) return;
  rows << QStringLiteral("最小 %1，最大 %2，极差 %3，均值 %4，总体标准差 %5")
              .arg(number(v.low), number(v.high), number(v.high - v.low), number(v.mean),
                   number(std::sqrt(std::max(0.0, v.m2 / double(v.count)))));
  if (!values || values->empty()) return;
  std::sort(values->begin(), values->end());
  const auto percentile = [&](double ratio) {
    const double position = ratio * (values->size() - 1);
    const size_t index = size_t(position);
    const auto lower = (*values)[index];
    const auto upper = (*values)[std::min(index + 1, values->size() - 1)];
    return number(lower + (upper - lower) * (position - index));
  };
  rows << QStringLiteral("P5 %1，P25 %2，中位数 %3，P75 %4，P95 %5")
      .arg(percentile(.05), percentile(.25), percentile(.5), percentile(.75), percentile(.95));
}

inline size_t boundary(const PJ::PlotData& series, double time, bool upper = false)
{
  size_t low = 0, high = series.size();
  while (low < high)
  {
    const size_t mid = low + (high - low) / 2;
    const double x = series.at(mid).x;
    if (x < time || (upper && x == time))
      low = mid + 1;
    else
      high = mid;
  }
  return low;
}

inline bool match(const PJ::PlotData& series, double time, double tolerance,
                  bool previousOnly, double& value)
{
  const size_t end = boundary(series, time, true);
  size_t index = end ? end - 1 : series.size();
  if (!previousOnly && end < series.size() &&
      (index == series.size() || series.at(end).x - time < time - series.at(index).x))
    index = end;
  if (index == series.size())
    return false;
  const auto& sample = series.at(index);
  value = sample.y;
  return std::abs(sample.x - time) <= tolerance && std::isfinite(value);
}

inline void manageGroups(QWidget* parent, const QStringList& available, QJsonObject& groups,
                         const std::function<void(const QJsonObject&)>& persist)
{
  QDialog dialog(parent);
  dialog.setWindowTitle(QString::fromUtf8("测点组管理"));
  dialog.resize(760, 480);
  auto* layout = new QVBoxLayout(&dialog);
  auto* columns = new QSplitter;
  auto* groupList = new QListWidget;
  auto* members = new QListWidget;
  groupList->setAccessibleName(QString::fromUtf8("测点组"));
  members->setAccessibleName(QString::fromUtf8("组内测点"));
  columns->addWidget(groupList);
  columns->addWidget(members);
  columns->setStretchFactor(1, 1);
  layout->addWidget(columns, 1);
  const auto current = [=] {
    const auto* item = groupList->currentItem();
    return item ? item->text() : QString();
  };
  const QVariantMap aliases = qApp->property("bladeScopeTagAliases").toMap();
  const auto showMembers = [&] {
    members->clear();
    for (const auto value : groups.value(current()).toArray())
    {
      const auto raw = value.toString();
      const auto alias = aliases.value(raw).toString();
      auto* item = new QListWidgetItem(alias.isEmpty() ? raw : alias + " (" + raw + ")", members);
      item->setData(Qt::UserRole, raw);
      if (!available.isEmpty() && !available.contains(raw))
        item->setText(item->text() + QString::fromUtf8("〔当前录波缺失〕"));
    }
  };
  const auto reload = [&](const QString& name) {
    groupList->clear();
    groupList->addItems(groups.keys());
    const auto found = groupList->findItems(name, Qt::MatchExactly);
    if (!found.isEmpty())
      groupList->setCurrentItem(found.front());
    else if (groupList->count())
      groupList->setCurrentRow(0);
    showMembers();
  };
  const auto commit = [&](const QString& name) {
    persist(groups);
    reload(name);
  };
  QObject::connect(groupList, &QListWidget::currentItemChanged, &dialog, showMembers);
  auto* groupRow = new QHBoxLayout;
  auto* memberRow = new QHBoxLayout;
  layout->addLayout(groupRow);
  layout->addLayout(memberRow);
  const auto button = [&](QHBoxLayout* row, const char* title, auto action) {
    auto* widget = new QPushButton(QString::fromUtf8(title));
    row->addWidget(widget);
    QObject::connect(widget, &QPushButton::clicked, &dialog, action);
  };
  const auto nameDialog = [&](const QString& old) {
    bool ok = false;
    const auto name = QInputDialog::getText(&dialog, dialog.windowTitle(),
        QString::fromUtf8("组名"), QLineEdit::Normal, old, &ok).trimmed();
    if (!ok || name.isEmpty() || name == old)
      return QString();
    if (groups.contains(name))
    {
      QMessageBox::warning(&dialog, dialog.windowTitle(), QString::fromUtf8("该组名已存在。"));
      return QString();
    }
    return name;
  };
  button(groupRow, "新建组", [&] {
    const auto name = nameDialog({});
    if (!name.isEmpty())
    {
      groups.insert(name, QJsonArray());
      commit(name);
    }
  });
  button(groupRow, "重命名", [&] {
    const auto old = current();
    if (old.isEmpty())
      return;
    const auto name = nameDialog(old);
    if (!name.isEmpty())
    {
      const auto points = groups.value(old);
      groups.remove(old);
      groups.insert(name, points);
      commit(name);
    }
  });
  button(groupRow, "删除组", [&] {
    const auto name = current();
    if (!name.isEmpty() &&
        QMessageBox::question(&dialog, dialog.windowTitle(),
          QString::fromUtf8("删除测点组「%1」？").arg(name)) == QMessageBox::Yes)
    {
      groups.remove(name);
      commit({});
    }
  });
  button(memberRow, "添加测点", [&] {
    const auto name = current();
    if (name.isEmpty())
      return;
    bool ok = false;
    const auto raw = QInputDialog::getItem(&dialog, dialog.windowTitle(),
        QString::fromUtf8("选择或输入原始标签"), available, available.isEmpty() ? -1 : 0,
        true, &ok).trimmed();
    auto list = groups.value(name).toArray();
    if (ok && !raw.isEmpty() && !list.contains(QJsonValue(raw)))
    {
      list.append(raw);
      groups.insert(name, list);
      commit(name);
    }
  });
  button(memberRow, "移除选中测点", [&] {
    const int row = members->currentRow();
    const auto name = current();
    if (row < 0 || name.isEmpty())
      return;
    auto list = groups.value(name).toArray();
    list.removeAt(row);
    groups.insert(name, list);
    commit(name);
  });
  const auto reorder = [&](int step) {
    const auto name = current();
    auto list = groups.value(name).toArray();
    const int row = members->currentRow(), next = row + step;
    if (row < 0 || next < 0 || next >= list.size())
      return;
    const auto value = list.at(row);
    list.removeAt(row);
    list.insert(next, value);
    groups.insert(name, list);
    commit(name);
    members->setCurrentRow(next);
  };
  button(memberRow, "上移", [&] { reorder(-1); });
  button(memberRow, "下移", [&] { reorder(1); });
  button(groupRow, "导出配置", [&] {
    auto path = QFileDialog::getSaveFileName(&dialog, QString::fromUtf8("导出测点组"),
                                             "measurement-groups.json", "JSON (*.json)");
    if (path.isEmpty())
      return;
    if (!path.endsWith(".json", Qt::CaseInsensitive))
      path += ".json";
    QSaveFile file(path);
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"groups", groups}}).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
      QMessageBox::warning(&dialog, dialog.windowTitle(), QString::fromUtf8("无法保存测点组配置。"));
  });
  button(groupRow, "导入配置", [&] {
    const auto path = QFileDialog::getOpenFileName(&dialog, QString::fromUtf8("导入测点组"),
                                                  {}, QStringLiteral("测点组文件 (*.xlsx *.json);;Excel (*.xlsx);;JSON (*.json)"));
    if (path.isEmpty())
      return;
    QJsonObject imported;
    if (path.endsWith(".xlsx", Qt::CaseInsensitive))
    {
      const auto sheets = BladeFitUI::readWorkbookRows(path, 64);
      for (const auto& rows : sheets)
      {
        for (int header = 0; header < rows.size(); ++header)
        {
          const int groupColumn = rows[header].indexOf(QStringLiteral("测点组"));
          const int tagColumn = rows[header].indexOf(QStringLiteral("原始字段"));
          if (groupColumn < 0 || tagColumn < 0)
            continue;
          if (!imported.isEmpty())
          {
            QMessageBox::warning(&dialog, dialog.windowTitle(),
                                 QStringLiteral("Excel 中找到多张测点组表，请保留一张后重试。"));
            return;
          }
          QMap<QString, QStringList> membersByGroup;
          for (int row = header + 1; row < rows.size(); ++row)
          {
            const auto& name = rows[row][groupColumn];
            const auto& tag = rows[row][tagColumn];
            if (name.isEmpty() && tag.isEmpty())
              continue;
            if (name.isEmpty() || tag.isEmpty())
            {
              QMessageBox::warning(&dialog, dialog.windowTitle(),
                                   QStringLiteral("测点组表的数据行 %1 缺少组名或原始字段。").arg(row + 1));
              return;
            }
            auto& members = membersByGroup[name];
            if (!members.contains(tag))
              members.append(tag);
          }
          for (auto it = membersByGroup.cbegin(); it != membersByGroup.cend(); ++it)
            imported.insert(it.key(), QJsonArray::fromStringList(it.value()));
          break;
        }
      }
      if (imported.isEmpty())
      {
        QMessageBox::warning(&dialog, dialog.windowTitle(),
                             QStringLiteral("Excel 中没有可导入的测点组。请检查「测点组」和「原始字段」列。"));
        return;
      }
    }
    else
    {
      QFile file(path);
      const auto doc = file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll())
                                                     : QJsonDocument();
      const auto envelope = doc.object();
      if (!doc.isObject() || envelope.value("version").toInt() != 1 ||
          !envelope.value("groups").isObject())
      {
        QMessageBox::warning(&dialog, dialog.windowTitle(), QString::fromUtf8("测点组配置格式无效。"));
        return;
      }
      imported = envelope.value("groups").toObject();
    }
    for (auto it = imported.begin(); it != imported.end(); ++it)
    {
      bool valid = !it.key().trimmed().isEmpty() && it.value().isArray();
      for (const auto point : it.value().toArray())
        valid = valid && point.isString() && !point.toString().trimmed().isEmpty();
      if (!valid)
      {
        QMessageBox::warning(&dialog, dialog.windowTitle(), QString::fromUtf8("配置包含无效测点或组名。"));
        return;
      }
    }
    QString firstImported;
    for (auto it = imported.begin(); it != imported.end(); ++it)
    {
      QString name = it.key();
      for (int suffix = 2; groups.contains(name); ++suffix)
        name = QStringLiteral("%1 (%2)").arg(it.key()).arg(suffix);
      groups.insert(name, it.value());
      if (firstImported.isEmpty())
        firstImported = name;
    }
    commit(firstImported.isEmpty() ? current() : firstImported);
    QMessageBox::information(&dialog, dialog.windowTitle(),
                             QStringLiteral("已导入 %1 个测点组。").arg(imported.size()));
  });
  button(memberRow, "关闭", [&] { dialog.accept(); });
  reload({});
  dialog.exec();
}

inline void openGroupManager(QWidget* parent, const PJ::PlotDataMapRef& data)
{
  QStringList available;
  for (const auto& entry : data.numeric)
    available << QString::fromStdString(entry.first);
  available.sort(Qt::CaseInsensitive);
  const QString key = measurementGroupsKey;
  QSettings settings;
  auto saved = QJsonDocument::fromJson(settings.value(key).toByteArray()).object();
  manageGroups(parent, available, saved, [&](const QJsonObject& value) {
    settings.setValue(key, QJsonDocument(value).toJson(QJsonDocument::Compact));
  });
}

using SampleFilter = std::function<bool(const PJ::PlotData&, double, double)>;
struct Condition
{
  QString field;
  int op;
  double lower, upper;
  bool enabled;
};

inline bool conditionPasses(const Condition& rule, double value)
{
  if (!std::isfinite(value)) return false;
  switch (rule.op)
  {
    case 0: return value == rule.lower;
    case 1: return value != rule.lower;
    case 2: return value > rule.lower;
    case 3: return value >= rule.lower;
    case 4: return value < rule.lower;
    case 5: return value <= rule.lower;
    case 6: return value >= rule.lower && value <= rule.upper;
  }
  return false;
}

inline void open(QWidget* parent, const PJ::PlotDataMapRef& data, const QStringList& initial,
                 double start, double end,
                 const std::function<void(const QStringList&, bool, double, double, bool, const SampleFilter&)>& plot,
                 const std::function<void(const QStringList&, double, double, bool, const SampleFilter&)>& dual)
{
  QDialog dialog(parent);
  dialog.setWindowTitle(QString::fromUtf8("录波分析"));
  dialog.resize(760, 650);
  auto* layout = new QVBoxLayout(&dialog);
  auto* channels = new QListWidget;
  auto* search = new QLineEdit;
  search->setPlaceholderText(QString::fromUtf8("搜索测点"));
  layout->addWidget(search);
  layout->addWidget(channels, 1);
  auto* condition = new QComboBox;
  auto* reference = new QComboBox;
  condition->addItem(QString::fromUtf8("不筛选"), QString());
  reference->addItem(QString::fromUtf8("不计算差值"), QString());
  QStringList names;
  for (const auto& entry : data.numeric)
    names << QString::fromStdString(entry.first);
  names.sort(Qt::CaseInsensitive);
  const QVariantMap aliases = qApp->property("bladeScopeTagAliases").toMap();
  for (const auto& name : names)
  {
    const QString alias = aliases.value(name).toString();
    const QString display = alias.isEmpty() ? name : alias + " (" + name + ")";
    auto* item = new QListWidgetItem(display, channels);
    item->setData(Qt::UserRole, name);
    item->setCheckState(initial.contains(name) ? Qt::Checked : Qt::Unchecked);
    condition->addItem(display, name);
    reference->addItem(display, name);
  }
  QObject::connect(search, &QLineEdit::textChanged, &dialog, [=](const QString& text) {
    for (int i = 0; i < channels->count(); ++i)
      channels->item(i)->setHidden(!channels->item(i)->text().contains(text, Qt::CaseInsensitive));
  });
  const auto selected = [=] {
    QStringList list;
    for (int i = 0; i < channels->count(); ++i)
      if (channels->item(i)->checkState() == Qt::Checked)
        list << channels->item(i)->data(Qt::UserRole).toString();
    return list;
  };
  const auto warn = [&](const QString& message) {
    QMessageBox::warning(&dialog, dialog.windowTitle(), message);
  };
  auto* groups = new QComboBox;
  groups->setEditable(true);
  groups->setInsertPolicy(QComboBox::NoInsert);
  groups->lineEdit()->setPlaceholderText(QString::fromUtf8("选择或输入测点组名称"));
  const QString key = measurementGroupsKey;
  QSettings settings;
  QJsonObject saved = QJsonDocument::fromJson(settings.value(key).toByteArray()).object();
  groups->addItems(saved.keys());
  groups->setCurrentIndex(-1);
  auto* groupRow = new QHBoxLayout;
  groupRow->addWidget(groups, 1);
  auto addButton = [&dialog](QHBoxLayout* row, const char* name, auto action) {
    auto* button = new QPushButton(QString::fromUtf8(name));
    row->addWidget(button);
    QObject::connect(button, &QPushButton::clicked, &dialog, action);
  };
  addButton(groupRow, "保存测点组", [&] {
    const QString name = groups->currentText().trimmed();
    const QStringList list = selected();
    if (name.isEmpty() || list.isEmpty())
      return warn(QString::fromUtf8("请输入组名并勾选测点。"));
    if (saved.contains(name) &&
        QMessageBox::question(&dialog, dialog.windowTitle(), QString::fromUtf8("覆盖同名测点组？"))
            != QMessageBox::Yes)
      return;
    if (!saved.contains(name))
      groups->addItem(name);
    saved.insert(name, QJsonArray::fromStringList(list));
    settings.setValue(key, QJsonDocument(saved).toJson(QJsonDocument::Compact));
    groups->setCurrentText(name);
  });
  addButton(groupRow, "管理测点组", [&] {
    const QString selectedGroup = groups->currentText();
    manageGroups(&dialog, names, saved, [&](const QJsonObject& value) {
      settings.setValue(key, QJsonDocument(value).toJson(QJsonDocument::Compact));
    });
    groups->clear();
    groups->addItems(saved.keys());
    if (saved.contains(selectedGroup))
      groups->setCurrentText(selectedGroup);
  });
  addButton(groupRow, "载入测点组", [&] {
    const auto group = saved.value(groups->currentText().trimmed()).toArray();
    if (!saved.contains(groups->currentText().trimmed()))
      return warn(QString::fromUtf8("请选择已保存的测点组。"));
    QStringList missing;
    for (const auto value : group)
      if (!names.contains(value.toString()))
        missing << value.toString();
    for (int i = 0; i < channels->count(); ++i)
    {
      auto* item = channels->item(i);
      item->setCheckState(group.contains(item->data(Qt::UserRole).toString()) ? Qt::Checked : Qt::Unchecked);
    }
    int position = 0;
    for (const auto value : group)
    {
      for (int row = position; row < channels->count(); ++row)
      {
        if (channels->item(row)->data(Qt::UserRole).toString() == value.toString())
        {
          auto* item = channels->takeItem(row);
          channels->insertItem(position++, item);
          item->setHidden(!item->text().contains(search->text(), Qt::CaseInsensitive));
          break;
        }
      }
    }
    if (!missing.isEmpty())
      warn(QString::fromUtf8("录波中缺失：") + missing.join(", "));
  });
  layout->addLayout(groupRow);

  auto* fields = new QFormLayout;
  auto* from = new QLineEdit(number(start));
  auto* to = new QLineEdit(number(end));
  auto* op = new QComboBox;
  op->addItems({"=", "!=", ">", ">=", "<", "<=", QString::fromUtf8("区间")});
  auto* threshold = new QLineEdit("0");
  auto* upper = new QLineEdit("1");
  auto* tolerance = new QLineEdit("1");
  auto* previous = new QCheckBox(QString::fromUtf8("条件测点使用之前最近有效样本"));
  previous->setChecked(true);
  auto* units = new QCheckBox(QString::fromUtf8("确认差值测点单位一致"));
  fields->addRow(QString::fromUtf8("起始时间（原始秒）"), from);
  fields->addRow(QString::fromUtf8("结束时间（原始秒）"), to);
  const auto addSearchableChannel = [&](const QString& field, QComboBox* combo) {
    using Entry = QPair<QString, QVariant>;
    QVector<Entry> entries;
    entries.reserve(combo->count());
    for (int i = 0; i < combo->count(); ++i)
      entries.append(qMakePair(combo->itemText(i), combo->itemData(i)));

    auto* query = new QLineEdit;
    query->setPlaceholderText(QString::fromUtf8("搜索中文名或原始标签"));
    query->setClearButtonEnabled(true);
    query->setAccessibleName(field + QString::fromUtf8("搜索"));
    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo->setMinimumContentsLength(12);
    auto* row = new QHBoxLayout;
    row->addWidget(query, 2);
    row->addWidget(combo, 3);
    fields->addRow(field, row);

    QObject::connect(query, &QLineEdit::textChanged, &dialog, [combo, entries](const QString& text) {
      const QVariant previous = combo->currentData();
      const QString needle = text.trimmed();
      const QSignalBlocker blocker(combo);
      combo->clear();
      for (const auto& entry : entries)
        if (entry.second.toString().isEmpty() || entry.second == previous ||
            entry.first.contains(needle, Qt::CaseInsensitive))
          combo->addItem(entry.first, entry.second);
      const int index = combo->findData(previous);
      combo->setCurrentIndex(index >= 0 ? index : 0);
    });
  };
  addSearchableChannel(QString::fromUtf8("条件测点"), condition);
  auto* comparison = new QHBoxLayout;
  comparison->addWidget(op);
  comparison->addWidget(threshold);
  comparison->addWidget(new QLabel(QString::fromUtf8("区间上限")));
  comparison->addWidget(upper);
  fields->addRow(QString::fromUtf8("条件"), comparison);
  fields->addRow(QString::fromUtf8("最大匹配时间差（秒）"), tolerance);
  fields->addRow(previous);
  addSearchableChannel(QString::fromUtf8("参考测点（目标减参考）"), reference);
  fields->addRow(units);
  layout->addLayout(fields);

  QVector<Condition> conditions;
  auto* filtering = new QCheckBox(QStringLiteral("启用筛选"));
  filtering->setChecked(true);
  layout->addWidget(filtering);
  auto* conditionsList = new QListWidget;
  conditionsList->setMaximumHeight(100);
  layout->addWidget(conditionsList);
  auto* filters = new QHBoxLayout;
  layout->addLayout(filters);
  const auto refresh = [&] {
    const QSignalBlocker block(conditionsList);
    conditionsList->clear();
    for (const auto& rule : conditions)
    {
      auto* item = new QListWidgetItem(QStringLiteral("%1 %2 %3 %4").arg(rule.field,
          op->itemText(rule.op), number(rule.lower), rule.op == 6 ? number(rule.upper) : QString()),
          conditionsList);
      item->setCheckState(rule.enabled ? Qt::Checked : Qt::Unchecked);
    }
  };
  QObject::connect(conditionsList, &QListWidget::itemChanged, &dialog, [&](QListWidgetItem* item) {
    conditions[conditionsList->row(item)].enabled = item->checkState() == Qt::Checked;
  });
  addButton(filters, "添加 AND 条件", [&] {
    bool lowerOK = false, upperOK = false;
    const auto field = condition->currentData().toString();
    const auto lower = threshold->text().toDouble(&lowerOK);
    const auto high = upper->text().toDouble(&upperOK);
    const int operation = op->currentIndex();
    if (field.isEmpty() || !lowerOK || !std::isfinite(lower) ||
        (operation == 6 && (!upperOK || !std::isfinite(high) || high < lower)))
      return warn(QStringLiteral("请选择条件测点并填写有效阈值。"));
    conditions.push_back({field, operation, lower, operation == 6 ? high : 0., true});
    refresh();
  });
  addButton(filters, "删除条件", [&] {
    if (conditionsList->currentRow() < 0) return;
    conditions.removeAt(conditionsList->currentRow());
    refresh();
  });
  addButton(filters, "清空条件", [&] { conditions.clear(); refresh(); });

  const auto templateKey = QStringLiteral("BladeScope/waveformFilterTemplates");
  auto templates = QJsonDocument::fromJson(settings.value(templateKey).toByteArray()).object();
  auto* presets = new QComboBox;
  presets->addItems(templates.keys());
  filters->addWidget(presets, 1);
  addButton(filters, "保存模板", [&] {
    if (conditions.isEmpty()) return warn(QStringLiteral("请先添加条件。"));
    bool ok = false;
    const auto name = QInputDialog::getText(&dialog, QStringLiteral("筛选模板"),
        QStringLiteral("模板名称"), QLineEdit::Normal, {}, &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    if (templates.contains(name) && QMessageBox::question(&dialog, dialog.windowTitle(),
        QStringLiteral("覆盖同名筛选模板？"), QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No) != QMessageBox::Yes) return;
    QJsonArray rows;
    for (const auto& rule : conditions)
      rows.append(QJsonObject{{"field",rule.field},{"op",rule.op},{"lower",rule.lower},
                              {"upper",rule.upper},{"enabled",rule.enabled}});
    if (!templates.contains(name)) presets->addItem(name);
    templates.insert(name, rows);
    presets->setCurrentText(name);
    settings.setValue(templateKey, QJsonDocument(templates).toJson(QJsonDocument::Compact));
  });
  addButton(filters, "载入模板", [&] {
    const auto rows = templates.value(presets->currentText()).toArray();
    if (rows.isEmpty()) return;
    QVector<Condition> loaded;
    for (const auto value : rows)
    {
      const auto row = value.toObject();
      const int operation = row.value("op").toInt(-1);
      const auto field = row.value("field").toString();
      if (operation < 0 || operation > 6 || field.isEmpty())
        return warn(QStringLiteral("筛选模板格式无效。"));
      loaded.push_back({field, operation, row.value("lower").toDouble(),
                        row.value("upper").toDouble(), row.value("enabled").toBool(true)});
    }
    conditions = std::move(loaded);
    refresh();
  });

  const auto makeFilter = [&](SampleFilter& result) {
    if (!filtering->isChecked()) return true;
    QVector<QPair<const PJ::PlotData*, Condition>> inputs;
    for (const auto& rule : conditions)
    {
      if (!rule.enabled) continue;
      const auto entry = data.numeric.find(rule.field.toStdString());
      if (entry == data.numeric.end())
      {
        warn(QStringLiteral("缺少条件测点：%1").arg(rule.field));
        return false;
      }
      inputs.append(qMakePair(&entry->second, rule));
    }
    if (inputs.isEmpty()) return true;
    bool ok = false;
    const double dt = tolerance->text().toDouble(&ok);
    if (!ok || !std::isfinite(dt) || dt < 0)
    {
      warn(QStringLiteral("请输入有效的时间匹配容差。"));
      return false;
    }
    const bool usePrevious = previous->isChecked();
    result = [inputs,dt,usePrevious](const PJ::PlotData& series, double time, double value) {
      for (const auto& input : inputs)
      {
        double comparison = value;
        if (input.first != &series && !match(*input.first,time,dt,usePrevious,comparison)) return false;
        if (!conditionPasses(input.second,comparison)) return false;
      }
      return true;
    };
    return true;
  };
  const auto plotOverview = [&](bool newTab) {
    bool a = false, b = false;
    const auto first = from->text().toDouble(&a), last = to->text().toDouble(&b);
    if (!a || !b || !std::isfinite(first) || !std::isfinite(last) || first > last)
      return warn(QStringLiteral("请输入有效的起止时间。"));
    SampleFilter filter;
    if (makeFilter(filter)) plot(selected(), newTab, first, last, false, filter);
  };
  auto* plotRow = new QHBoxLayout;
  layout->addLayout(plotRow);
  addButton(plotRow, "常规视图（每条最多 5 万点）", [&] {
    if (selected().isEmpty())
      return warn(QString::fromUtf8("请勾选测点。"));
    plotOverview(true);
  });
  addButton(plotRow, "追加到当前图页", [&] {
    if (selected().isEmpty())
      return warn(QString::fromUtf8("请勾选测点。"));
    plotOverview(false);
  });
  auto* results = new QPlainTextEdit;
  results->setReadOnly(true);
  layout->addWidget(results, 1);
  auto* actions = new QHBoxLayout;
  const QStringList viewModes{QStringLiteral("常规视图（每条最多 5 万点）"),
                              QStringLiteral("细化 TS 明细（全原始点）")};
  auto* statisticsMode = new QComboBox;
  statisticsMode->addItems(viewModes);
  actions->addWidget(statisticsMode);
  auto run = [&](bool exportSamples) {
    const QStringList targets = selected();
    bool fromOK = false, toOK = false, toleranceOK = false;
    const double first = from->text().toDouble(&fromOK);
    const double last = to->text().toDouble(&toOK);
    const double dt = tolerance->text().toDouble(&toleranceOK);
    const auto referenceKey = reference->currentData().toString().toStdString();
    const auto referenceEntry = data.numeric.find(referenceKey);
    const auto* baseline = referenceEntry == data.numeric.end() ? nullptr : &referenceEntry->second;
    if (targets.isEmpty() || !fromOK || !toOK || !std::isfinite(first) || !std::isfinite(last) ||
        first > last || (baseline && (!toleranceOK || !std::isfinite(dt) || dt < 0)))
      return warn(QStringLiteral("请检查测点、时间区间和参考测点的匹配时间差。"));
    SampleFilter filter;
    if (!makeFilter(filter)) return;
    const bool detail = statisticsMode->currentIndex() == 1;
    if (baseline && !units->isChecked())
      return warn(QString::fromUtf8("计算差值前请确认单位一致。"));

    QString path;
    if (exportSamples)
    {
      path = QFileDialog::getSaveFileName(&dialog, QString::fromUtf8("导出筛选采样"), {}, "CSV (*.csv)");
      if (path.isEmpty())
        return;
      if (!path.endsWith(".csv", Qt::CaseInsensitive))
        path += ".csv";
    }
    QSaveFile file(path);
    if (exportSamples && !file.open(QIODevice::WriteOnly))
      return warn(QString::fromUtf8("无法创建导出文件。"));
    QTextStream csv(&file);
    csv.setCodec("UTF-8");
    if (exportSamples)
      csv << "raw_tag,timestamp,value,reference_value,difference\n";
    QStringList rows;
    rows << QString::fromUtf8("时间区间：%1 至 %2").arg(number(first), number(last));
    rows << QStringLiteral("数据口径：%1；筛选%2").arg(statisticsMode->currentText(),
        filter ? QStringLiteral("已启用") : QStringLiteral("未启用"));
    for (const auto& name : targets)
    {
      const auto it = data.numeric.find(name.toStdString());
      if (it == data.numeric.end())
        continue;
      const auto& series = it->second;
      Statistics full, filtered, delta;
      const size_t firstIndex = boundary(series, first);
      const size_t stop = boundary(series, last, true);
      std::vector<double> retained, differences;
      size_t rejected = 0;
      BladeTimeView::select(firstIndex, stop, detail, [&](size_t i) { return series.at(i).y; }, [&](size_t i) {
        const auto& sample = series.at(i);
        if (!std::isfinite(sample.y))
        {
          ++full.invalid;
          ++filtered.invalid;
          return;
        }
        full.add(sample.y);
        if (filter && !filter(series, sample.x, sample.y))
        {
          ++rejected;
          return;
        }
        filtered.add(sample.y);
        if (!exportSamples) retained.push_back(sample.y);
        double ref = 0;
        const bool paired = baseline && match(*baseline, sample.x, dt, false, ref);
        if (paired)
        {
          const auto difference = sample.y - ref;
          delta.add(difference);
          if (!exportSamples) differences.push_back(difference);
        }
        else if (baseline)
          ++delta.unmatched;
        if (exportSamples)
        {
          csv << csvCell(name) << ',' << number(sample.x) << ',' << number(sample.y) << ',';
          if (paired)
            csv << number(ref) << ',' << number(sample.y - ref);
          else
            csv << ',';
          csv << '\n';
        }
      });
      rows << "" << name;
      if (filter)
      {
        appendStatistics(rows, QStringLiteral("筛选前"), full);
        rows << QStringLiteral("条件排除或无法配对：%1").arg(qulonglong(rejected));
      }
      appendStatistics(rows, filter ? QStringLiteral("筛选后") : QStringLiteral("全部样本"), filtered, &retained);
      if (baseline)
        appendStatistics(rows, QStringLiteral("参考差值（目标−参考）"), delta, &differences);
    }
    if (exportSamples)
    {
      csv.flush();
      if (csv.status() != QTextStream::Ok || !file.commit())
        return warn(QString::fromUtf8("写入 CSV 失败。"));
    }
    results->setPlainText(rows.join('\n'));
  };
  addButton(actions, "双轴对照", [&] {
    bool firstOK = false, lastOK = false;
    const double first = from->text().toDouble(&firstOK), last = to->text().toDouble(&lastOK);
    if (!firstOK || !lastOK || !std::isfinite(first) || !std::isfinite(last) || first >= last)
      return warn(QStringLiteral("请输入有效的起止时间。"));
    bool accepted = false;

    const auto choice = QInputDialog::getItem(&dialog, QStringLiteral("双轴对照"),
        QStringLiteral("数据视图"), viewModes, statisticsMode->currentIndex(), false, &accepted);
    SampleFilter filter;
    if (accepted && makeFilter(filter)) dual(selected(), first, last, choice == viewModes.back(), filter);
  });
  addButton(actions, "细化 TS 明细（全原始点）", [&] {
    bool startOK = false, endOK = false;
    const auto first = from->text().toDouble(&startOK);
    const auto last = to->text().toDouble(&endOK);
    if (selected().isEmpty() || !startOK || !endOK || !std::isfinite(first) ||
        !std::isfinite(last) || first >= last)
      return warn(QString::fromUtf8("请选择测点并输入有效的起止时间。"));
    SampleFilter filter;
    if (makeFilter(filter)) plot(selected(), true, first, last, true, filter);
  });
  addButton(actions, "预览筛选与统计", [&] { run(false); });
  addButton(actions, "导出筛选采样 CSV", [&] { run(true); });
  addButton(actions, "复制统计", [&] { QApplication::clipboard()->setText(results->toPlainText()); });
  addButton(actions, "关闭", [&] { dialog.reject(); });
  layout->addLayout(actions);
  dialog.exec();
}
}

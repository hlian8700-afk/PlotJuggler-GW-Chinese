#pragma once
#include "PlotJuggler/plotdata.h"
#include <QtWidgets>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace BladeAnalysis
{
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

inline void open(QWidget* parent, const PJ::PlotDataMapRef& data, const QStringList& initial,
                 double start, double end,
                 const std::function<void(const QStringList&, bool)>& plot)
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
  const QString key = "BladeScope/measurementGroups";
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
  addButton(groupRow, "载入测点组", [&] {
    const auto group = saved.value(groups->currentText().trimmed()).toArray();
    if (group.isEmpty())
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
    if (!missing.isEmpty())
      warn(QString::fromUtf8("录波中缺失：") + missing.join(", "));
  });
  addButton(groupRow, "新图页作图", [&] {
    if (selected().isEmpty())
      return warn(QString::fromUtf8("请勾选测点。"));
    plot(selected(), true);
    dialog.accept();
  });
  addButton(groupRow, "追加到当前图页", [&] {
    if (selected().isEmpty())
      return warn(QString::fromUtf8("请勾选测点。"));
    plot(selected(), false);
    dialog.accept();
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
  fields->addRow(QString::fromUtf8("条件测点"), condition);
  auto* comparison = new QHBoxLayout;
  comparison->addWidget(op);
  comparison->addWidget(threshold);
  comparison->addWidget(new QLabel(QString::fromUtf8("区间上限")));
  comparison->addWidget(upper);
  fields->addRow(QString::fromUtf8("条件"), comparison);
  fields->addRow(QString::fromUtf8("最大匹配时间差（秒）"), tolerance);
  fields->addRow(previous);
  fields->addRow(QString::fromUtf8("参考测点（目标减参考）"), reference);
  fields->addRow(units);
  layout->addLayout(fields);

  auto* results = new QPlainTextEdit;
  results->setReadOnly(true);
  layout->addWidget(results, 1);
  auto* actions = new QHBoxLayout;
  auto run = [&](bool exportSamples) {
    const QStringList targets = selected();
    bool fromOK, toOK, thresholdOK, upperOK, toleranceOK;
    const double first = from->text().toDouble(&fromOK);
    const double last = to->text().toDouble(&toOK);
    const double limit = threshold->text().toDouble(&thresholdOK);
    const double high = upper->text().toDouble(&upperOK);
    const double dt = tolerance->text().toDouble(&toleranceOK);
    const int operation = op->currentIndex();
    const auto get = [&](QComboBox* box) -> const PJ::PlotData* {
      const auto it = data.numeric.find(box->currentData().toString().toStdString());
      return it == data.numeric.end() ? nullptr : &it->second;
    };
    const auto* criterion = get(condition);
    const auto* baseline = get(reference);
    if (targets.isEmpty() || !fromOK || !toOK || !std::isfinite(first) || !std::isfinite(last) ||
        first > last || ((criterion || baseline) && (!toleranceOK || !std::isfinite(dt) || dt < 0)) ||
        (criterion && (!thresholdOK || !std::isfinite(limit) ||
         (operation == 6 && (!upperOK || !std::isfinite(high) || high < limit)))))
      return warn(QString::fromUtf8("请检查测点、时间区间、条件值和匹配时间差。"));
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
    if (criterion)
      rows << QString::fromUtf8("条件：%1 %2 %3 %4").arg(condition->currentData().toString(),
          op->currentText(), number(limit), operation == 6 ? number(high) : QString());
    const auto summary = [&](const QString& name, const Statistics& v) {
      rows << QString::fromUtf8("%1：有效 %2，无法匹配 %3，无效 %4")
                  .arg(name).arg(qulonglong(v.count)).arg(qulonglong(v.unmatched)).arg(qulonglong(v.invalid));
      if (v.count)
        rows << QString::fromUtf8("最小 %1，最大 %2，峰峰 %3，均值 %4，标准差 %5")
                    .arg(number(v.low), number(v.high), number(v.high - v.low), number(v.mean),
                         number(std::sqrt(std::max(0.0, v.m2 / double(v.count)))));
    };
    for (const auto& name : targets)
    {
      const auto it = data.numeric.find(name.toStdString());
      if (it == data.numeric.end())
        continue;
      const auto& series = it->second;
      Statistics full, filtered, delta;
      const size_t stop = boundary(series, last, true);
      size_t rejected = 0;
      for (size_t i = boundary(series, first); i < stop; ++i)
      {
        const auto& sample = series.at(i);
        if (!std::isfinite(sample.y))
        {
          ++full.invalid;
          ++filtered.invalid;
          continue;
        }
        full.add(sample.y);
        double value = 0;
        if (criterion && !match(*criterion, sample.x, dt, previous->isChecked(), value))
        {
          ++filtered.unmatched;
          continue;
        }
        if (criterion)
        {
          const bool keep[] = {value == limit, value != limit, value > limit, value >= limit,
                               value < limit, value <= limit, value >= limit && value <= high};
          if (!keep[operation])
          {
            ++rejected;
            continue;
          }
        }
        filtered.add(sample.y);
        double ref = 0;
        const bool paired = baseline && match(*baseline, sample.x, dt, false, ref);
        if (paired)
          delta.add(sample.y - ref);
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
      }
      rows << "" << name;
      summary(QString::fromUtf8("全部样本"), full);
      if (criterion)
      {
        summary(QString::fromUtf8("筛选后"), filtered);
        rows << QString::fromUtf8("条件排除：%1").arg(qulonglong(rejected));
      }
      if (baseline)
        summary(QString::fromUtf8("参考差值"), delta);
    }
    if (exportSamples)
    {
      csv.flush();
      if (csv.status() != QTextStream::Ok || !file.commit())
        return warn(QString::fromUtf8("写入 CSV 失败。"));
    }
    results->setPlainText(rows.join('\n'));
  };
  addButton(actions, "预览筛选与统计", [&] { run(false); });
  addButton(actions, "导出筛选采样 CSV", [&] { run(true); });
  addButton(actions, "关闭", [&] { dialog.reject(); });
  layout->addLayout(actions);
  dialog.exec();
}
}

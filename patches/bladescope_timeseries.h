#pragma once
#include "bladescope_timeseries_math.h"
#include "bladescope_trend.h"
#include "bladescope_analysis.h"
#include "bladescope_recording_index.h"
#include <QClipboard>
#include <QHeaderView>
#include "qwt_date_scale_draw.h"
#include "qwt_plot_curve.h"
#include "qwt_plot_zoomer.h"
#include "qwt_plot_panner.h"
#include "qwt_plot_picker.h"
#include "qwt_legend.h"
#include "qwt_picker_machine.h"
#include "qwt_symbol.h"
#include "qwt_scale_engine.h"

namespace BladeTimeSeries
{
class SeriesCurve final : public QwtPlotCurve
{
public:
  using QwtPlotCurve::QwtPlotCurve;
  std::vector<std::pair<int, int>> spans;

  void drawSeries(QPainter* painter, const QwtScaleMap& xMap, const QwtScaleMap& yMap,
                  const QRectF& canvas, int from, int to) const override
  {
    if (to < 0) to = int(dataSize()) - 1;
    for (const auto& span : spans)
    {
      const int first = std::max(from, span.first);
      const int last = std::min(to, span.second);
      if (first <= last) QwtPlotCurve::drawSeries(painter, xMap, yMap, canvas, first, last);
    }
  }
};

struct Chart : QwtPlot
{
  explicit Chart(QWidget* parent, bool time) : QwtPlot(parent)
  {
    setCanvasBackground(palette().color(QPalette::Base));
    insertLegend(new QwtLegend, QwtPlot::BottomLegend);
    if (time) setAxisScaleDraw(QwtPlot::xBottom, new QwtDateScaleDraw(Qt::LocalTime));
    auto* zoom = new QwtPlotZoomer(canvas());
    zoom->setMousePattern(QwtEventPattern::MouseSelect2, Qt::RightButton);
    auto* pan = new QwtPlotPanner(canvas());
    pan->setMouseButton(Qt::MiddleButton);
    zoomer = zoom;
    canvas()->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(canvas(), &QWidget::customContextMenuRequested, this, [this](QPoint point) {
      QMenu menu;
      auto* restore = menu.addAction(QStringLiteral("恢复视图"));
      auto* range = menu.addAction(QStringLiteral("设置 Y 轴范围"));
      const auto* selected = menu.exec(canvas()->mapToGlobal(point));
      if (selected == restore) reset();
      if (selected != range) return;
      const auto axes = axisEnabled(yRight) ? QStringList{QStringLiteral("左轴"), QStringLiteral("右轴")}
                                           : QStringList{QStringLiteral("左轴")};
      bool ok = false;
      const auto label = QInputDialog::getItem(this, QStringLiteral("Y 轴范围"), QStringLiteral("轴"), axes, 0, false, &ok);
      if (!ok) return;
      const int axis = axes.indexOf(label) == 0 ? yLeft : yRight;
      const auto scale = axisScaleDiv(axis);
      const auto lower = QInputDialog::getDouble(this, label, QStringLiteral("下限"), scale.lowerBound(), -1e20, 1e20, 6, &ok);
      if (!ok) return;
      const auto upper = QInputDialog::getDouble(this, label, QStringLiteral("上限"), scale.upperBound(), -1e20, 1e20, 6, &ok);
      if (!ok) return;
      if (upper <= lower || (dynamic_cast<const QwtLogScaleEngine*>(axisScaleEngine(axis)) && lower <= 0))
      {
        QMessageBox::warning(this, label, QStringLiteral("请输入有效的轴上下限。"));
        return;
      }
      setAxisScale(axis, lower, upper);
      replot();
    });
  }

  void reset()
  {
    setAxisAutoScale(xBottom);
    setAxisAutoScale(yLeft);
    setAxisAutoScale(yRight);
    replot();
    zoomer->setZoomBase();
  }

  QwtPlotCurve* curve(const QString& name, const Points& points, bool scatter, int color,
                      bool time, int axis = yLeft)
  {
    QVector<QPointF> values;
    values.reserve(int(points.size()));
    auto* item = new SeriesCurve(name);
    int first = 0;
    for (const auto p : points)
    {
      if (std::isfinite(p.x) && std::isfinite(p.y))
        values.append(QPointF(p.x * (time ? 1000. : 1.), p.y));
      else if (!scatter && first < values.size())
      {
        item->spans.emplace_back(first, values.size() - 1);
        first = values.size();
      }
    }
    if (first < values.size()) item->spans.emplace_back(first, values.size() - 1);
    const auto ink = QColor::fromHsv((color * 137 + 210) % 360, 185, 190);
    item->setYAxis(axis);
    item->setPen(ink, 1.4);
    if (scatter)
    {
      item->setStyle(QwtPlotCurve::NoCurve);
      item->setSymbol(new QwtSymbol(QwtSymbol::Ellipse, QBrush(ink), QPen(Qt::NoPen), QSize(4, 4)));
    }
    item->setSamples(values);
    item->attach(this);
    return item;
  }

  QwtPlotZoomer* zoomer;
};

inline QString describe(const QString& name, const Points& values)
{
  std::vector<double> sample;
  sample.reserve(values.size());
  for (auto value : values) sample.push_back(value.y);
  const auto s = statistics(std::move(sample));
  if (!s.count) return name + QStringLiteral("　无有效样本");
  return QStringLiteral("%1　n=%2\n最小 %3　最大 %4　平均 %5　极差 %6\nP5 %7　P25 %8　中位数 %9　P75 %10　P95 %11　标准差 %12")
      .arg(name).arg(qulonglong(s.count)).arg(s.min).arg(s.max).arg(s.mean).arg(s.max-s.min)
      .arg(s.p05).arg(s.p25).arg(s.median).arg(s.p75).arg(s.p95).arg(s.sd);
}

inline void showStatistics(QWidget* parent, const QString& text)
{
  QDialog dialog(parent);
  dialog.setWindowTitle(QStringLiteral("数据统计"));
  dialog.resize(620, 360);
  QVBoxLayout layout(&dialog);
  auto* output = new QPlainTextEdit(text);
  output->setReadOnly(true);
  layout.addWidget(output);
  auto* copy = new QPushButton(QStringLiteral("复制统计"));
  layout.addWidget(copy);
  QObject::connect(copy, &QPushButton::clicked, &dialog, [text] { QApplication::clipboard()->setText(text); });
  dialog.exec();
}

inline void compare(QWidget* parent, const QStringList& names, const std::vector<Points>& channels,
                    double tolerance, bool xy)
{
  QDialog dialog(parent);
  dialog.setWindowTitle(xy ? QStringLiteral("XY 图") : QStringLiteral("时序双轴图"));
  dialog.resize(950, 580);
  QVBoxLayout layout(&dialog);
  auto* row = new QHBoxLayout;
  layout.addLayout(row);
  auto* x = new QComboBox;
  auto* y = new QComboBox;
  x->addItems(names);
  y->addItems(names);
  y->setCurrentIndex(std::min(1, int(names.size()) - 1));
  row->addWidget(new QLabel(xy ? QStringLiteral("X") : QStringLiteral("左轴 / 参考量")));
  row->addWidget(x, 1);
  row->addWidget(new QLabel(xy ? QStringLiteral("Y") : QStringLiteral("右轴 / 被减量")));
  row->addWidget(y, 1);
  auto* stats = new QPushButton(QStringLiteral("统计与 Y−X"));
  row->addWidget(stats);
  auto* chart = new Chart(&dialog, !xy);
  auto* log = new QCheckBox(QStringLiteral("Y 对数轴"));
  log->setVisible(xy);
  row->addWidget(log);
  layout.addWidget(chart);
  auto* readout = new QLabel;
  layout.addWidget(readout);
  Points delta, paired;
  struct PairTime { double x, y; };
  std::vector<PairTime> timestamps;
  auto render = [&] {
    if (x->currentIndex() < 0 || y->currentIndex() < 0) return;
    const auto& left = channels[size_t(x->currentIndex())];
    const auto& right = channels[size_t(y->currentIndex())];
    chart->detachItems(QwtPlotItem::Rtti_PlotCurve);
    chart->enableAxis(QwtPlot::yRight, !xy);
    chart->setAxisTitle(QwtPlot::yLeft, xy ? y->currentText() : x->currentText());
    chart->setAxisTitle(QwtPlot::yRight, y->currentText());
    chart->setAxisTitle(QwtPlot::xBottom, xy ? x->currentText() : QStringLiteral("时间"));
    delta.clear();
    paired.clear();
    timestamps.clear();
    for (auto a : left)
    {
      if (!std::isfinite(a.y)) continue;
      const auto* b = nearest(right, a.x, tolerance);
      if (!b || !std::isfinite(b->y)) continue;
      delta.push_back({a.x, b->y - a.y});
      if (!log->isChecked() || b->y > 0)
      {
        paired.push_back({a.y, b->y});
        timestamps.push_back({a.x, b->x});
      }
    }
    if (xy)
    {
      chart->setAxisScaleEngine(QwtPlot::yLeft, log->isChecked() ?
          static_cast<QwtScaleEngine*>(new QwtLogScaleEngine) : new QwtLinearScaleEngine);
      chart->curve(y->currentText(), paired, true, 0, false);
    }
    else
    {
      chart->curve(x->currentText(), left, true, 0, true);
      chart->curve(y->currentText(), right, true, 1, true, QwtPlot::yRight);
    }
    chart->reset();
    readout->setText(QStringLiteral("有效配对 %1 / %2　时间容差 %3 s　差值要求两测点单位一致")
        .arg(qulonglong(delta.size())).arg(qulonglong(left.size())).arg(tolerance));
  };
  if (xy)
  {
    auto* picker = new QwtPlotPicker(chart->canvas());
    picker->setStateMachine(new QwtPickerTrackerMachine);
    picker->setTrackerMode(QwtPicker::AlwaysOn);
    QObject::connect(picker, &QwtPlotPicker::moved, &dialog, [&](const QPointF& cursor) {
      double best = 64;
      size_t selected = paired.size();
      for (size_t i = 0; i < paired.size(); ++i)
      {
        const auto distance = std::pow(chart->transform(QwtPlot::xBottom, paired[i].x) -
            chart->transform(QwtPlot::xBottom, cursor.x()), 2) +
            std::pow(chart->transform(QwtPlot::yLeft, paired[i].y) -
            chart->transform(QwtPlot::yLeft, cursor.y()), 2);
        if (distance < best) { best = distance; selected = i; }
      }
      if (selected < paired.size())
      {
        const auto time = timestamps[selected];
        readout->setText(QStringLiteral("X=%1　Y=%2　X 时间 %3　Y 时间 %4")
            .arg(paired[selected].x).arg(paired[selected].y)
            .arg(QDateTime::fromMSecsSinceEpoch(qint64(time.x * 1000)).toString(Qt::ISODateWithMs),
                 QDateTime::fromMSecsSinceEpoch(qint64(time.y * 1000)).toString(Qt::ISODateWithMs)));
      }
    });
  }
  for (auto* selector : {x, y})
    QObject::connect(selector, &QComboBox::currentTextChanged, &dialog, render);
  QObject::connect(log, &QCheckBox::toggled, &dialog, render);
  QObject::connect(stats, &QPushButton::clicked, &dialog, [&] {
    showStatistics(&dialog, describe(x->currentText(), channels[size_t(x->currentIndex())]) +
        QStringLiteral("\n\n") + describe(y->currentText(), channels[size_t(y->currentIndex())]) +
        QStringLiteral("\n\n") + describe(QStringLiteral("Y−X（配对差值）"), delta));
  });
  render();
  dialog.exec();
}

inline void open(QWidget* parent, const PJ::PlotDataMapRef& source)
{
  if (source.numeric.empty())
  {
    QMessageBox::information(parent, QStringLiteral("时序分析"), QStringLiteral("请先加载录波测点。"));
    return;
  }
  QDialog dialog(parent);
  dialog.setWindowTitle(QStringLiteral("时序分析"));
  dialog.resize(1100, 650);
  QVBoxLayout layout(&dialog);
  auto* tools = new QHBoxLayout;
  layout.addLayout(tools);
  auto* from = new QLineEdit;
  auto* until = new QLineEdit;
  from->setPlaceholderText(QStringLiteral("开始时间（日期时间或秒）"));
  until->setPlaceholderText(QStringLiteral("结束时间（日期时间或秒）"));
  tools->addWidget(from, 1);
  tools->addWidget(until, 1);
  auto add = [&](QHBoxLayout* row, const QString& label) {
    auto* button = new QPushButton(label);
    button->setAutoDefault(false);
    row->addWidget(button);
    return button;
  };
  auto* fetch = add(tools, QStringLiteral("获取数据"));
  auto* previous = add(tools, QStringLiteral("←"));
  auto* next = add(tools, QStringLiteral("→"));
  auto* body = new QSplitter;
  layout.addWidget(body, 1);
  auto* tags = new QListWidget;
  tags->setSelectionMode(QAbstractItemView::ExtendedSelection);
  body->addWidget(tags);
  const auto aliases = qApp->property("bladeScopeTagAliases").toMap();
  QStringList names;
  for (const auto& entry : source.numeric) names << QString::fromStdString(entry.first);
  names.sort(Qt::CaseInsensitive);
  for (const auto& name : names)
  {
    auto* item = new QListWidgetItem(aliases.value(name, name).toString());
    item->setToolTip(name);
    item->setData(Qt::UserRole, name);
    item->setCheckState(Qt::Unchecked);
    tags->addItem(item);
  }
  auto* chart = new Chart(&dialog, true);
  body->addWidget(chart);
  body->setStretchFactor(1, 4);
  auto* actions = new QHBoxLayout;
  layout.addLayout(actions);
  auto* groups = new QComboBox;
  groups->addItem(QStringLiteral("选择测点组"), QString());
  QSettings settings;
  const auto key = QString::fromLatin1(BladeAnalysis::measurementGroupsKey);
  auto stored = QJsonDocument::fromJson(settings.value(key).toByteArray()).object();
  const auto aliasKey = QStringLiteral("BladeScope/timeSeriesGroupAliases");
  auto groupAliases = QJsonDocument::fromJson(settings.value(aliasKey).toByteArray()).object();
  for (const auto& name : stored.keys()) groups->addItem(name, name);
  actions->addWidget(groups);
  auto* scatter = new QCheckBox(QStringLiteral("散点"));
  actions->addWidget(scatter);
  auto* filter = add(actions, QStringLiteral("筛选"));
  auto* clear = add(actions, QStringLiteral("清除筛选"));
  auto* detail = add(actions, QStringLiteral("细化 TS 图"));
  auto* dual = add(actions, QStringLiteral("双轴图"));
  auto* xy = add(actions, QStringLiteral("XY 图"));
  auto* stats = add(actions, QStringLiteral("统计"));
  auto* trend = add(actions, QStringLiteral("添加趋势线"));
  auto* clearTrend = add(actions, QStringLiteral("清除趋势线"));
  auto* status = new QLabel;
  layout.addWidget(status);
  std::vector<Points> raw, processed;
  std::vector<size_t> visible;
  std::vector<Condition> conditions;
  QStringList shown;
  double lo = 0, hi = 0, tolerance = 0;
  unsigned revision = 0;
  std::map<QString, Points> trends;
  auto render = [&] {
    chart->detachItems(QwtPlotItem::Rtti_PlotCurve);
    size_t count = 0;
    for (size_t i = 0; i < processed.size(); ++i)
    {
      chart->curve(shown[int(i)], processed[i], scatter->isChecked(), int(i), true);
      for (const auto p : processed[i]) if (std::isfinite(p.y)) ++count;
    }
    for (const auto& item : trends) chart->curve(item.first, item.second, false, 10, true);
    chart->reset();
    status->setText(QStringLiteral("%1 条曲线　%2 个有效分析点").arg(shown.size()).arg(qulonglong(count)));
  };
  auto apply = [&] {
    ++revision;
    trends.clear();
    processed = prepare(raw, visible, conditions, tolerance);
    render();
  };
  auto read = [&](double start, double end, bool resetConditions) {
    if (std::isnan(start) || std::isnan(end) || end < start)
    {
      QMessageBox::warning(&dialog, dialog.windowTitle(), QStringLiteral("请检查起止时间。"));
      return;
    }
    std::vector<size_t> selected;
    QStringList labels;
    for (int i = 0; i < tags->count(); ++i)
      if (tags->item(i)->checkState() == Qt::Checked)
      {
        selected.push_back(size_t(i));
        labels << tags->item(i)->text();
      }
    if (selected.empty() || selected.size() > 16)
    {
      QMessageBox::warning(&dialog, dialog.windowTitle(), QStringLiteral("请选择 1–16 条实测曲线。"));
      return;
    }
    std::vector<Points> data(size_t(names.size()));
    auto copy = [&](size_t index) {
      auto& points = data[index];
      if (!points.empty()) return;
      const auto& series = source.numeric.at(names[int(index)].toStdString());
      const auto begin = BladeAnalysis::boundary(series, start);
      const auto endIndex = BladeAnalysis::boundary(series, end, true);
      points.reserve(endIndex - begin);
      for (size_t i = begin; i < endIndex; ++i)
      {
        const auto point = series.at(i);
        points.push_back({point.x, point.y});
      }
    };
    if (resetConditions) conditions.clear();
    for (auto index : selected) copy(index);
    for (const auto condition : conditions) copy(condition.channel);
    lo = start;
    hi = end;
    visible = std::move(selected);
    shown = labels;
    raw = std::move(data);
    apply();
  };
  auto parse = [](QLineEdit* entry, double empty) {
    const auto value = entry->text().trimmed();
    return value.isEmpty() ? empty : BladeRecordingIndex::timestamp(value);
  };
  QObject::connect(fetch, &QPushButton::clicked, &dialog, [&] {
    read(parse(from, -std::numeric_limits<double>::infinity()),
         parse(until, std::numeric_limits<double>::infinity()), true);
  });
  for (auto* button : {previous, next})
    QObject::connect(button, &QPushButton::clicked, &dialog, [&, button] {
      if (!std::isfinite(lo) || !std::isfinite(hi) || lo >= hi)
      {
        status->setText(QStringLiteral("请输入有限的起止时间后再滚动。"));
        return;
      }
      const double step = (hi - lo) * (button == next ? 1 : -1);
      read(lo + step, hi + step, false);
      from->setText(QString::number(lo, 'f', 6));
      until->setText(QString::number(hi, 'f', 6));
    });
  QObject::connect(groups, &QComboBox::currentTextChanged, &dialog, [&] {
    const auto name = groups->currentData().toString();
    if (name.isEmpty()) return;
    const auto group = stored.value(name).toArray();
    const auto local = groupAliases.value(name).toObject();
    QStringList missing;
    for (const auto field : group)
      if (!names.contains(field.toString())) missing << field.toString();
    for (int i = 0; i < tags->count(); ++i)
    {
      tags->item(i)->setCheckState(group.contains(names[i]) ? Qt::Checked : Qt::Unchecked);
      tags->item(i)->setText(local.value(names[i]).toString(aliases.value(names[i], names[i]).toString()));
    }
    if (!missing.isEmpty()) status->setText(QStringLiteral("当前录波缺少：") + missing.join(QStringLiteral("、")));
  });
  tags->setContextMenuPolicy(Qt::CustomContextMenu);
  QObject::connect(tags, &QListWidget::customContextMenuRequested, &dialog, [&](QPoint pos) {
    const auto selected = tags->selectedItems();
    if (selected.isEmpty()) return;
    QMenu menu;
    auto* save = menu.addAction(QStringLiteral("以选中测点新建组"));
    if (menu.exec(tags->viewport()->mapToGlobal(pos)) != save) return;
    bool ok = false;
    const auto name = QInputDialog::getText(&dialog, QStringLiteral("新建测点组"),
        QStringLiteral("组名"), QLineEdit::Normal, {}, &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    if (stored.contains(name))
    {
      QMessageBox::warning(&dialog, dialog.windowTitle(), QStringLiteral("组名已经存在。"));
      return;
    }
    QDialog edit(&dialog);
    edit.setWindowTitle(name);
    QVBoxLayout fields(&edit);
    auto* table = new QTableWidget(selected.size(), 2);
    table->setHorizontalHeaderLabels({QStringLiteral("原始测点"), QStringLiteral("组内显示别名")});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    for (int i = 0; i < selected.size(); ++i)
    {
      auto* field = new QTableWidgetItem(selected[i]->data(Qt::UserRole).toString());
      field->setFlags(field->flags() & ~Qt::ItemIsEditable);
      table->setItem(i, 0, field);
      table->setItem(i, 1, new QTableWidgetItem(selected[i]->text()));
    }
    fields.addWidget(table);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    fields.addWidget(&buttons);
    QObject::connect(&buttons, &QDialogButtonBox::accepted, &edit, &QDialog::accept);
    QObject::connect(&buttons, &QDialogButtonBox::rejected, &edit, &QDialog::reject);
    if (edit.exec() != QDialog::Accepted) return;
    QJsonArray members;
    QJsonObject local;
    for (int i = 0; i < table->rowCount(); ++i)
    {
      const auto field = table->item(i, 0)->text();
      const auto alias = table->item(i, 1)->text().trimmed();
      members.append(field);
      if (!alias.isEmpty()) local.insert(field, alias);
    }
    groupAliases.insert(name, local);
    settings.setValue(aliasKey, QJsonDocument(groupAliases).toJson(QJsonDocument::Compact));
    stored.insert(name, members);
    settings.setValue(key, QJsonDocument(stored).toJson(QJsonDocument::Compact));
    groups->addItem(name, name);
    groups->setCurrentIndex(groups->count() - 1);
  });
  QObject::connect(scatter, &QCheckBox::toggled, &dialog, render);
  QObject::connect(filter, &QPushButton::clicked, &dialog, [&] {
    if (visible.empty()) return;
    QDialog editor(&dialog);
    editor.setWindowTitle(QStringLiteral("AND 条件筛选"));
    QVBoxLayout form(&editor);
    auto* rows = new QTableWidget(0, 3);
    rows->setHorizontalHeaderLabels({QStringLiteral("已取数测点"), QStringLiteral("下限（含）"), QStringLiteral("上限（含）")});
    rows->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    form.addWidget(rows);
    auto append = [&](const Condition* condition) {
      const int row = rows->rowCount();
      rows->insertRow(row);
      auto* name = new QComboBox;
      name->addItems(shown);
      if (condition)
      {
        const auto index = std::find(visible.begin(), visible.end(), condition->channel);
        if (index != visible.end()) name->setCurrentIndex(int(index - visible.begin()));
        if (std::isfinite(condition->lower)) rows->setItem(row, 1, new QTableWidgetItem(QString::number(condition->lower, 'g', 16)));
        if (std::isfinite(condition->upper)) rows->setItem(row, 2, new QTableWidgetItem(QString::number(condition->upper, 'g', 16)));
      }
      rows->setCellWidget(row, 0, name);
    };
    for (const auto& condition : conditions) append(&condition);
    if (conditions.empty()) append(nullptr);
    auto* buttons = new QHBoxLayout;
    form.addLayout(buttons);
    auto* plus = add(buttons, QStringLiteral("添加条件"));
    auto* minus = add(buttons, QStringLiteral("删除当前行"));
    auto* diff = new QDoubleSpinBox;
    diff->setRange(0, 3600);
    diff->setDecimals(6);
    diff->setValue(tolerance);
    buttons->addWidget(new QLabel(QStringLiteral("匹配差（秒）")));
    buttons->addWidget(diff);
    QDialogButtonBox accept(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form.addWidget(&accept);
    QObject::connect(plus, &QPushButton::clicked, &editor, [&] { append(nullptr); });
    QObject::connect(minus, &QPushButton::clicked, &editor, [&] {
      if (rows->currentRow() >= 0) rows->removeRow(rows->currentRow());
    });
    QObject::connect(&accept, &QDialogButtonBox::rejected, &editor, &QDialog::reject);
    QObject::connect(&accept, &QDialogButtonBox::accepted, &editor, [&] {
      std::vector<Condition> valid;
      for (int i = 0; i < rows->rowCount(); ++i)
      {
        bool ok = true;
        auto value = [&](int column, double fallback) {
          const auto* item = rows->item(i, column);
          if (!item || item->text().trimmed().isEmpty()) return fallback;
          bool numeric = false;
          const double result = item->text().toDouble(&numeric);
          ok &= numeric && std::isfinite(result);
          return result;
        };
        const double lower = value(1, -std::numeric_limits<double>::infinity());
        const double upper = value(2, std::numeric_limits<double>::infinity());
        if (!ok || lower > upper)
        {
          QMessageBox::warning(&editor, editor.windowTitle(), QStringLiteral("请检查第 %1 行的上下限。").arg(i + 1));
          return;
        }
        if (std::isfinite(lower) || std::isfinite(upper))
          valid.push_back({visible[size_t(qobject_cast<QComboBox*>(rows->cellWidget(i, 0))->currentIndex())], lower, upper});
      }
      conditions = std::move(valid);
      tolerance = diff->value();
      editor.accept();
    });
    if (editor.exec() == QDialog::Accepted) apply();
  });
  QObject::connect(clear, &QPushButton::clicked, &dialog, [&] { conditions.clear(); apply(); });
  QObject::connect(stats, &QPushButton::clicked, &dialog, [&] {
    QStringList lines{QStringLiteral("当前分析数据（降采样、筛选后），总体标准差")};
    for (size_t i = 0; i < processed.size(); ++i) lines << describe(shown[int(i)], processed[i]);
    showStatistics(&dialog, lines.join(QStringLiteral("\n\n")));
  });
  for (auto* button : {dual, xy})
    QObject::connect(button, &QPushButton::clicked, &dialog, [&, button] {
      if (!processed.empty()) compare(&dialog, shown, processed, tolerance, button == xy);
    });
  QObject::connect(detail, &QPushButton::clicked, &dialog, [&] {
    if (visible.empty()) return;
    const auto range = chart->axisScaleDiv(QwtPlot::xBottom);
    QDialog window(&dialog);
    window.setWindowTitle(QStringLiteral("细化 TS 图（原始明细）"));
    window.resize(920, 520);
    QVBoxLayout view(&window);
    auto* chartDetail = new Chart(&window, true);
    view.addWidget(chartDetail);
    size_t count = 0;
    for (size_t i = 0; i < visible.size(); ++i)
    {
      Points part;
      for (const auto point : raw[visible[i]])
        if (point.x * 1000 >= range.lowerBound() && point.x * 1000 <= range.upperBound()) part.push_back(point);
      count += part.size();
      chartDetail->curve(shown[int(i)], part, false, int(i), true);
    }
    view.addWidget(new QLabel(QStringLiteral("原始明细点 %1").arg(qulonglong(count))));
    chartDetail->reset();
    window.exec();
  });
  QObject::connect(trend, &QPushButton::clicked, &dialog, [&] {
    if (!scatter->isChecked() || processed.empty())
    {
      status->setText(QStringLiteral("请先获取数据并切换到散点。"));
      return;
    }
    bool ok = false;
    QStringList targets;
    for (auto index : visible) targets << names[int(index)];
    const auto label = QInputDialog::getItem(&dialog, QStringLiteral("趋势线"), QStringLiteral("目标测点"), targets, 0, false, &ok);
    if (!ok) return;
    const QStringList models{QStringLiteral("线性"), QStringLiteral("二次"), QStringLiteral("三次"),
                             QStringLiteral("指数"), QStringLiteral("滑动平均")};
    const auto model = models.indexOf(QInputDialog::getItem(&dialog, QStringLiteral("趋势线"),
                                         QStringLiteral("模型"), models, 0, false, &ok));
    if (!ok || model < 0) return;
    int width = 5;
    if (model == 4)
    {
      width = QInputDialog::getInt(&dialog, QStringLiteral("滑动平均"), QStringLiteral("窗口样本数"), 5, 1, 50000, 1, &ok);
      if (!ok) return;
    }
    const auto points = processed[size_t(targets.indexOf(label))];
    const auto request = revision;
    BladeTrend::background<Points>(&dialog, [points, model, width] {
      Points valid;
      for (auto p : points) if (std::isfinite(p.y)) valid.push_back(p);
      if (valid.empty()) throw std::runtime_error("没有有效拟合点");
      if (model == 3) return exponential(valid);
      if (model == 4)
      {
        Points smooth;
        Points segment;
        for (auto point : points)
        {
          if (std::isfinite(point.y)) segment.push_back(point);
          else if (!segment.empty())
          {
            const auto part = BladeFit::average(segment, size_t(width));
            smooth.insert(smooth.end(), part.begin(), part.end());
            smooth.push_back(point);
            segment.clear();
          }
        }
        const auto part = BladeFit::average(segment, size_t(width));
        smooth.insert(smooth.end(), part.begin(), part.end());
        return smooth;
      }
      const auto fitted = BladeFit::regress(valid, model + 1);
      return BladeFit::sample(valid.front().x, valid.back().x, fitted);
    }, [&, label, model, request](Points points, const QString& error) {
      if (request != revision) return;
      if (!error.isEmpty()) { status->setText(error); return; }
      trends[label + QStringLiteral(" · ") + QStringList{QStringLiteral("线性"), QStringLiteral("二次"),
          QStringLiteral("三次"), QStringLiteral("指数"), QStringLiteral("滑动平均")}[model]] = std::move(points);
      render();
    });
  });
  QObject::connect(clearTrend, &QPushButton::clicked, &dialog, [&] { ++revision; trends.clear(); render(); });
  dialog.exec();
}
}

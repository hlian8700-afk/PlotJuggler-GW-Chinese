#pragma once
#include "bladescope_analysis.h"
#include "plotwidget.h"
#include "bladescope_plot_theme.h"
#include "bladescope_time_display.h"
#include "bladescope_waveform_sampling.h"
#include <qwt_scale_div.h>
#include <qwt_scale_widget.h>
#include <QClipboard>
#include <QDataStream>
#include <QDrag>
#include <QDragEnterEvent>
#include <QHelpEvent>
#include <memory>
#include <QMimeData>
#include <QToolTip>
#include <qwt_plot.h>
#include <qwt_plot_curve.h>

namespace BladeWaveform
{
inline constexpr auto dragFormat = "application/x-bladescope-waveform-tags";

class UtcScaleDraw final : public BladePlotTheme::ScaleDraw
{
public:
  QwtText label(double value) const override
  {
    const auto ticks = scaleDiv().ticks(QwtScaleDiv::MajorTick);
    const double step = ticks.size() > 1 ? ticks.at(1) - ticks.at(0) : value;
    const bool calendar = BladeTimeDisplay::isCalendarTime(value);
    const int digits = BladeTimeDisplay::decimals(BladeTimeDisplay::precision(), step);
    auto label = BladeTimeDisplay::format(value, true, calendar, calendar, digits, Qt::UTC);
    if (calendar) label.replace(QLatin1Char(' '), QLatin1Char('\n'));
    return label;
  }
};

class Tags final : public QListWidget
{
public:
  using QListWidget::QListWidget;
  bool waveform = false;
protected:
  void startDrag(Qt::DropActions) override
  {
    QStringList fields;
    for (const auto* item : selectedItems()) fields << item->data(Qt::UserRole).toString();
    if (fields.isEmpty()) return;
    auto* mime = new QMimeData;
    if (waveform)
      mime->setData(dragFormat, QJsonDocument(QJsonArray::fromStringList(fields)).toJson(QJsonDocument::Compact));
    else
    {
      QByteArray bytes;
      QDataStream stream(&bytes, QIODevice::WriteOnly);
      for (const auto& field : fields) stream << field;
      mime->setData("curveslist/add_curve", bytes);
    }
    QDrag drag(this);
    drag.setMimeData(mime);
    drag.exec(Qt::CopyAction);
  }
};

class Page final : public QWidget
{
public:
  Page(const PJ::PlotDataMapRef& source, QAction* curveAnalysis, std::function<void()> back, QWidget* sidebar,
       std::function<PlotWidget*()> timePlot, QWidget* parent)
    : QWidget(parent), source(source), shown(std::make_shared<PJ::PlotDataMapRef>())
  {
    setObjectName(QStringLiteral("bladeWaveformPage"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    auto* header = new QHBoxLayout;
    root->addLayout(header);
    auto* returnButton = new QPushButton(QStringLiteral("时序图"), this);
    connect(returnButton, &QPushButton::clicked, this, std::move(back));
    header->addWidget(returnButton);
    auto* analysis = new QPushButton(QStringLiteral("分析"), this);
    analysis->setMinimumWidth(76);
    auto* menu = new QMenu(analysis);
    menu->addAction(curveAnalysis);
    menu->addAction(QStringLiteral("录波分析"))->setEnabled(false);
    analysis->setMenu(menu);
    header->addWidget(analysis);
    view = new QComboBox(this);
    view->addItem(QStringLiteral("常规时序（每条最多 5 万点）"));
    view->addItem(QStringLiteral("细化 TS（全部采样点）"));
    connect(view, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
      if (!plottedFields.isEmpty()) draw(plottedFields);
    });
    header->addWidget(view);
    header->addStretch();

    auto* side = new QVBoxLayout(sidebar);
    status = new QLabel(sidebar);
    side->setContentsMargins(0, 0, 0, 0);
    groups = new QComboBox(sidebar);
    auto* manage = new QPushButton(QStringLiteral("管理测点组"), sidebar);
    side->addWidget(groups);
    side->addWidget(manage);
    search = new QLineEdit(sidebar);
    search->setPlaceholderText(QStringLiteral("搜索中文标签或原始字段"));
    side->addWidget(search);
    labelMode = new QComboBox(sidebar);
    labelMode->addItems({QStringLiteral("原始标签"), QStringLiteral("中文标签"), QStringLiteral("中英双显")});
    side->addWidget(labelMode);
    connect(labelMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { refreshTags(true); });
    tags = new Tags(sidebar);
    tags->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tags->setDragEnabled(true);
    system = new QComboBox(sidebar);
    side->addWidget(system);
    connect(system, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { filterTags(); });
    side->addWidget(tags, 1);
    side->addWidget(status);
    startEnabled = new QCheckBox(QStringLiteral("开始时间（UTC）"), sidebar);
    endEnabled = new QCheckBox(QStringLiteral("结束时间（UTC）"), sidebar);
    start = makeTime(sidebar);
    end = makeTime(sidebar);
    side->addWidget(startEnabled);
    side->addWidget(start);
    side->addWidget(endEnabled);
    side->addWidget(end);
    rollControls = new QWidget(sidebar);
    auto* roll = new QHBoxLayout(rollControls);
    roll->setContentsMargins(0, 0, 0, 0);
    previous = new QPushButton(QStringLiteral("← 上一窗口"), rollControls);
    next = new QPushButton(QStringLiteral("下一窗口 →"), rollControls);
    roll->addWidget(previous);
    roll->addWidget(next);
    side->addWidget(rollControls);
    connect(previous, &QPushButton::clicked, this, [this] { rollWindow(-1); });
    connect(next, &QPushButton::clicked, this, [this] { rollWindow(1); });
    start->setEnabled(false);
    end->setEnabled(false);
    connect(startEnabled, &QCheckBox::toggled, start, &QWidget::setEnabled);
    connect(endEnabled, &QCheckBox::toggled, end, &QWidget::setEnabled);
    style = new QComboBox(sidebar);
    style->addItems({QStringLiteral("折线图"), QStringLiteral("散点图")});
    side->addWidget(style);
    generate = new QPushButton(QStringLiteral("作图"), sidebar);
    side->addWidget(generate);
    analysisControls = new QWidget(sidebar);
    auto* actions = new QHBoxLayout(analysisControls);
    actions->setContentsMargins(0, 0, 0, 0);
    auto* filter = new QPushButton(QStringLiteral("条件筛选"), analysisControls);
    auto* statistics = new QPushButton(QStringLiteral("采样统计"), analysisControls);
    actions->addWidget(filter);
    actions->addWidget(statistics);
    auto* comparison = new QPushButton(QStringLiteral("双轴对照"), analysisControls);
    actions->addWidget(comparison);
    connect(comparison, &QPushButton::clicked, this, [this] { showComparison(); });
    side->addWidget(analysisControls);
    connect(filter, &QPushButton::clicked, this, [this] { editConditions(); });
    connect(statistics, &QPushButton::clicked, this, [this] { showStatistics(); });
    plot = new PlotWidget(*shown, this);
    connect(plot, &QObject::destroyed, [hold = shown] {});
    root->addWidget(plot, 1);
    canvas = plot->findChild<QwtPlot*>();
    if (canvas)
    {
      canvas->canvas()->setAcceptDrops(true);
      canvas->canvas()->setMouseTracking(true);
      canvas->canvas()->installEventFilter(this);
    }
    connect(groups, QOverload<int>::of(&QComboBox::activated), this, [this](int) { refreshTags(); });
    connect(search, &QLineEdit::textChanged, this, [this](const QString& value) {
      Q_UNUSED(value);
      filterTags();
    });
    connect(manage, &QPushButton::clicked, this, [this] {
      BladeAnalysis::openGroupManager(this, this->source);
      refreshGroups();
    });
    connect(generate, &QPushButton::clicked, this, [this, timePlot] {
      QStringList fields;
      for (int i = 0; i < tags->count(); ++i)
        if (tags->item(i)->checkState() == Qt::Checked)
          fields << tags->item(i)->data(Qt::UserRole).toString();
      if (tags->waveform) { draw(fields); return; }
      if (fields.isEmpty() || fields.size() > 16)
      {
        status->setText(QStringLiteral("请选择 1 至 16 个测点。"));
        return;
      }
      if (auto* target = timePlot())
      {
        if (target->isXYPlot()) return;
        for (const auto& field : fields)
        {
          const auto raw = field.toStdString();
          const auto& curves = target->curveList();
          if (std::none_of(curves.begin(), curves.end(), [&](const auto& curve) { return curve.src_name == raw; }))
            target->addCurve(raw);
        }
        emit target->curveListChanged();
        target->zoomOut(false);
        target->replot();
      }
    });
    connect(style, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
      applyStyle();
      QToolTip::hideText();
    });
    refreshGroups();
  }

  void setMode(bool waveform, bool plotting)
  {
    tags->waveform = waveform;
    generate->setVisible(plotting);
    updateTimeControls();
  }

  void filterSearch(bool clear)
  {
    if (clear) search->clear();
    search->setFocus();
    if (!clear) search->selectAll();
  }

  void clearData()
  {
    plottedFields.clear();
    conditions.clear();
    summaries.clear();
    plot->removeAllCurves();
    shown->numeric.clear();
    plot->replot();
    refreshTags();
  }

  void refreshGroups()
  {
    QSettings settings;
    groupsData = QJsonDocument::fromJson(settings.value(BladeAnalysis::measurementGroupsKey).toByteArray()).object();
    const auto selected = groups->currentData().toString();
    const QSignalBlocker block(groups);
    groups->clear();
    groups->addItem(QStringLiteral("全部测点"), QString());
    for (const auto& name : groupsData.keys()) groups->addItem(name, name);
    groups->setCurrentIndex(std::max(0, groups->findData(selected)));
    refreshTags(true);
  }

protected:
  bool eventFilter(QObject* watched, QEvent* event) override
  {
    if (!canvas || watched != canvas->canvas()) return QWidget::eventFilter(watched, event);
    if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove || event->type() == QEvent::Drop)
    {
      auto* drop = static_cast<QDropEvent*>(event);
      if (drop->mimeData()->hasFormat(dragFormat))
      {
        if (event->type() == QEvent::Drop)
        {
          QStringList fields;
          const auto payload = QJsonDocument::fromJson(drop->mimeData()->data(dragFormat)).array();
          for (const auto value : payload) if (value.isString() && !fields.contains(value.toString())) fields << value.toString();
          if (draw(fields))
            for (int i = 0; i < tags->count(); ++i)
              tags->item(i)->setCheckState(fields.contains(tags->item(i)->data(Qt::UserRole).toString()) ? Qt::Checked : Qt::Unchecked);
        }
        drop->acceptProposedAction();
        return true;
      }
    }
    if (event->type() == QEvent::ToolTip)
    {
      auto* help = static_cast<QHelpEvent*>(event);
      tooltip(help->pos(), help->globalPos());
      return true;
    }
    if (event->type() == QEvent::Leave) QToolTip::hideText();
    return QWidget::eventFilter(watched, event);
  }

private:
  void filterTags()
  {
    const auto term = search->text().trimmed();
    const auto category = system->currentData().toString();
    for (int i = 0; i < tags->count(); ++i)
    {
      auto* item = tags->item(i);
      const bool matchesSearch = item->data(Qt::UserRole).toString().contains(term, Qt::CaseInsensitive) ||
                                 item->data(Qt::UserRole + 1).toString().contains(term, Qt::CaseInsensitive);
      item->setHidden(!matchesSearch || (!category.isEmpty() && item->data(Qt::UserRole + 2).toString() != category));
    }
  }

  static QDateTimeEdit* makeTime(QWidget* parent)
  {
    auto* edit = new QDateTimeEdit(parent);
    edit->setTimeSpec(Qt::UTC);
    edit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    edit->setCalendarPopup(true);
    return edit;
  }

  void refreshTags(bool keepChecks = false)
  {
    QSet<QString> checked;
    if (keepChecks)
      for (int i = 0; i < tags->count(); ++i)
        if (tags->item(i)->checkState() == Qt::Checked)
          checked.insert(tags->item(i)->data(Qt::UserRole).toString());
    tags->clear();
    const auto group = groups->currentData().toString();
    QStringList names;
    if (group.isEmpty())
    {
      for (const auto& entry : source.numeric)
        if (entry.second.size()) names << QString::fromStdString(entry.first);
      names.sort(Qt::CaseInsensitive);
    }
    else for (const auto value : groupsData.value(group).toArray()) names << value.toString();
    auto aliases = qApp->property("bladeScopeTagAliases").toMap();
    const auto recordingAliases = qApp->property("bladeScopeRecordingAliases").toMap();
    for (auto it = recordingAliases.cbegin(); it != recordingAliases.cend(); ++it)
      aliases.insert(it.key(), it.value());
    const auto systems = qApp->property("bladeScopeTagSystems").toMap();
    const auto selectedSystem = system->currentData().toString();
    QStringList categories;
    size_t missing = 0;
    double first = std::numeric_limits<double>::infinity(), last = -first;
    for (const auto& name : names)
    {
      const auto it = source.numeric.find(name.toStdString());
      if (it == source.numeric.end() || !it->second.size()) { ++missing; continue; }
      const auto& series = it->second;
      first = std::min(first,series.at(0).x);
      last = std::max(last,series.at(series.size()-1).x);
      const auto label = aliases.value(name).toString();
      const auto title = label.isEmpty() || labelMode->currentIndex() == 0 ? name :
                         labelMode->currentIndex() == 1 ? label : label + QStringLiteral("（") + name + QStringLiteral("）");
      auto* item = new QListWidgetItem(title, tags);
      item->setData(Qt::UserRole, name);
      item->setCheckState(checked.contains(name) ? Qt::Checked : Qt::Unchecked);
      item->setData(Qt::UserRole + 1, label);
      const auto category = systems.value(name, name.section('.', 0, 0)).toString();
      item->setData(Qt::UserRole + 2, category);
      categories << category;
    }
    categories.removeDuplicates();
    categories.sort(Qt::CaseInsensitive);
    {
      const QSignalBlocker block(system);
      system->clear();
      system->addItem(QStringLiteral("全部系统"), QString());
      for (const auto& category : categories) system->addItem(category, category);
      system->setCurrentIndex(std::max(0, system->findData(selectedSystem)));
    }
    filterTags();
    if (std::isfinite(first) && !startEnabled->isChecked())
      start->setDateTime(QDateTime::fromMSecsSinceEpoch(qint64(first*1000),Qt::UTC));
    if (std::isfinite(last) && !endEnabled->isChecked())
      end->setDateTime(QDateTime::fromMSecsSinceEpoch(qint64(last*1000),Qt::UTC));
    updateTimeControls();
    status->setText(source.numeric.empty() ? QStringLiteral("请先加载录波数据") :
        QStringLiteral("可选测点 %1，缺失 %2").arg(tags->count()).arg(qulonglong(missing)));
  }

  void updateTimeControls()
  {
    const bool showTime = tags->waveform && tags->count() > 0;
    if (!showTime && tags->count() == 0)
    {
      startEnabled->setChecked(false);
      endEnabled->setChecked(false);
    }
    startEnabled->setVisible(showTime);
    endEnabled->setVisible(showTime);
    start->setVisible(showTime);
    end->setVisible(showTime);
    rollControls->setVisible(showTime);
    analysisControls->setVisible(tags->waveform);
    analysisControls->setEnabled(tags->count() > 0);
    previous->setEnabled(!plottedFields.isEmpty());
    next->setEnabled(!plottedFields.isEmpty());
    style->setVisible(tags->waveform);
    generate->setEnabled(tags->count() > 0);
  }

  void rollWindow(int direction)
  {
    if (plottedFields.isEmpty() || !canvas) return;
    const auto scale = canvas->axisScaleDiv(QwtPlot::xBottom);
    double first = scale.lowerBound();
    double last = scale.upperBound();
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -minimum;
    for (const auto& field : plottedFields)
    {
      const auto entry = source.numeric.find(field.toStdString());
      if (entry == source.numeric.end() || !entry->second.size()) continue;
      const auto& series = entry->second;
      minimum = std::min(minimum, series.at(0).x);
      maximum = std::max(maximum, series.at(series.size() - 1).x);
    }
    if (!shiftWindow(first, last, minimum, maximum, direction))
    {
      status->setText(QStringLiteral("已到录波边界，可先缩小时间窗口。"));
      return;
    }
    start->setDateTime(QDateTime::fromMSecsSinceEpoch(qRound64(first * 1000), Qt::UTC));
    end->setDateTime(QDateTime::fromMSecsSinceEpoch(qRound64(last * 1000), Qt::UTC));
    startEnabled->setChecked(true);
    endEnabled->setChecked(true);
    draw(plottedFields);
  }

  struct Condition
  {
    QString channel;
    int operation;
    double threshold;
  };
  struct Summary
  {
    BladeAnalysis::Statistics source, retained;
    size_t sampled = 0;
  };

  void editConditions()
  {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("录波条件筛选"));
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(QStringLiteral("全部条件须同时满足。不同测点按最近时间匹配。"), &dialog));
    auto* table = new QTableWidget(0, 3, &dialog);
    table->setHorizontalHeaderLabels({QStringLiteral("条件测点"), QStringLiteral("比较"), QStringLiteral("阈值")});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    layout->addWidget(table);
    QStringList names;
    for (const auto& entry : source.numeric)
      if (entry.second.size()) names << QString::fromStdString(entry.first);
    names.sort(Qt::CaseInsensitive);
    const auto add = [&](const Condition& condition) {
      const int row = table->rowCount();
      table->insertRow(row);
      auto* channel = new QComboBox(table);
      channel->addItems(names);
      if (!condition.channel.isEmpty() && channel->findText(condition.channel) < 0)
        channel->addItem(condition.channel);
      if (!condition.channel.isEmpty()) channel->setCurrentText(condition.channel);
      auto* comparison = new QComboBox(table);
      comparison->addItems({QStringLiteral("≥"), QStringLiteral("≤"), QStringLiteral(">"), QStringLiteral("<")});
      comparison->setCurrentIndex(condition.operation);
      table->setCellWidget(row, 0, channel);
      table->setCellWidget(row, 1, comparison);
      table->setItem(row, 2, new QTableWidgetItem(BladeAnalysis::number(condition.threshold)));
    };
    for (const auto& condition : conditions) add(condition);
    auto* addRow = new QPushButton(QStringLiteral("添加条件"), &dialog);
    auto* removeRow = new QPushButton(QStringLiteral("移除选中条件"), &dialog);
    auto* rowActions = new QHBoxLayout;
    rowActions->addWidget(addRow);
    rowActions->addWidget(removeRow);
    layout->addLayout(rowActions);
    connect(addRow, &QPushButton::clicked, &dialog, [&] { add({{}, 0, 0.0}); });
    connect(removeRow, &QPushButton::clicked, &dialog, [&] {
      if (table->currentRow() >= 0) table->removeRow(table->currentRow());
    });
    auto* toleranceBox = new QLineEdit(BladeAnalysis::number(matchTolerance), &dialog);
    auto* form = new QFormLayout;
    form->addRow(QStringLiteral("最大时间偏差（秒）"), toleranceBox);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    QVector<Condition> result;
    double tolerance = 0.0;
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
      bool valid = false;
      tolerance = toleranceBox->text().toDouble(&valid);
      if (!valid || !std::isfinite(tolerance) || tolerance < 0.0)
      {
        QMessageBox::warning(&dialog, QStringLiteral("条件筛选"), QStringLiteral("请输入有效的非负时间偏差。"));
        return;
      }
      result.clear();
      for (int row = 0; row < table->rowCount(); ++row)
      {
        const auto channel = static_cast<QComboBox*>(table->cellWidget(row, 0))->currentText();
        const auto op = static_cast<QComboBox*>(table->cellWidget(row, 1))->currentIndex();
        const double threshold = table->item(row, 2)->text().toDouble(&valid);
        if (!valid || !std::isfinite(threshold) || !source.numeric.count(channel.toStdString()))
        {
          QMessageBox::warning(&dialog, QStringLiteral("条件筛选"),
                               QStringLiteral("请检查第 %1 行的测点与阈值。").arg(row + 1));
          return;
        }
        result.push_back({channel, op, threshold});
      }
      dialog.accept();
    });
    dialog.resize(620, 340);
    if (dialog.exec() != QDialog::Accepted) return;
    conditions = result;
    matchTolerance = tolerance;
    if (!plottedFields.isEmpty()) draw(plottedFields);
  }

  void showComparison()
  {
    QStringList fields;
    for (const auto& curve : plot->curveList())
      fields << QString::fromStdString(curve.src_name);
    if (fields.size() < 2)
    {
      QMessageBox::information(this, QStringLiteral("双轴对照"), QStringLiteral("请先绘制至少两个有有效数据的测点。"));
      return;
    }

    bool syncing = false;
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("录波双轴对照"));
    auto* root = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    root->addLayout(form);
    auto* layoutMode = new QComboBox(&dialog);
    layoutMode->addItems({QStringLiteral("单图双 Y 轴"), QStringLiteral("上下联动图")});
    auto* left = new QComboBox(&dialog);
    auto* right = new QComboBox(&dialog);
    left->addItems(fields);
    right->addItems(fields);
    right->setCurrentIndex(1);
    form->addRow(QStringLiteral("布局"), layoutMode);
    form->addRow(QStringLiteral("左轴 / 上图"), left);
    form->addRow(QStringLiteral("右轴 / 下图"), right);

    auto* upper = new PlotWidget(*shown, &dialog);
    auto* lower = new PlotWidget(*shown, &dialog);
    root->addWidget(upper, 1);
    root->addWidget(lower, 1);
    auto* upperPlot = upper->findChild<QwtPlot*>();
    auto* lowerPlot = lower->findChild<QwtPlot*>();
    if (!upperPlot || !lowerPlot) return;
    for (auto* chart : {upperPlot, lowerPlot})
    {
      chart->setAxisScaleDraw(QwtPlot::xBottom, new UtcScaleDraw);
      chart->setAxisTitle(QwtPlot::xBottom, canvas->axisTitle(QwtPlot::xBottom));
    }

    const auto link = [&](QwtPlot* from, QwtPlot* to) {
      connect(from->axisWidget(QwtPlot::xBottom), &QwtScaleWidget::scaleDivChanged, &dialog,
              [&, from, to] {
        if (syncing || layoutMode->currentIndex() != 1) return;
        const auto scale = from->axisScaleDiv(QwtPlot::xBottom);
        if (scale == to->axisScaleDiv(QwtPlot::xBottom)) return;
        syncing = true;
        to->setAxisScaleDiv(QwtPlot::xBottom, scale);
        to->replot();
        syncing = false;
      });
    };
    link(upperPlot, lowerPlot);
    link(lowerPlot, upperPlot);

    const auto initialScale = canvas->axisScaleDiv(QwtPlot::xBottom);
    const auto redraw = [&] {
      syncing = true;
      const bool stacked = layoutMode->currentIndex() == 1;
      lower->setVisible(stacked);
      upper->removeAllCurves();
      lower->removeAllCurves();
      upperPlot->enableAxis(QwtPlot::yRight, !stacked);
      upperPlot->setAxisTitle(QwtPlot::yLeft, left->currentText());
      upperPlot->setAxisTitle(QwtPlot::yRight, right->currentText());
      lowerPlot->setAxisTitle(QwtPlot::yLeft, right->currentText());
      const auto curveStyle = style->currentIndex() ? QwtPlotCurve::Dots : QwtPlotCurve::Lines;
      if (auto* item = upper->addCurve(left->currentText().toStdString(), QColor(32, 108, 177)))
        item->curve->setStyle(curveStyle);
      if (stacked || left->currentIndex() != right->currentIndex())
      {
        auto* target = stacked ? lower : upper;
        if (auto* item = target->addCurve(right->currentText().toStdString(), QColor(199, 79, 36)))
        {
          item->curve->setStyle(curveStyle);
          if (!stacked) item->curve->setYAxis(QwtPlot::yRight);
        }
      }
      for (auto* chart : {upperPlot, lowerPlot})
      {
        chart->setAxisAutoScale(QwtPlot::yLeft);
        chart->setAxisAutoScale(QwtPlot::yRight);
        chart->setAxisScaleDiv(QwtPlot::xBottom, initialScale);
        chart->replot();
      }
      syncing = false;
    };
    for (auto* box : {layoutMode, left, right})
      connect(box, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog, redraw);
    redraw();
    dialog.resize(1050, 690);
    dialog.exec();
  }

  void showStatistics()
  {
    if (summaries.isEmpty())
    {
      QMessageBox::information(this, QStringLiteral("采样统计"), QStringLiteral("请先作图。"));
      return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("当前时间窗口采样统计"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* table = new QTableWidget(summaries.size(), 15, &dialog);
    table->setHorizontalHeaderLabels({QStringLiteral("测点"), QStringLiteral("有效原始点"),
      QStringLiteral("重采样后"), QStringLiteral("保留点"), QStringLiteral("最小值"), QStringLiteral("最大值"),
      QStringLiteral("均值"), QStringLiteral("标准差"), QStringLiteral("极差"), QStringLiteral("P05"),
      QStringLiteral("P25"), QStringLiteral("中位数"), QStringLiteral("P75"), QStringLiteral("P95"), QStringLiteral("无匹配")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    int row = 0;
    for (auto it = summaries.cbegin(); it != summaries.cend(); ++it, ++row)
    {
      const auto& kept = it.value().retained;
      const auto value = [&](double x) { return kept.count ? BladeAnalysis::number(x) : QStringLiteral("—"); };
      std::vector<double> sorted;
      const auto curve = shown->numeric.find(it.key().toStdString());
      if (curve != shown->numeric.end())
      {
        sorted.reserve(curve->second.size());
        for (size_t i = 0; i < curve->second.size(); ++i) sorted.push_back(curve->second.at(i).y);
        std::sort(sorted.begin(), sorted.end());
      }
      const auto percentile = [&](double p) {
        if (sorted.empty()) return QStringLiteral("—");
        const double rank = p * (sorted.size() - 1);
        const auto low = size_t(rank);
        const auto high = std::min(low + 1, sorted.size() - 1);
        const double fraction = rank - low;
        return value(sorted[low] * (1.0 - fraction) + sorted[high] * fraction);
      };
      const QStringList cells{it.key(), QString::number(qulonglong(it.value().source.count)),
        QString::number(qulonglong(it.value().sampled)), QString::number(qulonglong(kept.count)), value(kept.low), value(kept.high), value(kept.mean),
        value(kept.count ? std::sqrt(std::max(0.0, kept.m2 / double(kept.count))) : 0.0),
        value(kept.high - kept.low), percentile(0.05), percentile(0.25), percentile(0.5),
        percentile(0.75), percentile(0.95), QString::number(qulonglong(kept.unmatched))};
      for (int column = 0; column < cells.size(); ++column)
        table->setItem(row, column, new QTableWidgetItem(cells.at(column)));
    }
    table->resizeColumnsToContents();
    layout->addWidget(table);
    auto* copy = new QPushButton(QStringLiteral("复制统计"), &dialog);
    layout->addWidget(copy);
    connect(copy, &QPushButton::clicked, &dialog, [table] {
      QStringList rows, cells;
      for (int column = 0; column < table->columnCount(); ++column)
        cells << table->horizontalHeaderItem(column)->text();
      rows << cells.join(QLatin1Char('\t'));
      for (int row = 0; row < table->rowCount(); ++row)
      {
        cells.clear();
        for (int column = 0; column < table->columnCount(); ++column)
          cells << table->item(row, column)->text();
        rows << cells.join(QLatin1Char('\t'));
      }
      QApplication::clipboard()->setText(rows.join(QLatin1Char('\n')));
    });
    dialog.resize(1050, 360);
    dialog.exec();
  }

  bool draw(const QStringList& fields)
  {
    if (fields.isEmpty() || fields.size() > 16)
    {
      status->setText(QStringLiteral("请选择 1 至 16 个测点。"));
      return false;
    }
    const double infinity = std::numeric_limits<double>::infinity();
    const double lo = startEnabled->isChecked() ? start->dateTime().toMSecsSinceEpoch()/1000. : -infinity;
    const double hi = endEnabled->isChecked() ? end->dateTime().toMSecsSinceEpoch()/1000. : infinity;
    if (lo > hi) { status->setText(QStringLiteral("开始时间不能晚于结束时间。")); return false; }
    plot->removeAllCurves();
    shown->numeric.clear();
    auto aliases = qApp->property("bladeScopeTagAliases").toMap();
    const auto recordingAliases = qApp->property("bladeScopeRecordingAliases").toMap();
    for (auto it = recordingAliases.cbegin(); it != recordingAliases.cend(); ++it)
      aliases.insert(it.key(), it.value());
    summaries.clear();
    size_t count = 0;
    double firstPlottedTime = std::numeric_limits<double>::infinity();
    for (const auto& name : fields)
    {
      const auto key = name.toStdString();
      const auto entry = source.numeric.find(key);
      if (entry == source.numeric.end()) continue;
      const auto& series = entry->second;
      const auto stop = BladeAnalysis::boundary(series, hi, true);
      const auto begin = BladeAnalysis::boundary(series, lo);
      if (begin >= stop) continue;
      firstPlottedTime = std::min(firstPlottedTime, series.at(begin).x);
      auto& summary = summaries[name];
      for (size_t i = begin; i < stop; ++i)
        if (std::isfinite(series.at(i).y)) ++summary.source.count;
      std::vector<std::pair<double, double>> retained;
      forEachDisplayIndex(begin, stop, view->currentIndex() == 1,
                         [&](size_t i) { return series.at(i).y; }, [&](size_t i) {
        const auto sample = series.at(i);
        if (!std::isfinite(sample.y)) return;
        ++summary.sampled;
        for (const auto& condition : conditions)
        {
          double value = sample.y;
          if (condition.channel != name)
          {
            const auto match = source.numeric.find(condition.channel.toStdString());
            if (match == source.numeric.end() ||
                !BladeAnalysis::match(match->second, sample.x, matchTolerance, false, value))
            {
              ++summary.retained.unmatched;
              return;
            }
          }
          const double threshold = condition.threshold;
          const bool accept = condition.operation == 0 ? value >= threshold :
                              condition.operation == 1 ? value <= threshold :
                              condition.operation == 2 ? value > threshold : value < threshold;
          if (!accept) return;
        }
        summary.retained.add(sample.y);
        retained.emplace_back(sample.x, sample.y);
      });
      if (retained.empty()) continue;
      auto& points = shown->addNumeric(key)->second;
      for (const auto& sample : retained) points.pushBack({sample.first, sample.second});
      count += retained.size();
      if (auto* curve = plot->addCurve(key))
      {
        const auto alias = aliases.value(name).toString();
        if (!alias.isEmpty()) curve->curve->setTitle(alias);
      }
    }
    if (canvas)
    {
      canvas->setAxisScaleDraw(QwtPlot::xBottom, new UtcScaleDraw);
      canvas->setAxisTitle(QwtPlot::xBottom, BladeTimeDisplay::isCalendarTime(firstPlottedTime)
          ? QStringLiteral("时间（UTC）") : QStringLiteral("时间（秒）"));
    }
    emit plot->curveListChanged();
    plot->zoomOut(false);
    if (canvas && lo < hi && std::isfinite(lo) && std::isfinite(hi))
      canvas->setAxisScale(QwtPlot::xBottom, lo, hi);
    applyStyle();
    status->setText(QStringLiteral("%1 条曲线，%2 个取样点")
        .arg(qulonglong(plot->curveList().size())).arg(qulonglong(count)));
    QToolTip::hideText();
    plottedFields = fields;
    updateTimeControls();
    return count != 0;
  }

  void applyStyle()
  {
    for (const auto& layer : plot->curveList())
      layer.curve->setStyle(style->currentIndex() ? QwtPlotCurve::Dots : QwtPlotCurve::Lines);
    plot->replot();
  }

  void tooltip(QPoint mouse, QPoint global)
  {
    constexpr double radius = 7.;
    double best = radius*radius;
    QString result;
    const double offset = plot->timeOffset();
    const double a = canvas->invTransform(QwtPlot::xBottom, mouse.x()-radius)+offset;
    const double b = canvas->invTransform(QwtPlot::xBottom, mouse.x()+radius)+offset;
    const bool scatter = style->currentIndex() == 1;
    for (const auto& layer : plot->curveList())
    {
      if (!layer.curve->isVisible()) continue;
      const auto it = shown->numeric.find(layer.src_name);
      if (it == shown->numeric.end()) continue;
      const auto& series = it->second;
      const auto begin = BladeAnalysis::boundary(series,std::min(a,b));
      const auto end = BladeAnalysis::boundary(series,std::max(a,b),true);
      const auto stop = scatter ? end : std::min<size_t>(series.size(),end+1);
      const size_t step = std::max<size_t>(1,(stop-begin+255)/256);
      auto projected = [&](size_t i) {
        const auto p = series.at(i);
        return QPointF(canvas->transform(QwtPlot::xBottom,p.x-offset),
                       canvas->transform(QwtPlot::yLeft,p.y));
      };
      for (size_t i = begin; i < stop; i += step)
      {
        const auto sample = series.at(i);
        if (!std::isfinite(sample.y)) continue;
        const auto p = projected(i);
        const auto delta = QPointF(mouse)-p;
        double distance = QPointF::dotProduct(delta,delta);
        if (!scatter && i && std::isfinite(series.at(i-1).y))
        {
          const auto q = projected(i-1), d = p-q, v = QPointF(mouse)-q;
          const auto length = QPointF::dotProduct(d,d);
          if (length > 0)
          {
            const auto t = std::clamp(QPointF::dotProduct(v,d)/length,0.,1.);
            const auto off = QPointF(mouse)-(q+t*d);
            distance = std::min(distance,QPointF::dotProduct(off,off));
          }
        }
        if (!(distance < best)) continue;
        best = distance;
        const bool calendar = BladeTimeDisplay::isCalendarTime(sample.x);
        const int digits = BladeTimeDisplay::precision() == BladeTimeDisplay::Seconds ? 0 : 3;
        const QString time = BladeTimeDisplay::format(sample.x, true, calendar, calendar, digits, Qt::UTC);
        result = QStringLiteral("%1\n时间：%2%3\n数值：%4")
            .arg(layer.curve->title().text(),
                 time, calendar ? QStringLiteral(" UTC") : QString(),
                 BladeAnalysis::number(sample.y));
      }
    }
    if (result.isEmpty()) QToolTip::hideText();
    else QToolTip::showText(global,result,canvas->canvas());
  }

  const PJ::PlotDataMapRef& source;
  std::shared_ptr<PJ::PlotDataMapRef> shown;
  PlotWidget* plot = nullptr;
  QwtPlot* canvas = nullptr;
  QStringList plottedFields;
  QComboBox* view = nullptr;
  QComboBox *groups = nullptr, *style = nullptr;
  Tags* tags = nullptr;
  QLineEdit* search = nullptr;
  QCheckBox *startEnabled = nullptr, *endEnabled = nullptr;
  QComboBox *labelMode = nullptr, *system = nullptr;
  QVector<Condition> conditions;
  QMap<QString, Summary> summaries;
  double matchTolerance = 0.0;
  QWidget* analysisControls = nullptr;
  QWidget* rollControls = nullptr;
  QPushButton *generate = nullptr, *previous = nullptr, *next = nullptr;
  QDateTimeEdit *start = nullptr, *end = nullptr;
  QLabel* status = nullptr;
  QJsonObject groupsData;
};
}

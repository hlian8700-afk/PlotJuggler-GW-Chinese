#pragma once
#include "bladescope_analysis.h"
#include "plotwidget.h"
#include "bladescope_plot_theme.h"
#include "bladescope_time_display.h"
#include "bladescope_waveform_sampling.h"
#include <qwt_scale_div.h>
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
      auto& points = shown->addNumeric(key)->second;
      forEachDisplayIndex(begin, stop, view->currentIndex() == 1,
                          [&](size_t i) { return series.at(i).y; }, [&](size_t i) {
        const auto sample = series.at(i);
        points.pushBack({sample.x, sample.y});
        ++count;
      });
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
    if (count != 0) plottedFields = fields;
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
  QWidget* rollControls = nullptr;
  QPushButton *generate = nullptr, *previous = nullptr, *next = nullptr;
  QDateTimeEdit *start = nullptr, *end = nullptr;
  QLabel* status = nullptr;
  QJsonObject groupsData;
};
}

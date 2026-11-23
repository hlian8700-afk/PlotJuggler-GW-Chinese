#pragma once
#include "bladescope_analysis.h"
#include "plotwidget.h"
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

class Tags final : public QListWidget
{
public:
  using QListWidget::QListWidget;
protected:
  void startDrag(Qt::DropActions) override
  {
    QStringList fields;
    for (const auto* item : selectedItems()) fields << item->data(Qt::UserRole).toString();
    if (fields.isEmpty()) return;
    auto* mime = new QMimeData;
    mime->setData(dragFormat, QJsonDocument(QJsonArray::fromStringList(fields)).toJson(QJsonDocument::Compact));
    QDrag drag(this);
    drag.setMimeData(mime);
    drag.exec(Qt::CopyAction);
  }
};

class Page final : public QWidget
{
public:
  Page(const PJ::PlotDataMapRef& source, QAction* curveAnalysis, std::function<void()> back, QWidget* parent)
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
    auto* analysis = new QToolButton(this);
    analysis->setText(QStringLiteral("分析"));
    analysis->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(analysis);
    menu->addAction(curveAnalysis);
    menu->addAction(QStringLiteral("录波分析"))->setEnabled(false);
    analysis->setMenu(menu);
    header->addWidget(analysis);
    auto* view = new QComboBox(this);
    view->addItem(QStringLiteral("常规时序"));
    header->addWidget(view);
    status = new QLabel(this);
    header->addWidget(status, 1);

    auto* split = new QSplitter(Qt::Horizontal, this);
    root->addWidget(split, 1);
    auto* sidebar = new QWidget(split);
    auto* side = new QVBoxLayout(sidebar);
    groups = new QComboBox(sidebar);
    auto* manage = new QPushButton(QStringLiteral("管理测点组"), sidebar);
    side->addWidget(groups);
    side->addWidget(manage);
    search = new QLineEdit(sidebar);
    search->setPlaceholderText(QStringLiteral("搜索中文标签或原始字段"));
    side->addWidget(search);
    tags = new Tags(sidebar);
    tags->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tags->setDragEnabled(true);
    side->addWidget(tags, 1);
    startEnabled = new QCheckBox(QStringLiteral("开始时间（UTC）"), sidebar);
    endEnabled = new QCheckBox(QStringLiteral("结束时间（UTC）"), sidebar);
    start = makeTime(sidebar);
    end = makeTime(sidebar);
    side->addWidget(startEnabled);
    side->addWidget(start);
    side->addWidget(endEnabled);
    side->addWidget(end);
    start->setEnabled(false);
    end->setEnabled(false);
    connect(startEnabled, &QCheckBox::toggled, start, &QWidget::setEnabled);
    connect(endEnabled, &QCheckBox::toggled, end, &QWidget::setEnabled);
    style = new QComboBox(sidebar);
    style->addItems({QStringLiteral("折线图"), QStringLiteral("散点图")});
    side->addWidget(style);
    auto* generate = new QPushButton(QStringLiteral("作图"), sidebar);
    side->addWidget(generate);
    plot = new PlotWidget(*shown, split);
    connect(plot, &QObject::destroyed, [hold = shown] {});
    split->addWidget(sidebar);
    split->addWidget(plot);
    split->setStretchFactor(1, 1);
    split->setSizes({310, 900});
    canvas = plot->findChild<QwtPlot*>();
    if (canvas)
    {
      canvas->canvas()->setAcceptDrops(true);
      canvas->canvas()->setMouseTracking(true);
      canvas->canvas()->installEventFilter(this);
    }
    connect(groups, QOverload<int>::of(&QComboBox::activated), this, [this](int) { refreshTags(); });
    connect(search, &QLineEdit::textChanged, this, [this](const QString& value) {
      for (int i = 0; i < tags->count(); ++i)
        tags->item(i)->setHidden(!tags->item(i)->text().contains(value.trimmed(), Qt::CaseInsensitive));
    });
    connect(manage, &QPushButton::clicked, this, [this] {
      BladeAnalysis::openGroupManager(this, this->source);
      refreshGroups();
    });
    connect(generate, &QPushButton::clicked, this, [this] {
      QStringList fields;
      for (int i = 0; i < tags->count(); ++i)
        if (tags->item(i)->checkState() == Qt::Checked)
          fields << tags->item(i)->data(Qt::UserRole).toString();
      draw(fields);
    });
    connect(style, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
      applyStyle();
      QToolTip::hideText();
    });
    refreshGroups();
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
    const auto aliases = qApp->property("bladeScopeTagAliases").toMap();
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
      auto* item = new QListWidgetItem(label.isEmpty() ? name : label + QStringLiteral("（") + name + QStringLiteral("）"), tags);
      item->setData(Qt::UserRole, name);
      item->setCheckState(checked.contains(name) ? Qt::Checked : Qt::Unchecked);
      item->setHidden(!item->text().contains(search->text().trimmed(),Qt::CaseInsensitive));
    }
    if (std::isfinite(first) && !startEnabled->isChecked())
      start->setDateTime(QDateTime::fromMSecsSinceEpoch(qint64(first*1000),Qt::UTC));
    if (std::isfinite(last) && !endEnabled->isChecked())
      end->setDateTime(QDateTime::fromMSecsSinceEpoch(qint64(last*1000),Qt::UTC));
    status->setText(QStringLiteral("可选测点 %1，缺失 %2").arg(tags->count()).arg(qulonglong(missing)));
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
    const auto aliases = qApp->property("bladeScopeTagAliases").toMap();
    size_t count = 0;
    for (const auto& name : fields)
    {
      const auto key = name.toStdString();
      const auto entry = source.numeric.find(key);
      if (entry == source.numeric.end()) continue;
      const auto& series = entry->second;
      const auto stop = BladeAnalysis::boundary(series, hi, true);
      const auto begin = BladeAnalysis::boundary(series, lo);
      if (begin >= stop) continue;
      auto& points = shown->addNumeric(key)->second;
      for (size_t i = begin; i < stop; ++i)
      {
        const auto sample = series.at(i);
        points.pushBack({sample.x, sample.y});
        ++count;
      }
      if (auto* curve = plot->addCurve(key))
      {
        const auto alias = aliases.value(name).toString();
        if (!alias.isEmpty()) curve->curve->setTitle(alias);
      }
    }
    emit plot->curveListChanged();
    plot->zoomOut(false);
    applyStyle();
    status->setText(QStringLiteral("%1 条曲线，%2 个取样点")
        .arg(qulonglong(plot->curveList().size())).arg(qulonglong(count)));
    QToolTip::hideText();
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
        result = QStringLiteral("%1\n时间：%2 UTC\n数值：%3")
            .arg(layer.curve->title().text(),
                 QDateTime::fromMSecsSinceEpoch(qint64(sample.x*1000),Qt::UTC).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
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
  QComboBox *groups = nullptr, *style = nullptr;
  Tags* tags = nullptr;
  QLineEdit* search = nullptr;
  QCheckBox *startEnabled = nullptr, *endEnabled = nullptr;
  QDateTimeEdit *start = nullptr, *end = nullptr;
  QLabel* status = nullptr;
  QJsonObject groupsData;
};
}

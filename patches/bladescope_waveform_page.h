#pragma once
#include "bladescope_analysis.h"
#include "bladescope_fit_panel.h"
#include "plotwidget.h"
#include "bladescope_plot_theme.h"
#include "bladescope_time_display.h"
#include "bladescope_waveform_sampling.h"
#include <qwt_picker_machine.h>
#include <qwt_plot_picker.h>
#include <qwt_scale_div.h>
#include <qwt_scale_widget.h>
#include <QClipboard>
#include <QDataStream>
#include <QEventLoop>
#include <QTimer>
#include <QDrag>
#include <QDragEnterEvent>
#include <QHelpEvent>
#include <memory>
#include <QMimeData>
#include <QToolTip>
#include <qwt_plot.h>
#include <qwt_plot_curve.h>
#include "bladescope_trend.h"

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
    auto* headerWidget=new QWidget;BladeUI::toolbar(headerWidget);
    auto* header = new QHBoxLayout(headerWidget);header->setContentsMargins(0,0,0,0);header->setSpacing(6);
    auto* headerScroll=new QScrollArea;headerScroll->setWidgetResizable(true);headerScroll->setFrameShape(QFrame::NoFrame);
    headerScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);headerScroll->setMaximumHeight(60);
    headerScroll->setWidget(headerWidget);root->addWidget(headerScroll);
    auto* returnButton = new QPushButton(QStringLiteral("时序图"), this);
    connect(returnButton, &QPushButton::clicked, this, std::move(back));
    header->addWidget(returnButton);
    auto* analysis = new QPushButton(QStringLiteral("分析"), this);
    analysis->setMinimumWidth(76);analysis->setObjectName("bladeWaveformAnalysisMenu");
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
    modes = new QButtonGroup(this);
    const QStringList labels{QStringLiteral("录波时序"), QStringLiteral("双轴对照"),
                             QStringLiteral("XY 分析"), QStringLiteral("时序趋势拟合")};
    for (int i = 0; i < labels.size(); ++i)
    {
      auto* button = new QPushButton(labels.at(i), this);
      button->setCheckable(true);
      modes->addButton(button, i);
      button->setChecked(i == 0);
      header->addWidget(button);
      if (i == 0) header->addWidget(view);
      connect(button, &QPushButton::clicked, this, [this, i] {
        if (analysisMode == i) return;
        analysisMode = i;
        resetAnalysisPanel();plot->removeAllCurves();plot->hide();lastDrawSignature.clear();
        if(auto* hint=findChild<QLabel*>("bladeWaveformEmpty")){hint->setText(QStringLiteral("模式已切换，请点击作图更新"));hint->show();}
        refreshAnalysisParameters();
        if (i == 0)
        {
          resetAnalysisPanel();
          if (!plottedFields.isEmpty()) draw(plottedFields);
          else status->setText(QStringLiteral("请在左栏勾选测点，再点击「作图」。"));
          return;
        }
        status->setText(QStringLiteral("请在左栏勾选测点，再点击「作图」。"));
      });
    }
    auto* fitData=new QPushButton(QStringLiteral("适应数据"),this);header->addWidget(fitData);
    auto* fullTime=new QPushButton(QStringLiteral("全量时间"),this);header->addWidget(fullTime);
    connect(fitData,&QPushButton::clicked,this,[this]{focusTime(false);});
    connect(fullTime,&QPushButton::clicked,this,[this]{focusTime(true);});
    header->addStretch();
    parameters = new QWidget(this);
    parameterToggle=new QToolButton(this);parameterToggle->setText(QStringLiteral("分析参数 ▾"));parameterToggle->setCheckable(true);parameterToggle->setChecked(true);
    header->addWidget(parameterToggle);connect(parameterToggle,&QToolButton::toggled,this,[this](bool on){parameters->setVisible(on&&analysisMode!=0);});
    parameterSummary=new QLabel(this);parameterSummary->setWordWrap(true);root->addWidget(parameterSummary);
    root->addWidget(parameters);
    parameterForm = new QFormLayout(parameters);
    parameters->hide();
    workspace = new QVBoxLayout;
    root->addLayout(workspace, 1);

    auto* outerSide = new QVBoxLayout(sidebar);outerSide->setContentsMargins(0,0,0,0);
    auto* sideScroll=new QScrollArea(sidebar);sideScroll->setWidgetResizable(true);
    sideScroll->setFrameShape(QFrame::NoFrame);outerSide->addWidget(sideScroll);
    auto* sideBody=new QWidget;sideScroll->setWidget(sideBody);
    auto* side = new QVBoxLayout(sideBody);
    status = new QLabel(sidebar);
    side->setContentsMargins(0, 0, 0, 0);
    groups = new BladeUI::GroupCombo(sidebar);
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
    tags->setMinimumHeight(100);side->addWidget(tags, 1);
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
    auto* analysisLayout = new QVBoxLayout(analysisControls);
    analysisLayout->setContentsMargins(0, 0, 0, 0);
    auto* actions = new QHBoxLayout;
    analysisLayout->addLayout(actions);
    auto* filter = new QPushButton(QStringLiteral("条件筛选"), analysisControls);
    auto* statistics = new QPushButton(QStringLiteral("采样统计"), analysisControls);
    actions->addWidget(filter);
    actions->addWidget(statistics);
    auto* projectActions = new QHBoxLayout;
    analysisLayout->addLayout(projectActions);
    auto* saveProject = new QPushButton(QStringLiteral("保存工程"), analysisControls);
    auto* openProject = new QPushButton(QStringLiteral("打开工程"), analysisControls);
    projectActions->addWidget(saveProject);
    projectActions->addWidget(openProject);
    connect(saveProject, &QPushButton::clicked, this, [this] { saveWaveformProject(); });
    connect(openProject, &QPushButton::clicked, this, [this] { openWaveformProject(); });
    side->addWidget(analysisControls);
    connect(filter, &QPushButton::clicked, this, [this] { editConditions(); });
    connect(statistics, &QPushButton::clicked, this, [this] { if(curveMode&&curveStatistics)curveStatistics();else showStatistics(); });
    plot = new PlotWidget(*shown, this);
    connect(plot, &QObject::destroyed, [hold = shown] {});
    auto* emptyHint=new QLabel(QStringLiteral("请选择测点并点击「作图」"),this);
    emptyHint->setObjectName("bladeWaveformEmpty");emptyHint->setAlignment(Qt::AlignCenter);
    workspace->addWidget(emptyHint);workspace->addWidget(plot, 1);plot->hide();
    canvas = plot->findChild<QwtPlot*>();
    if (canvas)
    {
      canvas->canvas()->setAcceptDrops(true);
      canvas->canvas()->setMouseTracking(true);
      canvas->canvas()->installEventFilter(this);
    }
    connect(groups, QOverload<int>::of(&QComboBox::activated), this, [this](int) { refreshTags(true); });
    connect(search, &QLineEdit::textChanged, this, [this](const QString& value) {
      Q_UNUSED(value);
      filterTags();
    });
    connect(manage, &QPushButton::clicked, this, [this] {
      BladeAnalysis::openGroupManager(this, this->source);
      refreshGroups();
    });
    connect(generate, &QPushButton::clicked, this, [this, timePlot] {
      if(curveMode&&curvePlot){curvePlot(workspaceState());return;}
      if(!tags->waveform&&activateRecording)activateRecording();
      const auto fields = checkedFields();
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
    connect(tags, &QListWidget::itemChanged, this, [this] { refreshAnalysisParameters();status->setText(QStringLiteral("勾选已修改，请点击作图更新")); });
    tags->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tags,&QWidget::customContextMenuRequested,this,[this](QPoint pos){
      if(!curveMode||!associateField)return;auto* item=tags->itemAt(pos);if(!item)return;
      QMenu menu;for(int role=0;role<BladeFit::NRoles;++role){
        auto* action=menu.addAction(QStringLiteral("关联为：")+BladeFitUI::roles()[role]);
        connect(action,&QAction::triggered,this,[this,item,role]{associateField(item->data(Qt::UserRole).toString(),role);});}
      menu.exec(tags->mapToGlobal(pos));
    });
    refreshGroups();
    if (parent && qEnvironmentVariableIsSet("BLADESCOPE_FIT_ACCEPTANCE_OUTPUT"))
      verifyWorkspace();
  }

  std::function<void(QJsonObject)> curvePlot;
  std::function<void()> curveStatistics,activateRecording;
  std::function<void(QString,int)> associateField;
  std::function<QJsonObject()> curveConfiguration;
  std::function<void(QJsonObject)> restoreCurveConfiguration;
  QStringList availableFields()const {QStringList names;for(const auto& entry:source.numeric)names<<QString::fromStdString(entry.first);return names;}
  QJsonObject workspaceState()const {
    QJsonArray filters;for(const auto& c:conditions)filters.append(QJsonObject{{"channel",c.channel},{"operation",c.operation},{"threshold",c.threshold},{"enabled",c.enabled}});
    QJsonObject state{{"conditions",filters},{"filterEnabled",filterEnabled},{"tolerance",matchTolerance}};
    if(startEnabled->isChecked())state["start"]=start->dateTime().toMSecsSinceEpoch()/1000.;
    if(endEnabled->isChecked())state["end"]=end->dateTime().toMSecsSinceEpoch()/1000.;
    return state;
  }
  void setCurveMode(bool on){curveMode=on;updateTimeControls();}
  void focusTime(bool all){
    double lo=std::numeric_limits<double>::infinity(),hi=-lo;
    const auto& data=all?source:*shown;
    for(const auto& item:data.numeric){if(!all&&!summaries.contains(QString::fromStdString(item.first)))continue;
      const auto& s=item.second;if(s.size()){lo=std::min(lo,s.front().x);hi=std::max(hi,s.back().x);}}
    if(!std::isfinite(lo)||!std::isfinite(hi))return;
    if(lo==hi){lo-=.5;hi+=.5;}
    for(auto* target:findChildren<PlotWidget*>()){
      if(target->isXYPlot()||target->isHidden())continue;
      auto rect=target->currentBoundingRect();rect.setLeft(lo-target->timeOffset());rect.setRight(hi-target->timeOffset());
      target->setZoomRectangle(rect,false);target->replot();
    }
  }
  void plotDiagnosticEvidence(const QStringList& fields,double lo,double hi){
    setCurveMode(false);setMode(true,true);analysisMode=0;modes->button(0)->setChecked(true);
    groups->setCurrentIndex(0);refreshTags(true);
    for(int i=0;i<tags->count();++i)tags->item(i)->setCheckState(fields.contains(tags->item(i)->data(Qt::UserRole).toString())?Qt::Checked:Qt::Unchecked);
    start->setDateTime(QDateTime::fromMSecsSinceEpoch(qRound64(lo*1000),Qt::UTC));
    end->setDateTime(QDateTime::fromMSecsSinceEpoch(qRound64(hi*1000),Qt::UTC));
    startEnabled->setChecked(true);endEnabled->setChecked(true);filterEnabled=false;
    refreshAnalysisParameters();draw(fields);
    status->setText(status->text()+QStringLiteral("；诊断证据窗口，条件筛选已关闭，未自动判断故障原因"));
  }
  void setMode(bool waveform, bool plotting)
  {
    tags->waveform = waveform;
    Q_UNUSED(plotting);generate->setVisible(true);
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
    resetAnalysisPanel();
    plottedFields.clear();
    conditions.clear();
    summaries.clear();
    plot->removeAllCurves();
    shown->numeric.clear();plot->hide();
    if(auto* hint=findChild<QLabel*>("bladeWaveformEmpty")){hint->setText(QStringLiteral("请选择测点并点击「作图」"));hint->show();}
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
    const QSignalBlocker blockTags(tags);
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
      item->setData(Qt::UserRole, name);item->setToolTip(name);
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
    refreshAnalysisParameters();
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
    const bool showTime = true;
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
    analysisControls->setVisible(true);
    analysisControls->setEnabled(tags->count() > 0);
    previous->setEnabled(curveMode||!plottedFields.isEmpty());
    next->setEnabled(curveMode||!plottedFields.isEmpty());
    style->setVisible(true);style->setEnabled(!curveMode);
    style->setToolTip(curveMode?QStringLiteral("曲线分析的图形类型由当前分析项决定"):QString());
    generate->setEnabled(tags->count() > 0);
  }

  void rollWindow(int direction)
  {
    if ((!curveMode&&plottedFields.isEmpty()) || !canvas) return;
    const auto scale = canvas->axisScaleDiv(QwtPlot::xBottom);
    double first = curveMode?start->dateTime().toMSecsSinceEpoch()/1000.:scale.lowerBound();
    double last = curveMode?end->dateTime().toMSecsSinceEpoch()/1000.:scale.upperBound();
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -minimum;
    for (const auto& field : (curveMode?availableFields():plottedFields))
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
    if(curveMode&&curvePlot)curvePlot(workspaceState());else draw(plottedFields);
  }

  struct Condition
  {
    QString channel;
    int operation;
    double threshold;
    bool enabled = true;
  };
  void saveWaveformProject()
  {
    QStringList fields = plottedFields;
    if (fields.isEmpty())
      for (int i = 0; i < tags->count(); ++i)
        if (tags->item(i)->checkState() == Qt::Checked)
          fields << tags->item(i)->data(Qt::UserRole).toString();
    if (fields.isEmpty() || fields.size() > 16)
    {
      QMessageBox::warning(this, QStringLiteral("保存工程"), QStringLiteral("请选择 1 至 16 个测点。"));
      return;
    }
    const auto path = QFileDialog::getSaveFileName(this, QStringLiteral("保存录波工程"),
        QStringLiteral("录波工程.json"), QStringLiteral("JSON 文件 (*.json)"));
    if (path.isEmpty()) return;
    QJsonArray filters;
    for (const auto& condition : conditions)
      filters.append(QJsonObject{{QStringLiteral("channel"), condition.channel}, {QStringLiteral("operation"), condition.operation},
                                {QStringLiteral("threshold"), condition.threshold}, {QStringLiteral("enabled"), condition.enabled}});
    const QJsonObject document{
      {"analysisMode",analysisMode},{"comparisonLayout",comparisonLayout},{"xyX",xyX},{"xyY",xyY},{"xyTolerance",xyTolerance},
      {"trendField",trendField},{"trendModel",trendModel},{"trendDegree",trendDegree},{"trendWindow",trendWindow},{"trendRange",trendRange},
      {"curveConfiguration",curveConfiguration?curveConfiguration():QJsonObject()},
      {"comparisonAxes",QJsonObject::fromVariantMap([&]{QVariantMap m;for(auto it=comparisonAxes.begin();it!=comparisonAxes.end();++it)m[it.key()]=it.value();return m;}())},
      {QStringLiteral("format"), QStringLiteral("BladeScopeWaveformProject")}, {QStringLiteral("version"), 1},
      {"group",groups->currentData().toString()},{"search",search->text()},{"labelMode",labelMode->currentIndex()},{"system",system->currentData().toString()},
      {QStringLiteral("fields"), QJsonArray::fromStringList(fields)}, {QStringLiteral("view"), view->currentIndex()},
      {QStringLiteral("style"), style->currentIndex()}, {QStringLiteral("filterEnabled"), filterEnabled},
      {QStringLiteral("tolerance"), matchTolerance}, {QStringLiteral("conditions"), filters},
      {QStringLiteral("start"), startEnabled->isChecked() ? start->dateTime().toUTC().toString(Qt::ISODateWithMs) : QString()},
      {QStringLiteral("end"), endEnabled->isChecked() ? end->dateTime().toUTC().toString(Qt::ISODateWithMs) : QString()}
    };
    const auto bytes = QJsonDocument(document).toJson(QJsonDocument::Indented);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
      QMessageBox::warning(this, QStringLiteral("保存工程"), file.errorString());
  }

  void openWaveformProject()
  {
    const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("打开录波工程"), {},
        QStringLiteral("JSON 文件 (*.json)"));
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
      QMessageBox::warning(this, QStringLiteral("打开工程"), file.errorString());
      return;
    }
    const auto document = QJsonDocument::fromJson(file.readAll()).object();
    const auto fail = [&](const QString& message) {
      QMessageBox::warning(this, QStringLiteral("打开工程"), message);
    };
    if (document.value(QStringLiteral("format")).toString() != QStringLiteral("BladeScopeWaveformProject") ||
        document.value(QStringLiteral("version")).toInt() != 1 || !document.value(QStringLiteral("fields")).isArray() ||
        !document.value(QStringLiteral("conditions")).isArray())
    {
      fail(QStringLiteral("无法识别该录波工程格式。"));
      return;
    }
    const auto viewIndex = document.value(QStringLiteral("view")).toInt(-1);
    const auto styleIndex = document.value(QStringLiteral("style")).toInt(-1);
    const double tolerance = document.value(QStringLiteral("tolerance")).toDouble(-1.0);
    const bool filtering = document.value(QStringLiteral("filterEnabled")).toBool(true);
    const auto first = document.value(QStringLiteral("start")).toString();
    const auto last = document.value(QStringLiteral("end")).toString();
    const auto firstTime = QDateTime::fromString(first, Qt::ISODateWithMs).toUTC();
    const auto lastTime = QDateTime::fromString(last, Qt::ISODateWithMs).toUTC();
    if (viewIndex < 0 || viewIndex >= view->count() || styleIndex < 0 || styleIndex >= style->count() ||
        !std::isfinite(tolerance) || tolerance < 0.0 ||
        (!first.isEmpty() && (!firstTime.isValid() || firstTime < start->minimumDateTime() || firstTime > start->maximumDateTime())) ||
        (!last.isEmpty() && (!lastTime.isValid() || lastTime < end->minimumDateTime() || lastTime > end->maximumDateTime())) ||
        (firstTime.isValid() && lastTime.isValid() && firstTime > lastTime))
    {
      fail(QStringLiteral("工程的显示、时间或筛选参数无效。"));
      return;
    }
    QStringList fields, missing;
    const auto hasSamples = [&](const QString& name) {
      const auto it = source.numeric.find(name.toStdString());
      return it != source.numeric.end() && it->second.size() != 0;
    };
    for (const auto item : document.value(QStringLiteral("fields")).toArray())
    {
      const auto name = item.toString();
      if (name.isEmpty() || fields.contains(name))
      {
        fail(QStringLiteral("工程含有空测点或重复测点。"));
        return;
      }
      fields << name;
      if (!hasSamples(name)) missing << name;
    }
    QVector<Condition> restored;
    for (const auto item : document.value(QStringLiteral("conditions")).toArray())
    {
      const auto record = item.toObject();
      const auto name = record.value(QStringLiteral("channel")).toString();
      const auto op = record.value(QStringLiteral("operation")).toInt(-1);
      const auto threshold = record.value(QStringLiteral("threshold")).toDouble(std::numeric_limits<double>::quiet_NaN());
      const bool enabled = record.value(QStringLiteral("enabled")).toBool(true);
      if (name.isEmpty() || op < 0 || op > 3 || !std::isfinite(threshold))
      {
        fail(QStringLiteral("工程含有无效的筛选条件。"));
        return;
      }
      if (enabled && filtering && !hasSamples(name)) missing << name;
      restored.push_back({name, op, threshold, enabled});
    }
    if (fields.isEmpty() || fields.size() > 16)
    {
      fail(QStringLiteral("工程应包含 1 至 16 个测点。"));
      return;
    }
    missing.removeDuplicates();
    if (!missing.isEmpty())
    {
      fail(QStringLiteral("请先载入所需录波。缺少测点：%1").arg(missing.join(QStringLiteral("、"))));
      return;
    }

    const int mode=document.value("analysisMode").toInt(0);
    if(mode<0||mode>3||document.value("trendModel").toInt(0)<0||document.value("trendModel").toInt(0)>1||
       document.value("trendDegree").toInt(1)<1||document.value("trendDegree").toInt(1)>6||document.value("trendWindow").toInt(11)<1){fail(QStringLiteral("工程分析参数无效"));return;}
    analysisMode=mode;modes->button(mode)->setChecked(true);
    comparisonLayout=document.value("comparisonLayout").toInt(0);comparisonAxes.clear();
    const auto axes=document.value("comparisonAxes").toObject();for(auto it=axes.begin();it!=axes.end();++it)comparisonAxes[it.key()]=it.value().toInt();
    xyX=document.value("xyX").toString();xyY=document.value("xyY").toString();xyTolerance=document.value("xyTolerance").toString();
    trendField=document.value("trendField").toString();trendModel=document.value("trendModel").toInt(0);trendDegree=document.value("trendDegree").toInt(1);
    trendWindow=document.value("trendWindow").toInt(11);trendRange=document.value("trendRange").toInt(0);
    if(restoreCurveConfiguration&&document.value("curveConfiguration").isObject())restoreCurveConfiguration(document.value("curveConfiguration").toObject());
    const QSignalBlocker blockView(view), blockStyle(style);
    view->setCurrentIndex(viewIndex);
    style->setCurrentIndex(styleIndex);
    if (firstTime.isValid()) start->setDateTime(firstTime);
    if (lastTime.isValid()) end->setDateTime(lastTime);
    startEnabled->setChecked(firstTime.isValid());
    endEnabled->setChecked(lastTime.isValid());
    conditions = std::move(restored);
    matchTolerance = tolerance;
    filterEnabled = filtering;
    groups->setCurrentIndex(std::max(0,groups->findData(document.value("group").toString())));
    {QSignalBlocker block(labelMode);labelMode->setCurrentIndex(std::clamp(document.value("labelMode").toInt(0),0,2));}
    refreshTags();search->setText(document.value("search").toString());
    system->setCurrentIndex(std::max(0,system->findData(document.value("system").toString())));
    for (int i = 0; i < tags->count(); ++i)
    {
      auto* item = tags->item(i);
      item->setCheckState(fields.contains(item->data(Qt::UserRole).toString()) ? Qt::Checked : Qt::Unchecked);
    }
    if(curveMode&&curvePlot)curvePlot(workspaceState());else draw(fields);
  }

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
    auto* global = new QCheckBox(QStringLiteral("启用条件筛选"), &dialog);
    global->setChecked(filterEnabled);
    layout->addWidget(global);
    auto* table = new QTableWidget(0, 4, &dialog);
    table->setHorizontalHeaderLabels({QStringLiteral("条件测点"), QStringLiteral("比较"), QStringLiteral("阈值"), QStringLiteral("启用")});
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
      auto* enabled = new QTableWidgetItem;
      enabled->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
      enabled->setCheckState(condition.enabled ? Qt::Checked : Qt::Unchecked);
      table->setItem(row, 3, enabled);
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
    const auto collect = [&](bool applying) {
      bool valid = false;
      tolerance = toleranceBox->text().toDouble(&valid);
      if (!valid || !std::isfinite(tolerance) || tolerance < 0.0)
      {
        QMessageBox::warning(&dialog, QStringLiteral("条件筛选"), QStringLiteral("请输入有效的非负时间偏差。"));
        return false;
      }
      result.clear();
      for (int row = 0; row < table->rowCount(); ++row)
      {
        const auto channel = static_cast<QComboBox*>(table->cellWidget(row, 0))->currentText();
        const auto op = static_cast<QComboBox*>(table->cellWidget(row, 1))->currentIndex();
        const double threshold = table->item(row, 2)->text().toDouble(&valid);
        const bool enabled = table->item(row, 3)->checkState() == Qt::Checked;
        if (!valid || !std::isfinite(threshold) || channel.isEmpty() ||
            (applying && global->isChecked() && enabled && !source.numeric.count(channel.toStdString())))
        {
          QMessageBox::warning(&dialog, QStringLiteral("条件筛选"),
                               QStringLiteral("请检查第 %1 行的测点与阈值。").arg(row + 1));
          return false;
        }
        result.push_back({channel, op, threshold, enabled});
      }
      return true;
    };

    QSettings settings;
    const QString key = QStringLiteral("BladeScope/waveformFilterTemplates");
    auto templates = QJsonDocument::fromJson(settings.value(key).toByteArray()).object();
    auto* templateNames = new QComboBox(&dialog);
    templateNames->addItems(templates.keys());
    auto* load = new QPushButton(QStringLiteral("载入模板"), &dialog);
    auto* save = new QPushButton(QStringLiteral("保存模板"), &dialog);
    rowActions->addWidget(templateNames, 1);
    rowActions->addWidget(load);
    rowActions->addWidget(save);
    connect(load, &QPushButton::clicked, &dialog, [&] {
      const auto name = templateNames->currentText();
      if (!templates.contains(name)) return;
      const auto entry = templates.value(name).toObject();
      table->setRowCount(0);
      global->setChecked(entry.value(QStringLiteral("enabled")).toBool(true));
      toleranceBox->setText(BladeAnalysis::number(entry.value(QStringLiteral("tolerance")).toDouble()));
      for (const auto& item : entry.value(QStringLiteral("conditions")).toArray())
      {
        const auto row = item.toObject();
        add({row.value(QStringLiteral("channel")).toString(), row.value(QStringLiteral("operation")).toInt(),
             row.value(QStringLiteral("threshold")).toDouble(), row.value(QStringLiteral("enabled")).toBool(true)});
      }
    });
    connect(save, &QPushButton::clicked, &dialog, [&] {
      if (!collect(false)) return;
      bool confirmed = false;
      const auto name = QInputDialog::getText(&dialog, QStringLiteral("保存工况模板"), QStringLiteral("模板名称"),
          QLineEdit::Normal, templateNames->currentText(), &confirmed).trimmed();
      if (!confirmed || name.isEmpty()) return;
      if (templates.contains(name) && QMessageBox::question(&dialog, QStringLiteral("保存工况模板"),
          QStringLiteral("覆盖同名模板？")) != QMessageBox::Yes) return;
      QJsonArray rows;
      for (const auto& condition : result)
        rows.append(QJsonObject{{QStringLiteral("channel"), condition.channel},
                    {QStringLiteral("operation"), condition.operation}, {QStringLiteral("threshold"), condition.threshold},
                    {QStringLiteral("enabled"), condition.enabled}});
      templates.insert(name, QJsonObject{{QStringLiteral("enabled"), global->isChecked()},
          {QStringLiteral("tolerance"), tolerance}, {QStringLiteral("conditions"), rows}});
      settings.setValue(key, QJsonDocument(templates).toJson(QJsonDocument::Compact));
      if (templateNames->findText(name) < 0) templateNames->addItem(name);
      templateNames->setCurrentText(name);
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
      if (collect(true)) dialog.accept();
    });
    dialog.resize(620, 340);
    if (dialog.exec() != QDialog::Accepted) return;
    conditions = result;
    matchTolerance = tolerance;
    filterEnabled = global->isChecked();
    status->setText(QStringLiteral("条件已修改，请点击作图更新"));
  }

  static void verifyWorkspace()
  {
    PJ::PlotDataMapRef recording;
    auto& speed = recording.addNumeric("speed")->second;
    auto& torque = recording.addNumeric("torque")->second;
    for (int i = 0; i < 32; ++i)
    {
      speed.pushBack({1790000000.+double(i), 100.0 + i});
      torque.pushBack({1790000000.+double(i), 20.0 + 2 * i});
    }
    QAction analysis(nullptr);
    QWidget sidebar;
    Page page(recording, &analysis, [] {}, &sidebar, []() -> PlotWidget* { return nullptr; }, nullptr);
    page.setMode(true, true);
    page.groups->setCurrentIndex(0);
    page.refreshTags();
    const auto require = [](bool passed, const char* message) {
      if (!passed) qFatal("BladeScope waveform acceptance: %s", message);
    };
    require(page.tags->count() == 2, "fixture measurements missing");
    page.tags->item(0)->setCheckState(Qt::Checked);
    page.tags->item(1)->setCheckState(Qt::Checked);
    page.generate->click();
    require(page.plot->curveList().size() == 2, "time plot did not draw the checked measurements");

    const auto timeRange=page.canvas->axisScaleDiv(QwtPlot::xBottom);
    require(timeRange.upperBound()-timeRange.lowerBound()<40 && timeRange.lowerBound()>=1790000000.,"absolute recording time was stretched into empty years");
    page.setMode(false,false);page.setCurveMode(true);
    require(!page.generate->isHidden()&&!page.startEnabled->isHidden()&&!page.analysisControls->isHidden(),"shared sidebar controls disappeared in curve mode");
    page.setCurveMode(false);page.setMode(true,true);
    page.modes->button(1)->click();
    require(page.checkedFields().size() == 2 && page.parameterForm->rowCount() > 1 &&
            page.plot->isHidden() && !page.analysisPanel, "comparison switch retained an obsolete plot");
    auto* layout = qobject_cast<QComboBox*>(page.parameterForm->itemAt(0, QFormLayout::FieldRole)->widget());
    require(layout != nullptr, "comparison layout control missing");
    layout->setCurrentIndex(1);
    page.generate->click();
    require(page.analysisPanel && page.analysisPanel->parentWidget() == &page && !page.analysisPanel->isWindow(),
            "comparison plot is outside the workspace");
    const auto charts = page.analysisPanel->findChildren<QwtPlot*>();
    require(charts.size() == 2, "stacked comparison does not have two charts");
    auto* upper = charts.at(0);
    auto* lower = charts.at(1);
    upper->setAxisScale(QwtPlot::xBottom, 1790000005.0, 1790000012.0);
    upper->replot();
    require(upper->axisScaleDiv(QwtPlot::xBottom) == lower->axisScaleDiv(QwtPlot::xBottom),
            "stacked charts do not share the time scale");

    page.modes->button(2)->click();
    require(page.checkedFields().size() == 2 && page.parameterForm->rowCount() == 3 && !page.analysisPanel,
            "XY setup did not preserve selection or show parameters");
    page.generate->click();
    require(page.analysisPanel && !page.analysisPanel->isWindow(), "XY chart is outside the workspace");
    auto* xy = page.analysisPanel->findChild<PlotWidget*>();
    require(xy && xy->isXYPlot() && xy->curveList().size() == 1, "XY scatter did not draw");

    page.modes->button(3)->click();
    page.tags->item(1)->setCheckState(Qt::Unchecked);
    require(page.parameterForm->rowCount() == 4 && !page.analysisPanel,
            "trend setup did not preserve the existing plot");
    page.generate->click();
    require(page.analysisPanel && page.analysisPanel->parentWidget() == &page && !page.plot->isHidden(),
            "trend controls and time plot are not in the workspace");
    auto* summary = page.analysisPanel->findChild<QLabel*>();
    require(summary != nullptr, "trend summary is missing");
    QEventLoop waiting;
    QTimer poll;
    connect(&poll, &QTimer::timeout, &waiting, [&] {
      if (page.plot->curveList().size() >= 2) waiting.quit();
    });
    QTimer::singleShot(5000, &waiting, &QEventLoop::quit);
    poll.start(20);
    waiting.exec();
    require(page.plot->curveList().size() == 2 && !summary->text().isEmpty() &&
            summary->text() != QStringLiteral("正在拟合…"), "trend overlay or summary did not finish");

    page.modes->button(0)->click();
    require(!page.analysisPanel && page.checkedFields().size() == 1 && page.plot->curveList().size() == 1,
            "returning to time view did not preserve the new selection");
  }

  QStringList checkedFields() const
  {
    QStringList fields;
    for (int i = 0; i < tags->count(); ++i)
      if (tags->item(i)->checkState() == Qt::Checked)
        fields << tags->item(i)->data(Qt::UserRole).toString();
    return fields;
  }

  void refreshAnalysisParameters()
  {
    parameters->setVisible(analysisMode != 0 && parameterToggle->isChecked());
    parameterToggle->setVisible(analysisMode!=0);
    parameterSummary->setText(analysisMode==3?QStringLiteral("趋势拟合：调整参数后点击左栏作图；当前可见范围在点击时锁定"):QString());
    while (parameterForm->rowCount()) parameterForm->removeRow(0);
    if (analysisMode == 0) return;
    const auto fields = checkedFields();
    const int minimum = analysisMode == 3 ? 1 : 2;
    if (fields.size() < minimum || fields.size() > 16)
    {
      parameterForm->addRow(new QLabel(QStringLiteral("请勾选 %1 至 16 个测点。").arg(minimum), parameters));
      return;
    }

    const auto addChoice = [this](const QString& label, const QStringList& values, int index) {
      auto* box = new QComboBox(parameters);
      box->addItems(values);
      box->setCurrentIndex(index);
      parameterForm->addRow(label, box);
      return box;
    };

    if (analysisMode == 1)
    {
      auto* layout = addChoice(QStringLiteral("布局"),
          {QStringLiteral("单图双 Y 轴"), QStringLiteral("上下联动图")}, comparisonLayout);
      connect(layout, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
              [this](int index) { comparisonLayout = index; });
      auto* expand = new QPushButton(QStringLiteral("配置左右轴测点"), parameters);
      expand->setCheckable(true);
      auto* assignments = new QWidget(parameters);
      auto* grid = new QGridLayout(assignments);
      for (int i = 0; i < fields.size(); ++i)
      {
        const auto field = fields.at(i);
        auto* axis = new QComboBox(assignments);
        axis->addItems({QStringLiteral("不显示"), QStringLiteral("左轴 / 上图"), QStringLiteral("右轴 / 下图")});
        axis->setCurrentIndex(comparisonAxes.value(field, i == 0 ? 1 : i == 1 ? 2 : 0));
        grid->addWidget(new QLabel(field, assignments), i / 2, 2 * (i % 2));
        grid->addWidget(axis, i / 2, 2 * (i % 2) + 1);
        connect(axis, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, field](int index) { comparisonAxes.insert(field, index); });
      }
      parameterForm->addRow(expand);
      parameterForm->addRow(assignments);
      assignments->hide();
      connect(expand, &QPushButton::toggled, assignments, &QWidget::setVisible);
      return;
    }

    if (analysisMode == 2)
    {
      const int xIndex = std::max(0, int(fields.indexOf(xyX)));
      const int yIndex = fields.contains(xyY) ? fields.indexOf(xyY) : (xIndex == 0 ? 1 : 0);
      auto* x = addChoice(QStringLiteral("X 测点"), fields, xIndex);
      auto* y = addChoice(QStringLiteral("Y 测点"), fields, yIndex);
      xyX = x->currentText();
      xyY = y->currentText();
      connect(x, &QComboBox::currentTextChanged, this, [this](const QString& value) { xyX = value; });
      connect(y, &QComboBox::currentTextChanged, this, [this](const QString& value) { xyY = value; });
      auto* tolerance = new QLineEdit(xyTolerance.isEmpty() ? BladeAnalysis::number(matchTolerance) : xyTolerance, parameters);
      parameterForm->addRow(QStringLiteral("最大时间偏差（秒）"), tolerance);
      connect(tolerance, &QLineEdit::textEdited, this, [this](const QString& value) { xyTolerance = value; });
      return;
    }

    auto* field = addChoice(QStringLiteral("拟合测点"), fields, std::max(0, int(fields.indexOf(trendField))));
    trendField = field->currentText();
    connect(field, &QComboBox::currentTextChanged, this, [this](const QString& value) { trendField = value; });
    auto* model = addChoice(QStringLiteral("模型"),
        {QStringLiteral("多项式"), QStringLiteral("滑动平均")}, trendModel);
    auto* size = new QSpinBox(parameters);
    auto* sizeLabel=new QLabel(parameters);parameterForm->addRow(sizeLabel,size);
    const auto setModel = [this, size,sizeLabel](int index) {
      sizeLabel->setText(index==0?QStringLiteral("多项式阶数"):QStringLiteral("窗口点数（点）"));
      size->setToolTip(index==0?QStringLiteral("1 阶为线性趋势；需要足够的不同时间点"):QStringLiteral("按有效样本点数滑动平均，不等同于固定秒数"));
      trendModel = index;
      const QSignalBlocker block(size);
      size->setRange(1, index == 0 ? 6 : 10000000);
      size->setValue(index == 0 ? trendDegree : trendWindow);
    };
    setModel(trendModel);
    connect(model, QOverload<int>::of(&QComboBox::currentIndexChanged), this, setModel);
    connect(size, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
      if (trendModel == 0) trendDegree = value;
      else trendWindow = value;
    });
    auto* range = addChoice(QStringLiteral("拟合范围"),
        {QStringLiteral("已绘制范围"), QStringLiteral("当前可见时段")}, trendRange);
    connect(range, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) { trendRange = index; });
  }

  void resetAnalysisPanel()
  {
    delete analysisPanel;
    analysisPanel = nullptr;
    if (plot) plot->setVisible(!shown->numeric.empty());
    if(auto* hint=findChild<QLabel*>("bladeWaveformEmpty"))hint->setVisible(shown->numeric.empty());
  }

  QWidget* makeAnalysisPanel()
  {
    analysisPanel = new QWidget(this);
    workspace->addWidget(analysisPanel, 1);
    plot->hide();
    if(auto* hint=findChild<QLabel*>("bladeWaveformEmpty"))hint->hide();
    return analysisPanel;
  }

  void showTrend()
  {
    auto* controls = new QWidget(this);
    analysisPanel = controls;
    workspace->insertWidget(0, controls);
    auto* row = new QHBoxLayout(controls);
    auto* summary = new QLabel(controls);
    summary->setWordWrap(true);
    row->addWidget(summary);
    const auto name = trendField.toStdString();
    const auto fit = [=] {
      const auto found = shown->numeric.find(name);
      if (found == shown->numeric.end() || !found->second.size())
      {
        summary->setText(QStringLiteral("拟合测点在当前时间段内没有有效数据。"));
        return;
      }
      auto data = std::make_shared<BladeFit::Points>();
      const auto& series = found->second;
      for (size_t i = 0; i < series.size(); ++i)
      {
        const auto point = series.at(i);
        data->push_back({point.x, point.y});
      }
      const auto visible = canvas->axisScaleDiv(QwtPlot::xBottom);
      const double lo = trendRange && std::isfinite(fitVisibleLow) ? fitVisibleLow : data->front().x;
      const double hi = trendRange && std::isfinite(fitVisibleHigh) ? fitVisibleHigh : data->back().x;
      const int mode = trendModel, order = trendDegree, window = trendWindow;
      summary->setText(QStringLiteral("正在拟合…"));
      BladeTrend::background<BladeTrend::Trend>(controls, [=] {
        return BladeTrend::calculate(*data, mode, order, window, lo, hi);
      }, [=](BladeTrend::Trend result, const QString& error) {
        summary->setText(error.isEmpty()?result.summary+QStringLiteral("；时间范围 %1 ～ %2 UTC").arg(
          BladeTimeDisplay::format(lo,true,true,true,3,Qt::UTC),BladeTimeDisplay::format(hi,true,true,true,3,Qt::UTC)):error);
        if (!error.isEmpty() || result.points.empty()) return;
        const auto base = name + "/Trend";
        auto curveName = base;
        for (int n = 2; shown->getAllNames().count(curveName); ++n) curveName = base + std::to_string(n);
        auto& target = shown->addNumeric(curveName)->second;
        for (const auto point : result.points) target.pushBack({point.x, point.y});
        if (auto* curve = plot->addCurve(curveName, QColor(226, 88, 40))){
          curve->curve->setStyle(QwtPlotCurve::Lines);curve->curve->setPen(QPen(QColor(226,88,40),2,Qt::DashLine));
          curve->curve->setTitle(QString::fromStdString(name)+QStringLiteral("｜趋势：")+(mode==0?QStringLiteral("多项式 %1 阶").arg(order):QStringLiteral("滑动平均 %1 点").arg(window)));
        }
        parameterToggle->setChecked(false);
        emit plot->curveListChanged();
        plot->replot();
      });
    };
    fit();
  }

  void showXY()
  {
    QStringList fields;
    for (const auto& curve : plot->curveList())
    {
      const auto name = QString::fromStdString(curve.src_name);
      if (summaries.contains(name)) fields << name;
    }
    if (fields.size() < 2)
    {
      QMessageBox::information(this, QStringLiteral("XY 分析"), QStringLiteral("请先绘制至少两个有有效数据的测点。"));
      return;
    }

    struct Pair { double x, y, timeX, timeY; };
    struct State
    {
      PJ::PlotDataMapRef data;
      std::vector<Pair> paired;
      QwtPlotCurve fitCurve{QStringLiteral("XY 拟合")};
      quint64 revision = 0;
    };
    auto state = std::make_shared<State>();
    auto* panel = makeAnalysisPanel();
    connect(panel, &QObject::destroyed, [state] { state->fitCurve.detach(); });
    auto* root = new QVBoxLayout(panel);
    if ((!xyX.isEmpty() && !fields.contains(xyX)) ||
        (!xyY.isEmpty() && !fields.contains(xyY)))
    {
      status->setText(QStringLiteral("所选 X 或 Y 测点在当前时间段内没有有效数据。"));
      return;
    }
    const auto x = fields.contains(xyX) ? xyX : fields.front();
    const auto y = fields.contains(xyY) ? xyY : (x == fields.front() ? fields.at(1) : fields.front());
    const double seconds = xyTolerance.isEmpty() ? matchTolerance : xyTolerance.toDouble();
    auto* difference = new QPushButton(QStringLiteral("Y−X 差值统计"), panel);
    auto* fit = new QPushButton(QStringLiteral("XY 拟合"), panel);
    fit->setEnabled(false);
    auto* actions = new QHBoxLayout;
    actions->addWidget(difference);
    actions->addWidget(fit);
    root->addLayout(actions);
    auto* xyPlot = new PlotWidget(state->data, panel);
    connect(xyPlot, &QObject::destroyed, [state] {});
    root->addWidget(xyPlot, 1);
    auto* chart = xyPlot->findChild<QwtPlot*>();
    if (!chart) return;
    auto* status = new QLabel(panel);
    root->addWidget(status);
    auto* fitSummary = new QLabel(panel);
    fitSummary->setWordWrap(true);
    root->addWidget(fitSummary);
    state->fitCurve.setPen(QPen(QColor(214, 87, 39), 2.0));
    const auto redraw = [=] {
      const auto& xs = shown->numeric.at(x.toStdString());
      const auto& ys = shown->numeric.at(y.toStdString());
      ++state->revision;
      state->fitCurve.detach();
      fitSummary->clear();
      state->paired.clear();
      xyPlot->removeAllCurves();
      state->data.scatter_xy.clear();
      QMetaObject::invokeMethod(xyPlot, "setModeXY", Qt::DirectConnection, Q_ARG(bool, true));
      const auto name = QStringLiteral("%1 × %2").arg(x, y).toStdString();
      auto& scatter = state->data.addScatterXY(name)->second;
      for (size_t i = 0; i < xs.size(); ++i)
      {
        const auto a = xs.at(i);
        const auto end = BladeAnalysis::boundary(ys, a.x, true);
        auto nearest = end ? end - 1 : ys.size();
        if (end < ys.size() && (nearest == ys.size() || ys.at(end).x - a.x < a.x - ys.at(nearest).x))
          nearest = end;
        if (nearest == ys.size()) continue;
        const auto b = ys.at(nearest);
        if (std::abs(b.x - a.x) > seconds || !std::isfinite(a.y) || !std::isfinite(b.y)) continue;
        state->paired.push_back({a.y, b.y, a.x, b.x});
        scatter.pushBack({a.y, b.y});
      }
      if (!state->paired.empty())
        if (auto* curve = xyPlot->addCurve(name)) curve->curve->setStyle(QwtPlotCurve::Dots);
      chart->setAxisTitle(QwtPlot::xBottom, x);
      chart->setAxisTitle(QwtPlot::yLeft, y);
      emit xyPlot->curveListChanged();
      xyPlot->zoomOut(false);
      xyPlot->replot();
      difference->setEnabled(!state->paired.empty());
      fit->setEnabled(state->paired.size() >= 2);
      status->setText(QStringLiteral("配对 %1 / %2 个 X 采样点").arg(qulonglong(state->paired.size())).arg(qulonglong(xs.size())));
    };
    auto* options = new QWidget(panel);
    root->insertWidget(1, options);
    auto* optionsForm = new QFormLayout(options);
    options->hide();
    connect(fit, &QPushButton::clicked, options, [options] { options->setVisible(!options->isVisible()); });
    auto* model = new QComboBox(options);
    model->addItems({QStringLiteral("通用多项式"), QStringLiteral("风速–功率（M01）"),
                     QStringLiteral("风速–转速（M21）"), QStringLiteral("风速–桨距角（M22）"),
                     QStringLiteral("叶尖速比–功率系数（M03）")});
    optionsForm->addRow(QStringLiteral("模型"), model);
    auto* degreeBox = new QSpinBox(options);
    degreeBox->setRange(1, 6);
    optionsForm->addRow(QStringLiteral("多项式阶数"), degreeBox);
    const auto makeValue = [&](const QString& label, const QString& value) {
      auto* field = new QLineEdit(value, options);
      optionsForm->addRow(label, field);
      return field;
    };
    auto* cutIn = makeValue(QStringLiteral("切入风速（m/s）"), QStringLiteral("3"));
    auto* ratedWind = makeValue(QStringLiteral("额定风速（m/s）"), {});
    auto* cutOut = makeValue(QStringLiteral("切出风速（m/s）"), QStringLiteral("25"));
    auto* rated = makeValue(QStringLiteral("额定功率（kW）"), {});
    auto* rpm = makeValue(QStringLiteral("额定转速（r/min，与 Y 同轴）"), {});
    auto* width = makeValue(QStringLiteral("X 分箱宽度"), QStringLiteral("0.5"));
    auto* note = new QLabel(options);
    note->setWordWrap(true);
    optionsForm->addRow(note);
    const auto updateFields = [=] {
      const int mode = model->currentIndex();
      const auto show = [&](QWidget* field, bool visible) {
        field->setVisible(visible);
        if (auto* label = optionsForm->labelForField(field)) label->setVisible(visible);
      };
      show(degreeBox, mode == 0);
      for (auto* field : {cutIn, ratedWind, cutOut}) show(field, mode >= 1 && mode <= 3);
      show(rated, mode == 1);
      show(rpm, mode == 2);
      show(width, mode != 0);
      note->setText(mode == 4 ? QStringLiteral("X 请选择已计算的叶尖速比，Y 请选择已计算的功率系数。")
                              : mode == 0 ? QString() : QStringLiteral("X 请选择风速，Y 请选择与模型匹配的测点和单位。"));
    };
    connect(model, QOverload<int>::of(&QComboBox::currentIndexChanged), options, updateFields);
    updateFields();
    auto* calculate = new QPushButton(QStringLiteral("计算 XY 拟合"), options);
    optionsForm->addRow(calculate);
    connect(calculate, &QPushButton::clicked, options, [=] {
        if (state->paired.size() < 2) return;
        BladeFit::Params params;
        bool valid = true;
        const auto read = [&](QLineEdit* field) {
          bool ok = false;
          const double value = field->text().trimmed().toDouble(&ok);
          valid = valid && ok && std::isfinite(value);
          return value;
        };
        const int mode = model->currentIndex();
        if (mode != 0) params.width = read(width);
        if (mode >= 1 && mode <= 3)
        {
          params.cutIn = read(cutIn);
          params.ratedWind = read(ratedWind);
          params.cutOut = read(cutOut);
          valid = valid && params.cutIn >= 0.0 && params.cutIn < params.ratedWind && params.ratedWind < params.cutOut;
        }
        if (mode == 1) { params.rated = read(rated); valid = valid && params.rated > 0.0; }
        if (mode == 2) { params.rpm = read(rpm); valid = valid && params.rpm > 0.0; }
        if (!valid || params.width <= 0.0)
        {
          fitSummary->setText(QStringLiteral("请核对额定参数、风速区间和分箱宽度。"));
          return;
        }
      const int degree = degreeBox->value();
      const int kinds[]{-1, BladeFit::M01, BladeFit::M21, BladeFit::M22, BladeFit::M03};
      const int kind = kinds[model->currentIndex()];
      const auto modelName = model->currentText();
      auto rows = std::make_shared<BladeFit::Points>();
      rows->reserve(state->paired.size());
      for (const auto& point : state->paired) rows->push_back({point.x, point.y});
      const auto ticket = ++state->revision;
      calculate->setEnabled(false);
      state->fitCurve.detach();
      chart->replot();
      fitSummary->setText(QStringLiteral("正在拟合…"));
      BladeTrend::background<BladeTrend::Trend>(panel, [rows, degree, kind, params, modelName] {
        if (kind >= 0)
        {
          auto result = BladeFit::fitPairedTurbineCurve(kind, *rows, params);
          return BladeTrend::Trend{std::move(result.fit),
              QStringLiteral("%1 · 有效分箱 %2").arg(modelName).arg(qulonglong(result.bins.size()))};
        }
        std::sort(rows->begin(), rows->end(), [](const auto& a, const auto& b) { return a.x < b.x; });
        return BladeTrend::calculate(*rows, 0, degree, 1, rows->front().x, rows->back().x, QStringLiteral("X"));
      }, [=](BladeTrend::Trend result, const QString& error) {
        calculate->setEnabled(true);
        if (ticket != state->revision) return;
        fitSummary->setText(error.isEmpty() ? result.summary : error);
        if (!error.isEmpty() || result.points.empty()) return;
        QVector<QPointF> samples;
        for (const auto& point : result.points) samples.append(QPointF(point.x, point.y));
        state->fitCurve.setSamples(samples);
        state->fitCurve.attach(chart);
        chart->replot();
      });
    });
    connect(difference, &QPushButton::clicked, panel, [=] {
      std::vector<double> values;
      values.reserve(state->paired.size());
      for (const auto& point : state->paired) values.push_back(point.y - point.x);
      showDifference(panel, std::move(values));
    });
    auto* picker = new QwtPlotPicker(chart->canvas());
    picker->setStateMachine(new QwtPickerTrackerMachine);
    picker->setTrackerMode(QwtPicker::AlwaysOn);
    chart->canvas()->setMouseTracking(true);
    connect(picker, QOverload<const QPointF&>::of(&QwtPlotPicker::moved), panel, [=](const QPointF& cursor) {
      const auto px = chart->transform(QwtPlot::xBottom, cursor.x());
      const auto py = chart->transform(QwtPlot::yLeft, cursor.y());
      const Pair* best = nullptr;
      double distance = 64.0;
      for (const auto& point : state->paired)
      {
        const double dx = chart->transform(QwtPlot::xBottom, point.x) - px;
        const double dy = chart->transform(QwtPlot::yLeft, point.y) - py;
        const double squared = dx * dx + dy * dy;
        if (squared < distance) { best = &point; distance = squared; }
      }
      if (!best) { QToolTip::hideText(); return; }
      const auto time = [](double value) {
        const bool calendar = BladeTimeDisplay::isCalendarTime(value);
        return BladeTimeDisplay::format(value, true, calendar, calendar, 3, Qt::UTC);
      };
      QToolTip::showText(QCursor::pos(), QStringLiteral("X %1　Y %2\nX 时间 %3\nY 时间 %4")
          .arg(BladeAnalysis::number(best->x), BladeAnalysis::number(best->y), time(best->timeX), time(best->timeY)),
          chart->canvas());
    });
    redraw();
  }

  static void showDifference(QWidget* parent, std::vector<double> values)
  {
    values.erase(std::remove_if(values.begin(), values.end(),
                               [](double value) { return !std::isfinite(value); }), values.end());
    BladeAnalysis::Statistics summary;
    for (const auto value : values) summary.add(value);
    std::sort(values.begin(), values.end());
    const auto percentile = [&](double p) {
      if (values.empty()) return QStringLiteral("—");
      const double rank = p * (values.size() - 1);
      const auto index = size_t(rank);
      const double fraction = rank - index;
      return BladeAnalysis::number(values[index] * (1 - fraction) +
          values[std::min(index + 1, values.size() - 1)] * fraction);
    };
    const auto result = QStringLiteral("有效点 %1\n最小 %2　最大 %3　均值 %4\n标准差 %5　极差 %6\nP05 %7　P25 %8　中位数 %9　P75 %10　P95 %11")
        .arg(qulonglong(summary.count))
        .arg(BladeAnalysis::number(summary.low), BladeAnalysis::number(summary.high),
             BladeAnalysis::number(summary.mean))
        .arg(BladeAnalysis::number(std::sqrt(std::max(0.0, summary.m2 / double(std::max<size_t>(1, summary.count))))),
             BladeAnalysis::number(summary.high - summary.low))
        .arg(percentile(0.05), percentile(0.25), percentile(0.5), percentile(0.75), percentile(0.95));
    auto* layout = parent->layout();
    if (auto* previous = parent->findChild<QWidget*>(QStringLiteral("waveformDifference")))
      delete previous;
    auto* output = new QWidget(parent);
    output->setObjectName(QStringLiteral("waveformDifference"));
    auto* content = new QHBoxLayout(output);
    auto* summaryLabel = new QLabel(QStringLiteral("差值要求两测点单位一致。\n") + result, output);
    summaryLabel->setWordWrap(true);
    content->addWidget(summaryLabel, 1);
    auto* copy = new QPushButton(QStringLiteral("复制统计"), output);
    auto* exportText = new QPushButton(QStringLiteral("导出文本"), output);
    auto* close = new QPushButton(QStringLiteral("收起"), output);
    for (auto* button : {copy, exportText, close}) content->addWidget(button);
    layout->addWidget(output);
    connect(close, &QPushButton::clicked, output, &QObject::deleteLater);
    connect(copy, &QPushButton::clicked, output, [result] { QApplication::clipboard()->setText(result); });
    connect(exportText, &QPushButton::clicked, output, [output, result] {
      const auto path = QFileDialog::getSaveFileName(output, QStringLiteral("导出差值统计"),
          QStringLiteral("录波差值统计.txt"), QStringLiteral("文本文件 (*.txt)"));
      if (path.isEmpty()) return;
      const auto bytes = result.toUtf8() + "\n";
      QSaveFile file(path);
      if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        QMessageBox::warning(output, QStringLiteral("导出差值统计"), file.errorString());
    });
  }

  void showComparison()
  {
    QStringList fields;
    for (const auto& curve : plot->curveList())
    {
      const auto name = QString::fromStdString(curve.src_name);
      if (summaries.contains(name)) fields << name;
    }
    if (fields.size() < 2)
    {
      QMessageBox::information(this, QStringLiteral("双轴对照"), QStringLiteral("请先绘制至少两个有有效数据的测点。"));
      return;
    }

    auto syncing = std::make_shared<bool>(false);
    auto* panel = makeAnalysisPanel();
    auto* root = new QVBoxLayout(panel);
    const bool stacked = comparisonLayout == 1;
    auto* difference = new QPushButton(QStringLiteral("差值统计"), panel);

    auto* differencePanel = new QWidget(panel);
    auto* differenceRow = new QHBoxLayout(differencePanel);
    auto* x = new QComboBox(differencePanel);
    auto* y = new QComboBox(differencePanel);
    auto* tolerance = new QLineEdit(BladeAnalysis::number(matchTolerance), differencePanel);
    auto* calculate = new QPushButton(QStringLiteral("计算 Y−X"), differencePanel);
    differenceRow->addWidget(new QLabel(QStringLiteral("左轴"), differencePanel));
    differenceRow->addWidget(x);
    differenceRow->addWidget(new QLabel(QStringLiteral("右轴"), differencePanel));
    differenceRow->addWidget(y);
    differenceRow->addWidget(new QLabel(QStringLiteral("时间容差（秒）"), differencePanel));
    differenceRow->addWidget(tolerance);
    differenceRow->addWidget(calculate);
    root->addWidget(differencePanel);
    differencePanel->hide();
    connect(difference, &QPushButton::clicked, panel, [=] { differencePanel->setVisible(!differencePanel->isVisible()); });

    auto* upper = new PlotWidget(*shown, panel);
    auto* lower = new PlotWidget(*shown, panel);
    connect(upper, &QObject::destroyed, [hold = shown] {});
    connect(lower, &QObject::destroyed, [hold = shown] {});
    root->addWidget(upper, 1);
    root->addWidget(lower, 1);root->addWidget(difference,0,Qt::AlignLeft);
    upper->on_changeTimeOffset(0);lower->on_changeTimeOffset(0);
    auto* upperPlot = upper->findChild<QwtPlot*>();
    auto* lowerPlot = lower->findChild<QwtPlot*>();
    if (!upperPlot || !lowerPlot) return;
    for (auto* chart : {upperPlot, lowerPlot})
    {
      chart->setAxisScaleDraw(QwtPlot::xBottom, new UtcScaleDraw);
      chart->setAxisTitle(QwtPlot::xBottom, canvas->axisTitle(QwtPlot::xBottom));
    }

    const auto link = [&](QwtPlot* from, QwtPlot* to) {
      connect(from->axisWidget(QwtPlot::xBottom), &QwtScaleWidget::scaleDivChanged, panel,
              [=] {
        if (*syncing || !stacked) return;
        const auto scale = from->axisScaleDiv(QwtPlot::xBottom);
        if (scale == to->axisScaleDiv(QwtPlot::xBottom)) return;
        *syncing = true;
        to->setAxisScaleDiv(QwtPlot::xBottom, scale);
        to->replot();
        *syncing = false;
      });
    };
    link(upperPlot, lowerPlot);
    link(lowerPlot, upperPlot);

    const auto initialScale = canvas->axisScaleDiv(QwtPlot::xBottom);
    const auto redraw = [=] {
      QStringList left, right;
      for (int i = 0; i < fields.size(); ++i)
      {
        const auto& field = fields.at(i);
        const int axis = comparisonAxes.value(field, i == 0 ? 1 : i == 1 ? 2 : 0);
        if (axis == 1) left << field;
        if (axis == 2) right << field;
      }
      x->clear(); x->addItems(left);
      y->clear(); y->addItems(right);
      calculate->setEnabled(!left.isEmpty() && !right.isEmpty());
      if (left.isEmpty() || right.isEmpty())
        status->setText(QStringLiteral("请给左右轴各分配至少一个有有效数据的测点。"));
      *syncing = true;
      lower->setVisible(stacked);
      upper->removeAllCurves();
      lower->removeAllCurves();
      upperPlot->enableAxis(QwtPlot::yRight, !stacked);
      upperPlot->setAxisTitle(QwtPlot::yLeft, left.size() == 1 ? left.front() : QStringLiteral("左轴"));
      upperPlot->setAxisTitle(QwtPlot::yRight, right.size() == 1 ? right.front() : QStringLiteral("右轴"));
      lowerPlot->setAxisTitle(QwtPlot::yLeft, right.size() == 1 ? right.front() : QStringLiteral("下图"));
      const auto curveStyle = style->currentIndex() ? QwtPlotCurve::Dots : QwtPlotCurve::Lines;
      for (int i = 0; i < fields.size(); ++i)
      {
        const auto& name = fields.at(i);
        const bool onRight = right.contains(name);
        if (!onRight && !left.contains(name)) continue;
        auto* target = onRight && stacked ? lower : upper;
        if (auto* item = target->addCurve(name.toStdString(), QColor::fromHsv((210 + i * 137) % 360, 185, 190)))
        {
          item->curve->setStyle(curveStyle);
          if (onRight && !stacked) item->curve->setYAxis(QwtPlot::yRight);
        }
      }
      for (auto* chart : {upperPlot, lowerPlot})
      {
        chart->setAxisAutoScale(QwtPlot::yLeft);
        chart->setAxisAutoScale(QwtPlot::yRight);
        chart->setAxisScaleDiv(QwtPlot::xBottom, initialScale);
        chart->replot();
      }
      *syncing = false;
    };
    connect(calculate, &QPushButton::clicked, panel, [=] {
      bool valid = false;
      const double seconds = tolerance->text().toDouble(&valid);
      if (!valid || !std::isfinite(seconds) || seconds < 0.0)
      {
        status->setText(QStringLiteral("请输入有效的非负时间偏差。"));
        return;
      }
      const auto range = upperPlot->axisScaleDiv(QwtPlot::xBottom);
      const auto& xs = shown->numeric.at(x->currentText().toStdString());
      const auto& ys = shown->numeric.at(y->currentText().toStdString());
      const auto first = BladeAnalysis::boundary(xs, range.lowerBound());
      const auto last = BladeAnalysis::boundary(xs, range.upperBound(), true);
      std::vector<double> values;
      values.reserve(last - first);
      for (size_t i = first; i < last; ++i)
      {
        const auto sample = xs.at(i);
        double value = 0.0;
        if (BladeAnalysis::match(ys, sample.x, seconds, false, value)) values.push_back(value - sample.y);
      }
      showDifference(panel, std::move(values));
    });
    redraw();
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
    auto* exportCsv = new QPushButton(QStringLiteral("导出 CSV"), &dialog);
    layout->addWidget(exportCsv);
    connect(exportCsv, &QPushButton::clicked, &dialog, [&dialog, table] {
      const auto path = QFileDialog::getSaveFileName(&dialog, QStringLiteral("导出采样统计"),
          QStringLiteral("录波采样统计.csv"), QStringLiteral("CSV 文件 (*.csv)"));
      if (path.isEmpty()) return;
      QStringList rows;
      for (int row = -1; row < table->rowCount(); ++row)
      {
        QStringList cells;
        for (int column = 0; column < table->columnCount(); ++column)
        {
          const auto* cell = row < 0 ? table->horizontalHeaderItem(column) : table->item(row, column);
          cells << BladeAnalysis::csvCell(cell->text());
        }
        rows << cells.join(QLatin1Char(','));
      }
      const QByteArray bytes = QByteArray::fromHex("efbbbf") + rows.join(QStringLiteral("\r\n")).toUtf8() + "\r\n";
      QSaveFile file(path);
      if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        QMessageBox::warning(&dialog, QStringLiteral("导出采样统计"), file.errorString());
    });
    dialog.resize(1050, 360);
    dialog.exec();
  }

  bool draw(const QStringList& fields)
  {
    if ((analysisMode == 1 || analysisMode == 2) && fields.size() < 2)
    {
      status->setText(QStringLiteral("请在左栏至少勾选两个测点。"));
      return false;
    }
    if (fields.isEmpty() || fields.size() > 16)
    {
      status->setText(QStringLiteral("请选择 1 至 16 个测点。"));
      return false;
    }
    if (analysisMode == 2 && !xyTolerance.isEmpty())
    {
      bool valid = false;
      const double seconds = xyTolerance.toDouble(&valid);
      if (!valid || !std::isfinite(seconds) || seconds < 0.0)
      {
        status->setText(QStringLiteral("请输入有效的非负时间偏差。"));
        return false;
      }
    }
    const auto signature=QJsonDocument(QJsonObject{{"state",workspaceState()},{"fields",QJsonArray::fromStringList(fields)},{"mode",analysisMode}}).toJson(QJsonDocument::Compact);
    const bool keepView=signature==lastDrawSignature && !shown->numeric.empty();
    const auto previousView=plot->currentBoundingRect();
    if(analysisMode==3&&trendRange&&canvas){const auto s=canvas->axisScaleDiv(QwtPlot::xBottom);fitVisibleLow=s.lowerBound()+plot->timeOffset();fitVisibleHigh=s.upperBound()+plot->timeOffset();}
    const double infinity = std::numeric_limits<double>::infinity();
    const double lo = startEnabled->isChecked() ? start->dateTime().toMSecsSinceEpoch()/1000. : -infinity;
    const double hi = endEnabled->isChecked() ? end->dateTime().toMSecsSinceEpoch()/1000. : infinity;
    if (lo > hi) { status->setText(QStringLiteral("开始时间不能晚于结束时间。")); return false; }
    resetAnalysisPanel();
    if(auto* hint=findChild<QLabel*>("bladeWaveformEmpty"))hint->hide();plot->show();
    plot->removeAllCurves();
    shown->numeric.clear();
    QMetaObject::invokeMethod(plot,"setModeXY",Qt::DirectConnection,Q_ARG(bool,false));plot->on_changeTimeOffset(0);
    auto aliases = qApp->property("bladeScopeTagAliases").toMap();
    const auto recordingAliases = qApp->property("bladeScopeRecordingAliases").toMap();
    for (auto it = recordingAliases.cbegin(); it != recordingAliases.cend(); ++it)
      aliases.insert(it.key(), it.value());
    summaries.clear();
    size_t count = 0;
    double firstPlottedTime = std::numeric_limits<double>::infinity(),lastPlottedTime=-firstPlottedTime;
    size_t invalidTimes=0;
    for (const auto& name : fields)
    {
      const auto key = name.toStdString();
      const auto entry = source.numeric.find(key);
      if (entry == source.numeric.end()) continue;
      const auto& series = entry->second;
      const auto stop = BladeAnalysis::boundary(series, hi, true);
      const auto begin = BladeAnalysis::boundary(series, lo);
      if (begin >= stop) continue;

      auto& summary = summaries[name];
      for (size_t i = begin; i < stop; ++i)
        if (std::isfinite(series.at(i).y)) ++summary.source.count;
      std::vector<std::pair<double, double>> retained;
      forEachDisplayIndex(begin, stop, view->currentIndex() == 1,
                         [&](size_t i) { return series.at(i).y; }, [&](size_t i) {
        const auto sample = series.at(i);
        if (!std::isfinite(sample.x)){++invalidTimes;return;}
        if (!std::isfinite(sample.y)) return;
        ++summary.sampled;
        for (const auto& condition : conditions)
        {
          if (!filterEnabled) break;
          if (!condition.enabled) continue;
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
      firstPlottedTime=std::min(firstPlottedTime,retained.front().first);lastPlottedTime=std::max(lastPlottedTime,retained.back().first);
      count += retained.size();
      if (auto* curve = plot->addCurve(key))
      {
        const auto alias = aliases.value(name).toString();
        curve->curve->setTitle((alias.isEmpty()?name:alias)+QStringLiteral("｜原始"));
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
    if(canvas&&count){
      double left=std::isfinite(lo)?lo:firstPlottedTime,right=std::isfinite(hi)?hi:lastPlottedTime;
      if(left==right){left-=.5;right+=.5;}
      auto rect=plot->currentBoundingRect();rect.setLeft(left);rect.setRight(right);
      if(keepView)rect=previousView;
      plot->setZoomRectangle(rect,false);plot->replot();
    }
    lastDrawSignature=signature;
    applyStyle();
    if(auto* hint=findChild<QLabel*>("bladeWaveformEmpty")){
      hint->setText(QStringLiteral("筛选后无有效样本，请检查测点、时间窗口与条件筛选"));hint->setVisible(count==0);
    }
    plot->setVisible(count>0);
    status->setText(QStringLiteral("%1 条曲线，%2 个取样点")
        .arg(qulonglong(plot->curveList().size())).arg(qulonglong(count)));
    QToolTip::hideText();
    plottedFields = fields;
    if(invalidTimes)status->setText(status->text()+QStringLiteral("；无效时间戳 %1").arg(qulonglong(invalidTimes)));
    updateTimeControls();
    if (count && analysisMode == 3) showTrend();
    if (analysisMode == 1 || analysisMode == 2)
    {
      if (plot->curveList().size() < 2)
        status->setText(QStringLiteral("请勾选至少两个有有效数据的测点。"));
      else if (analysisMode == 1) showComparison();
      else showXY();
    }
    return count != 0;
  }

  void applyStyle()
  {
    for (const auto& layer : plot->curveList())
      if (summaries.contains(QString::fromStdString(layer.src_name)))
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
  QVBoxLayout* workspace = nullptr;
  QWidget* analysisPanel = nullptr;
  QWidget* parameters = nullptr;
  QFormLayout* parameterForm = nullptr;
  bool curveMode=false;
  QToolButton* parameterToggle=nullptr;
  QLabel* parameterSummary=nullptr;
  QByteArray lastDrawSignature;
  double fitVisibleLow=std::numeric_limits<double>::quiet_NaN(),fitVisibleHigh=fitVisibleLow;
  int analysisMode = 0;
  int comparisonLayout = 0;
  QMap<QString, int> comparisonAxes;
  QString xyX, xyY, xyTolerance, trendField;
  int trendModel = 0, trendDegree = 1, trendWindow = 11, trendRange = 0;
  QStringList plottedFields;
  QButtonGroup* modes = nullptr;
  QComboBox* view = nullptr;
  QComboBox *groups = nullptr, *style = nullptr;
  Tags* tags = nullptr;
  QLineEdit* search = nullptr;
  QCheckBox *startEnabled = nullptr, *endEnabled = nullptr;
  QComboBox *labelMode = nullptr, *system = nullptr;
  QVector<Condition> conditions;
  bool filterEnabled = true;
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

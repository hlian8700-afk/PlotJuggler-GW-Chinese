#pragma once
#include "bladescope_analysis.h"
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
    auto* analysisLayout = new QVBoxLayout(analysisControls);
    analysisLayout->setContentsMargins(0, 0, 0, 0);
    auto* actions = new QHBoxLayout;
    analysisLayout->addLayout(actions);
    auto* filter = new QPushButton(QStringLiteral("条件筛选"), analysisControls);
    auto* statistics = new QPushButton(QStringLiteral("采样统计"), analysisControls);
    actions->addWidget(filter);
    actions->addWidget(statistics);
    auto* comparison = new QPushButton(QStringLiteral("双轴对照"), analysisControls);
    actions->addWidget(comparison);
    auto* xy = new QPushButton(QStringLiteral("XY 分析"), analysisControls);
    actions->addWidget(xy);
    connect(xy, &QPushButton::clicked, this, [this] { showXY(); });
    connect(comparison, &QPushButton::clicked, this, [this] { showComparison(); });
    auto* trend = new QPushButton(QStringLiteral("时序趋势拟合"), analysisControls);
    analysisLayout->addWidget(trend);
    side->addWidget(analysisControls);
    connect(trend, &QPushButton::clicked, this, [this] { showTrend(); });
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
    bool enabled = true;
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
    if (!plottedFields.isEmpty()) draw(plottedFields);
  }

  void showTrend()
  {
    QStringList fields;
    for (const auto& curve : plot->curveList())
    {
      const auto name = QString::fromStdString(curve.src_name);
      if (summaries.contains(name)) fields << name;
    }
    if (!canvas || fields.isEmpty())
    {
      QMessageBox::information(this, QStringLiteral("时序趋势拟合"), QStringLiteral("请先绘制有效的录波测点。"));
      return;
    }
    bool confirmed = false;
    const auto field = QInputDialog::getItem(this, QStringLiteral("时序趋势拟合"),
        QStringLiteral("选择拟合测点"), fields, 0, false, &confirmed);
    if (confirmed) BladeTrend::open(plot, canvas, field.toStdString());
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

    PJ::PlotDataMapRef xyData;
    struct Pair { double x, y, timeX, timeY; };
    std::vector<Pair> paired;
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("录波 XY 分析"));
    auto* root = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    root->addLayout(form);
    auto* x = new QComboBox(&dialog);
    auto* y = new QComboBox(&dialog);
    x->addItems(fields);
    y->addItems(fields);
    y->setCurrentIndex(1);
    auto* tolerance = new QLineEdit(BladeAnalysis::number(matchTolerance), &dialog);
    form->addRow(QStringLiteral("X 测点"), x);
    form->addRow(QStringLiteral("Y 测点"), y);
    form->addRow(QStringLiteral("最大时间偏差（秒）"), tolerance);
    auto* apply = new QPushButton(QStringLiteral("绘制 XY"), &dialog);
    auto* difference = new QPushButton(QStringLiteral("Y−X 差值统计"), &dialog);
    auto* fit = new QPushButton(QStringLiteral("XY 拟合"), &dialog);
    fit->setEnabled(false);
    auto* actions = new QHBoxLayout;
    actions->addWidget(apply);
    actions->addWidget(difference);
    actions->addWidget(fit);
    root->addLayout(actions);
    auto* xyPlot = new PlotWidget(xyData, &dialog);
    root->addWidget(xyPlot, 1);
    auto* chart = xyPlot->findChild<QwtPlot*>();
    if (!chart) return;
    auto* status = new QLabel(&dialog);
    root->addWidget(status);
    auto* fitSummary = new QLabel(&dialog);
    fitSummary->setWordWrap(true);
    root->addWidget(fitSummary);
    QwtPlotCurve fitCurve(QStringLiteral("XY 拟合"));
    fitCurve.setPen(QPen(QColor(214, 87, 39), 2.0));
    quint64 revision = 0;
    const auto redraw = [&] {
      bool valid = false;
      const double seconds = tolerance->text().toDouble(&valid);
      if (!valid || !std::isfinite(seconds) || seconds < 0.0)
      {
        QMessageBox::warning(&dialog, QStringLiteral("XY 分析"), QStringLiteral("请输入有效的非负时间偏差。"));
        return;
      }
      const auto& xs = shown->numeric.at(x->currentText().toStdString());
      const auto& ys = shown->numeric.at(y->currentText().toStdString());
      ++revision;
      fitCurve.detach();
      fitSummary->clear();
      paired.clear();
      xyPlot->removeAllCurves();
      xyData.scatter_xy.clear();
      QMetaObject::invokeMethod(xyPlot, "setModeXY", Qt::DirectConnection, Q_ARG(bool, true));
      const auto name = QStringLiteral("%1 × %2").arg(x->currentText(), y->currentText()).toStdString();
      auto& scatter = xyData.addScatterXY(name)->second;
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
        paired.push_back({a.y, b.y, a.x, b.x});
        scatter.pushBack({a.y, b.y});
      }
      if (!paired.empty())
        if (auto* curve = xyPlot->addCurve(name)) curve->curve->setStyle(QwtPlotCurve::Dots);
      chart->setAxisTitle(QwtPlot::xBottom, x->currentText());
      chart->setAxisTitle(QwtPlot::yLeft, y->currentText());
      emit xyPlot->curveListChanged();
      xyPlot->zoomOut(false);
      xyPlot->replot();
      difference->setEnabled(!paired.empty());
      fit->setEnabled(paired.size() >= 2);
      status->setText(QStringLiteral("配对 %1 / %2 个 X 采样点").arg(qulonglong(paired.size())).arg(qulonglong(xs.size())));
    };
    connect(apply, &QPushButton::clicked, &dialog, redraw);
    connect(fit, &QPushButton::clicked, &dialog, [&] {
      QDialog options(&dialog);
      options.setWindowTitle(QStringLiteral("XY 拟合模型"));
      QFormLayout form(&options);
      auto* model = new QComboBox(&options);
      model->addItems({QStringLiteral("通用多项式"), QStringLiteral("风速–功率（M01）"),
                       QStringLiteral("风速–转速（M21）"), QStringLiteral("风速–桨距角（M22）"),
                       QStringLiteral("叶尖速比–功率系数（M03）")});
      form.addRow(QStringLiteral("模型"), model);
      auto* degreeBox = new QSpinBox(&options);
      degreeBox->setRange(1, 6);
      form.addRow(QStringLiteral("多项式阶数"), degreeBox);
      const auto makeValue = [&](const QString& label, const QString& value) {
        auto* field = new QLineEdit(value, &options);
        form.addRow(label, field);
        return field;
      };
      auto* cutIn = makeValue(QStringLiteral("切入风速（m/s）"), QStringLiteral("3"));
      auto* ratedWind = makeValue(QStringLiteral("额定风速（m/s）"), {});
      auto* cutOut = makeValue(QStringLiteral("切出风速（m/s）"), QStringLiteral("25"));
      auto* rated = makeValue(QStringLiteral("额定功率（kW）"), {});
      auto* rpm = makeValue(QStringLiteral("额定转速（r/min，与 Y 同轴）"), {});
      auto* width = makeValue(QStringLiteral("X 分箱宽度"), QStringLiteral("0.5"));
      auto* note = new QLabel(&options);
      note->setWordWrap(true);
      form.addRow(note);
      const auto updateFields = [&] {
        const int mode = model->currentIndex();
        const auto show = [&](QWidget* field, bool visible) {
          field->setVisible(visible);
          if (auto* label = form.labelForField(field)) label->setVisible(visible);
        };
        show(degreeBox, mode == 0);
        for (auto* field : {cutIn, ratedWind, cutOut}) show(field, mode >= 1 && mode <= 3);
        show(rated, mode == 1);
        show(rpm, mode == 2);
        show(width, mode != 0);
        note->setText(mode == 4 ? QStringLiteral("X 请选择已计算的叶尖速比，Y 请选择已计算的功率系数。")
                                : mode == 0 ? QString() : QStringLiteral("X 请选择风速，Y 请选择与模型匹配的测点和单位。"));
      };
      connect(model, QOverload<int>::of(&QComboBox::currentIndexChanged), &options, updateFields);
      updateFields();
      auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &options);
      form.addRow(buttons);
      connect(buttons, &QDialogButtonBox::rejected, &options, &QDialog::reject);
      BladeFit::Params params;
      connect(buttons, &QDialogButtonBox::accepted, &options, [&] {
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
        if (valid && params.width > 0.0) options.accept();
        else QMessageBox::warning(&options, options.windowTitle(), QStringLiteral("请核对额定参数、风速区间和分箱宽度。"));
      });
      if (options.exec() != QDialog::Accepted) return;
      const int degree = degreeBox->value();
      const int kinds[]{-1, BladeFit::M01, BladeFit::M21, BladeFit::M22, BladeFit::M03};
      const int kind = kinds[model->currentIndex()];
      const auto modelName = model->currentText();
      auto rows = std::make_shared<BladeFit::Points>();
      rows->reserve(paired.size());
      for (const auto& point : paired) rows->push_back({point.x, point.y});
      const auto ticket = ++revision;
      fit->setEnabled(false);
      fitCurve.detach();
      chart->replot();
      fitSummary->setText(QStringLiteral("正在拟合…"));
      BladeTrend::background<BladeTrend::Trend>(&dialog, [rows, degree, kind, params, modelName] {
        if (kind >= 0)
        {
          auto result = BladeFit::fitPairedTurbineCurve(kind, *rows, params);
          return BladeTrend::Trend{std::move(result.fit),
              QStringLiteral("%1 · 有效分箱 %2").arg(modelName).arg(qulonglong(result.bins.size()))};
        }
        std::sort(rows->begin(), rows->end(), [](const auto& a, const auto& b) { return a.x < b.x; });
        return BladeTrend::calculate(*rows, 0, degree, 1, rows->front().x, rows->back().x, QStringLiteral("X"));
      }, [&, ticket](BladeTrend::Trend result, const QString& error) {
        if (ticket != revision) return;
        fit->setEnabled(true);
        fitSummary->setText(error.isEmpty() ? result.summary : error);
        if (!error.isEmpty() || result.points.empty()) return;
        QVector<QPointF> samples;
        for (const auto& point : result.points) samples.append(QPointF(point.x, point.y));
        fitCurve.setSamples(samples);
        fitCurve.attach(chart);
        chart->replot();
      });
    });
    connect(difference, &QPushButton::clicked, &dialog, [&] {
      std::vector<double> values;
      values.reserve(paired.size());
      for (const auto& point : paired) values.push_back(point.y - point.x);
      showDifference(&dialog, std::move(values));
    });
    auto* picker = new QwtPlotPicker(chart->canvas());
    picker->setStateMachine(new QwtPickerTrackerMachine);
    picker->setTrackerMode(QwtPicker::AlwaysOn);
    chart->canvas()->setMouseTracking(true);
    connect(picker, QOverload<const QPointF&>::of(&QwtPlotPicker::moved), &dialog, [&](const QPointF& cursor) {
      const auto px = chart->transform(QwtPlot::xBottom, cursor.x());
      const auto py = chart->transform(QwtPlot::yLeft, cursor.y());
      const Pair* best = nullptr;
      double distance = 64.0;
      for (const auto& point : paired)
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
    dialog.resize(950, 650);
    dialog.exec();
  }

  static void showDifference(QWidget* parent, std::vector<double> values)
  {
    values.erase(std::remove_if(values.begin(), values.end(),
                               [](double value) { return !std::isfinite(value); }), values.end());
    if (values.empty())
    {
      QMessageBox::information(parent, QStringLiteral("差值统计"), QStringLiteral("当前范围内没有可用的配对点。"));
      return;
    }
    BladeAnalysis::Statistics summary;
    for (const auto value : values) summary.add(value);
    std::sort(values.begin(), values.end());
    const auto percentile = [&](double p) {
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
        .arg(BladeAnalysis::number(std::sqrt(std::max(0.0, summary.m2 / double(summary.count)))),
             BladeAnalysis::number(summary.high - summary.low))
        .arg(percentile(0.05), percentile(0.25), percentile(0.5), percentile(0.75), percentile(0.95));
    QMessageBox dialog(QMessageBox::Information, QStringLiteral("Y−X 差值统计"),
                       QStringLiteral("差值要求两测点单位一致。\n") + result, QMessageBox::Close, parent);
    auto* copy = dialog.addButton(QStringLiteral("复制统计"), QMessageBox::ActionRole);
    connect(copy, &QPushButton::clicked, &dialog, [result] { QApplication::clipboard()->setText(result); });
    auto* exportText = dialog.addButton(QStringLiteral("导出文本"), QMessageBox::ActionRole);
    connect(exportText, &QPushButton::clicked, &dialog, [&dialog, result] {
      const auto path = QFileDialog::getSaveFileName(&dialog, QStringLiteral("导出差值统计"),
          QStringLiteral("录波差值统计.txt"), QStringLiteral("文本文件 (*.txt)"));
      if (path.isEmpty()) return;
      const auto bytes = result.toUtf8() + "\n";
      QSaveFile file(path);
      if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        QMessageBox::warning(&dialog, QStringLiteral("导出差值统计"), file.errorString());
    });
    dialog.exec();
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

    bool syncing = false;
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("录波双轴对照"));
    auto* root = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    root->addLayout(form);
    auto* layoutMode = new QComboBox(&dialog);
    layoutMode->addItems({QStringLiteral("单图双 Y 轴"), QStringLiteral("上下联动图")});
    QStringList left{fields.at(0)}, right{fields.at(1)};
    form->addRow(QStringLiteral("布局"), layoutMode);
    auto* configure = new QPushButton(QStringLiteral("配置左右轴测点"), &dialog);
    auto* difference = new QPushButton(QStringLiteral("差值统计"), &dialog);
    auto* actions = new QHBoxLayout;
    actions->addWidget(configure);
    actions->addWidget(difference);
    root->addLayout(actions);

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
      syncing = false;
    };
    connect(layoutMode, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog, redraw);
    connect(configure, &QPushButton::clicked, &dialog, [&] {
      QDialog settings(&dialog);
      settings.setWindowTitle(QStringLiteral("配置左右轴测点"));
      auto* form = new QFormLayout(&settings);
      QVector<QComboBox*> assignments;
      for (const auto& name : fields)
      {
        auto* axis = new QComboBox(&settings);
        axis->addItems({QStringLiteral("不显示"), QStringLiteral("左轴 / 上图"), QStringLiteral("右轴 / 下图")});
        axis->setCurrentIndex(left.contains(name) ? 1 : right.contains(name) ? 2 : 0);
        form->addRow(name, axis);
        assignments.push_back(axis);
      }
      auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &settings);
      form->addRow(buttons);
      connect(buttons, &QDialogButtonBox::rejected, &settings, &QDialog::reject);
      connect(buttons, &QDialogButtonBox::accepted, &settings, [&] {
        QStringList newLeft, newRight;
        for (int i = 0; i < assignments.size(); ++i)
        {
          const auto axis = assignments[i]->currentIndex();
          if (axis == 1) newLeft << fields.at(i);
          if (axis == 2) newRight << fields.at(i);
        }
        if (newLeft.isEmpty() || newRight.isEmpty())
        {
          QMessageBox::warning(&settings, settings.windowTitle(), QStringLiteral("每侧至少选择一个测点。"));
          return;
        }
        left = newLeft;
        right = newRight;
        settings.accept();
      });
      if (settings.exec() == QDialog::Accepted) redraw();
    });
    connect(difference, &QPushButton::clicked, &dialog, [&] {
      QDialog selection(&dialog);
      selection.setWindowTitle(QStringLiteral("当前时间窗口差值"));
      auto* form = new QFormLayout(&selection);
      auto* x = new QComboBox(&selection);
      auto* y = new QComboBox(&selection);
      x->addItems(left);
      y->addItems(right);
      auto* tolerance = new QLineEdit(BladeAnalysis::number(matchTolerance), &selection);
      form->addRow(QStringLiteral("X（左轴 / 上图）"), x);
      form->addRow(QStringLiteral("Y（右轴 / 下图）"), y);
      form->addRow(QStringLiteral("最大时间偏差（秒）"), tolerance);
      auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &selection);
      form->addRow(buttons);
      connect(buttons, &QDialogButtonBox::rejected, &selection, &QDialog::reject);
      connect(buttons, &QDialogButtonBox::accepted, &selection, [&] {
        bool valid = false;
        const double seconds = tolerance->text().toDouble(&valid);
        if (!valid || !std::isfinite(seconds) || seconds < 0.0)
        {
          QMessageBox::warning(&selection, selection.windowTitle(), QStringLiteral("请输入有效的非负时间偏差。"));
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
        selection.accept();
        showDifference(&dialog, std::move(values));
      });
      selection.exec();
    });
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
  QStringList plottedFields;
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

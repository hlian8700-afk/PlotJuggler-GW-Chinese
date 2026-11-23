#pragma once
#include "bladescope_analysis.h"
#include "bladescope_fit_math.h"
#include "plotwidget.h"
#include <qwt_plot_curve.h>

namespace BladeWaveformXY
{
inline QWidget* create(QWidget* parent, const PJ::PlotDataMapRef& source, const QStringList& channels,
                 double first, double last, bool detail, const BladeAnalysis::SampleFilter& filter,
                 const QJsonObject& aliases)
{
  const auto title = QStringLiteral("录波 XY 分析");
  if (channels.size() < 2 || channels.size() > 16)
  {
    QMessageBox::information(parent, title, QStringLiteral("请选择 2 至 16 个测点。"));
    return nullptr;
  }
  using BladeFit::Point;
  using BladeFit::Points;
  QMap<QString, Points> unfiltered, filtered;
  for (const auto& name : channels)
  {
    const auto it = source.numeric.find(name.toStdString());
    if (it == source.numeric.end()) continue;
    const auto& series = it->second;
    auto& raw = unfiltered[name];
    auto& kept = filtered[name];
    BladeTimeView::select(BladeAnalysis::boundary(series, first),
        BladeAnalysis::boundary(series,last,true), detail,
        [&](size_t i) { return series.at(i).y; }, [&](size_t i) {
      const auto sample = series.at(i);
      if (!std::isfinite(sample.x) || !std::isfinite(sample.y)) return;
      const Point point{sample.x, sample.y};
      raw.push_back(point);
      if (filter && filter(series, sample.x, sample.y)) kept.push_back(point);
    });
  }

  auto plotted = std::make_shared<PJ::PlotDataMapRef>();
  auto* dialog = new QWidget(parent);
  dialog->setWindowTitle(title);
  dialog->resize(1050, 670);
  QVBoxLayout layout(dialog);
  auto* parameters = new QFormLayout;
  layout.addLayout(parameters);
  auto* x = new QComboBox(dialog);
  auto* y = new QComboBox(dialog);
  auto* model = new QComboBox(dialog);
  for (const auto& name : channels)
  {
    const auto alias = aliases.value(name).toString();
    const auto display = alias.isEmpty() ? name : alias + QStringLiteral("（") + name + QStringLiteral("）");
    x->addItem(display, name);
    y->addItem(display, name);
  }
  y->setCurrentIndex(1);
  model->addItem(QStringLiteral("散点"), 0);
  for (int degree = 1; degree <= 6; ++degree)
    model->addItem(QStringLiteral("%1 阶多项式").arg(degree), degree);
  model->addItem(QStringLiteral("移动最小二乘"), -1);
  auto* neighbors = new QSpinBox(dialog);
  neighbors->setRange(3, 5000);
  neighbors->setValue(41);
  neighbors->setEnabled(false);
  QObject::connect(model, QOverload<int>::of(&QComboBox::currentIndexChanged), dialog,
                   [=](int) { neighbors->setEnabled(model->currentData().toInt() < 0); });
  auto* tolerance = new QDoubleSpinBox(dialog);
  tolerance->setRange(0., 3600.);
  tolerance->setDecimals(6);
  auto* filterEnabled = new QCheckBox(QStringLiteral("应用录波条件筛选"), dialog);
  filterEnabled->setChecked(bool(filter));
  filterEnabled->setEnabled(bool(filter));
  parameters->addRow(filterEnabled);
  auto* difference = new QCheckBox(QStringLiteral("确认两测点单位一致，输出 Y−X"), dialog);
  parameters->addRow(QStringLiteral("X 测点"), x);
  parameters->addRow(QStringLiteral("Y 测点"), y);
  parameters->addRow(QStringLiteral("配对容差（秒）"), tolerance);
  parameters->addRow(QStringLiteral("拟合模型"), model);
  parameters->addRow(QStringLiteral("局部邻点数"), neighbors);
  parameters->addRow(difference);
  auto* plot = new PlotWidget(*plotted, dialog);
  QObject::connect(plot, &QObject::destroyed, [plotted] {});
  layout.addWidget(plot, 3);
  if (!QMetaObject::invokeMethod(plot, "setModeXY", Qt::DirectConnection, Q_ARG(bool, true)))
  {
    QMessageBox::warning(parent, title, QStringLiteral("无法启动原版 XY 视图。"));
    delete dialog;
    return nullptr;
  }
  auto* report = new QPlainTextEdit(dialog);
  report->setReadOnly(true);
  layout.addWidget(report, 1);
  auto* actions = new QHBoxLayout;
  layout.addLayout(actions);
  auto* refresh = new QPushButton(QStringLiteral("生成 XY 与统计"), dialog);
  auto* copy = new QPushButton(QStringLiteral("复制统计"), dialog);
  actions->addWidget(refresh);
  actions->addWidget(copy);
  QObject::connect(copy, &QPushButton::clicked, dialog, [=] {
    QApplication::clipboard()->setText(report->toPlainText());
  });
  auto rendered = std::make_shared<QPair<QString,QString>>();
  auto generate = [=, unfiltered = std::move(unfiltered), filtered = std::move(filtered)](bool preserveViewport) {
    const auto xName = x->currentData().toString(), yName = y->currentData().toString();
    const bool enabled = filterEnabled->isChecked();
    const auto& prepared = enabled ? filtered : unfiltered;
    const Points empty;
    const auto xSeries = prepared.constFind(xName), ySeries = prepared.constFind(yName);
    const auto& xs = xSeries == prepared.cend() ? empty : xSeries.value();
    const auto& ys = ySeries == prepared.cend() ? empty : ySeries.value();
    const auto dt = tolerance->value();
    const int degree = model->currentData().toInt();
    Points pairs, fitted;
    pairs.reserve(ys.size());
    size_t cursor = 0;
    BladeAnalysis::Statistics xStats, yStats, differenceStats;
    std::vector<double> xValues, yValues, differenceValues;
    for (const auto target : ys)
    {
      while (cursor < xs.size() && xs[cursor].x < target.x) ++cursor;
      size_t nearest = cursor;
      if (cursor && (cursor == xs.size() ||
          target.x - xs[cursor-1].x <= xs[cursor].x - target.x)) nearest = cursor-1;
      if (nearest == xs.size() || std::abs(xs[nearest].x - target.x) > dt) continue;
      const double xv = xs[nearest].y, yv = target.y;
      pairs.push_back({xv, yv});
      xStats.add(xv);
      yStats.add(yv);
      xValues.push_back(xv);
      yValues.push_back(yv);
      if (difference->isChecked())
      {
        const double delta = yv-xv;
        if (std::isfinite(delta)) { differenceStats.add(delta); differenceValues.push_back(delta); }
        else ++differenceStats.invalid;
      }
    }
    differenceStats.unmatched = ys.size()-pairs.size();
    QStringList rows{QStringLiteral("%1，原始秒 %2 至 %3")
                      .arg(detail ? QStringLiteral("细化原始点") : QStringLiteral("常规抽稀点"),
                           BladeAnalysis::number(first), BladeAnalysis::number(last)),
                     QStringLiteral("筛选%1；目标候选 %2，成功配对 %3，未配对 %4")
                      .arg(enabled ? QStringLiteral("已启用") : QStringLiteral("未启用"))
                      .arg(qulonglong(ys.size())).arg(qulonglong(pairs.size())).arg(qulonglong(ys.size()-pairs.size()))};
    BladeAnalysis::appendStatistics(rows, x->currentText(), xStats, &xValues);
    BladeAnalysis::appendStatistics(rows, y->currentText(), yStats, &yValues);
    if (difference->isChecked())
      BladeAnalysis::appendStatistics(rows, QStringLiteral("Y−X"), differenceStats, &differenceValues);

    if (degree < 0 && !pairs.empty())
    {
      try
      {
        fitted = BladeFit::movingLeastSquares(pairs, size_t(neighbors->value()));
        rows << QStringLiteral("移动最小二乘：每个位置最多 %1 个邻点，生成 %2 个拟合点")
                    .arg(neighbors->value()).arg(qulonglong(fitted.size()));
      }
      catch (const std::exception& error)
      {
        rows << QStringLiteral("拟合不可用：%1").arg(QString::fromUtf8(error.what()));
      }
    }
    if (degree > 0 && !pairs.empty())
    {
      try
      {
        const auto fit = BladeFit::regress(pairs, degree);
        const auto bounds = std::minmax_element(pairs.begin(),pairs.end(),[](Point a,Point b){return a.x < b.x;});
        fitted = BladeFit::sample(bounds.first->x,bounds.second->x,fit);
        long double residual = 0, variation = 0;
        for (const auto sample : pairs)
        {
          const long double error = sample.y - fit(sample.x);
          const long double centered = sample.y - yStats.mean;
          residual += error*error;
          variation += centered*centered;
        }
        QStringList coefficients;
        for (auto value : fit.coefficients) coefficients << QString::number(value,'g',10);
        rows << QStringLiteral("拟合：z=(X−%1)/%2；按升幂排列系数 %3；R² %4")
            .arg(QString::number(fit.origin,'g',10), QString::number(fit.scale,'g',10), coefficients.join(", "),
                 variation > 0 ? QString::number(double(1-residual/variation),'g',8) : QStringLiteral("不适用"));
      }
      catch (const std::exception& error)
      {
        rows << QStringLiteral("拟合不可用：%1").arg(QString::fromUtf8(error.what()));
      }
    }

    const auto viewport = plot->currentBoundingRect();
    const bool restore = preserveViewport && rendered->first == xName && rendered->second == yName &&
                         !plot->curveList().empty() && !pairs.empty();
    plot->removeAllCurves();
    plotted->scatter_xy.clear();
    QMetaObject::invokeMethod(plot, "setModeXY", Qt::DirectConnection, Q_ARG(bool, true));
    const auto add = [&](const QString& name, const Points& points, bool scatter) {
      if (points.empty()) return;
      const auto key = name.toStdString();
      auto& curve = plotted->addScatterXY(key)->second;
      for (const auto point : points) curve.pushBack({point.x,point.y});
      if (auto* item = plot->addCurve(key))
      {
        item->curve->setStyle(scatter ? QwtPlotCurve::Dots : QwtPlotCurve::Lines);
        item->curve->setSymbol(nullptr);
      }
    };
    const auto xTitle = aliases.value(xName).toString(xName);
    const auto yTitle = aliases.value(yName).toString(yName);
    const auto name = yTitle + QStringLiteral(" / ") + xTitle;
    add(name, pairs, true);
    add(name + QStringLiteral(" · 拟合"), fitted, false);
    plot->zoomOut(false);
    if (restore) plot->setZoomRectangle(viewport, false);
    rendered->first = xName;
    rendered->second = yName;
    plot->replot();
    report->setPlainText(rows.join(QLatin1Char('\n')));
  };
  auto update = std::make_shared<decltype(generate)>(std::move(generate));
  QObject::connect(refresh, &QPushButton::clicked, dialog, [update] { (*update)(false); });
  QObject::connect(filterEnabled, &QCheckBox::toggled, dialog, [update] { (*update)(true); });
  (*update)(false);
  return dialog;
}
}

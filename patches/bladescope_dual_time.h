#pragma once
#include "bladescope_analysis.h"
#include "plotwidget.h"
#include <qwt_plot.h>
#include <qwt_plot_curve.h>
#include <qwt_scale_widget.h>
#include <QScopedValueRollback>

namespace BladeDualTime
{
inline void open(QWidget* parent, const PJ::PlotDataMapRef& source, const QStringList& channels,
                 double first, double last, bool detail, const BladeAnalysis::SampleFilter& filter)
{
  const auto title = QStringLiteral("时序双轴对照");
  if (channels.size() < 2 || channels.size() > 16)
  {
    QMessageBox::information(parent, title, QStringLiteral("请选择 2 至 16 个测点。"));
    return;
  }
  QDialog assignment(parent);
  assignment.setWindowTitle(title);
  QVBoxLayout form(&assignment);
  form.addWidget(new QLabel(QStringLiteral("勾选右轴或下图测点。其余测点显示在左轴或上图。")));
  QListWidget fields;
  for (const auto& channel : channels)
  {
    auto* item = new QListWidgetItem(channel, &fields);
    item->setCheckState(fields.count() == channels.size() ? Qt::Checked : Qt::Unchecked);
  }
  form.addWidget(&fields);
  QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  form.addWidget(&buttons);
  QStringList left, right;
  QObject::connect(&buttons, &QDialogButtonBox::rejected, &assignment, &QDialog::reject);
  QObject::connect(&buttons, &QDialogButtonBox::accepted, &assignment, [&] {
    left.clear();
    right.clear();
    for (int i = 0; i < fields.count(); ++i)
      (fields.item(i)->checkState() == Qt::Checked ? right : left) << fields.item(i)->text();
    if (left.isEmpty() || right.isEmpty())
    {
      QMessageBox::warning(&assignment, title, QStringLiteral("两组均须至少选择一个测点。"));
      return;
    }
    assignment.accept();
  });
  if (assignment.exec() != QDialog::Accepted) return;

  PJ::PlotDataMapRef snapshot;
  struct Bounds
  {
    double low = std::numeric_limits<double>::infinity(), high = -low;
    void add(double value)
    {
      if (!std::isfinite(value)) return;
      low = std::min(low, value);
      high = std::max(high, value);
    }
    void apply(QwtPlot* canvas, QwtAxisId axis) const
    {
      if (!std::isfinite(low)) return;
      const double pad = low == high ? std::max(1., std::abs(low) * .01) : (high - low) * .04;
      canvas->setAxisScale(axis, low - pad, high + pad);
    }
  };
  Bounds leftBounds, rightBounds;
  for (const auto& channel : channels)
  {
    const auto key = channel.toStdString();
    const auto it = source.numeric.find(key);
    if (it == source.numeric.end()) continue;
    const auto& series = it->second;
    auto& points = snapshot.addNumeric(key)->second;
    auto& bounds = right.contains(channel) ? rightBounds : leftBounds;
    BladeTimeView::select(BladeAnalysis::boundary(series, first),
                          BladeAnalysis::boundary(series, last, true), detail,
        [&](size_t i) { return series.at(i).y; },
        [&](size_t i) {
          const auto point = series.at(i);
          if (filter && !filter(series, point.x, point.y)) return;
          points.pushBack({point.x, point.y});
          bounds.add(point.y);
        });
  }
  if (!std::isfinite(leftBounds.low) || !std::isfinite(rightBounds.low))
  {
    QMessageBox::information(parent, title, QStringLiteral("所选区间内至少一组没有有效样本。"));
    return;
  }

  QDialog dialog(parent);
  dialog.setWindowTitle(title);
  dialog.resize(1000, 640);
  QVBoxLayout layout(&dialog);
  auto* bar = new QHBoxLayout;
  layout.addLayout(bar);
  auto* mode = new QComboBox;
  mode->addItems({QStringLiteral("上下联动图"), QStringLiteral("单画布双 Y 轴")});
  bar->addWidget(mode);
  auto* rightRange = new QPushButton(QStringLiteral("设置右轴范围"));
  rightRange->setEnabled(false);
  bar->addWidget(rightRange);
  bar->addStretch();
  auto* pages = new QStackedWidget;
  layout.addWidget(pages, 1);
  auto* split = new QSplitter(Qt::Vertical);
  auto* top = new PlotWidget(snapshot, split);
  auto* bottom = new PlotWidget(snapshot, split);
  split->addWidget(top);
  split->addWidget(bottom);
  auto* single = new PlotWidget(snapshot, pages);
  pages->addWidget(split);
  pages->addWidget(single);
  auto* canvas = single->findChild<QwtPlot*>();
  if (!canvas)
  {
    QMessageBox::warning(parent, title, QStringLiteral("无法取得原版绘图画布。"));
    return;
  }

  const auto append = [](PlotWidget* plot, const QStringList& names, bool rightAxis) {
    for (const auto& name : names)
      if (auto* layer = plot->addCurve(name.toStdString()))
        if (rightAxis) layer->curve->setAxes(QwtPlot::xBottom, QwtPlot::yRight);
  };
  append(top, left, false);
  append(bottom, right, false);
  append(single, left, false);
  append(single, right, true);
  canvas->setAxisVisible(QwtPlot::yRight, true);
  for (auto* plot : {top, bottom, single}) plot->zoomOut(false);
  leftBounds.apply(canvas,QwtPlot::yLeft);
  rightBounds.apply(canvas,QwtPlot::yRight);
  single->replot();

  bool syncing = false;
  auto synchronize = [&](PlotWidget* sender, const QRectF& rectangle) {
    if (syncing || !(rectangle.width() > 0)) return;
    const QScopedValueRollback<bool> lock(syncing, true);
    for (auto* target : {top, bottom, single})
    {
      if (target == sender) continue;
      auto bounds = target->currentBoundingRect();
      const double offset = sender->timeOffset() - target->timeOffset();
      if (bounds.left() == rectangle.left() + offset && bounds.right() == rectangle.right() + offset) continue;
      bounds.setLeft(rectangle.left() + offset);
      bounds.setRight(rectangle.right() + offset);
      target->setZoomRectangle(bounds, false);
      target->replot();
    }
  };
  for (auto* plot : {top, bottom, single})
    if (auto* surface = plot->findChild<QwtPlot*>())
      QObject::connect(surface->axisWidget(QwtPlot::xBottom), &QwtScaleWidget::scaleDivChanged,
                       &dialog, [&, plot, surface] {
        if (dialog.isHidden() || (!pages->currentWidget()->isAncestorOf(plot) &&
                                 pages->currentWidget() != plot)) return;
        const auto time = surface->axisScaleDiv(QwtPlot::xBottom);
        auto bounds = plot->currentBoundingRect();
        bounds.setLeft(std::min(time.lowerBound(), time.upperBound()));
        bounds.setRight(std::max(time.lowerBound(), time.upperBound()));
        synchronize(plot, bounds);
      });

  QObject::connect(mode, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog, [&](int index) {
    auto* previous = pages->currentIndex() == 0 ? top : single;
    synchronize(previous, previous->currentBoundingRect());
    pages->setCurrentIndex(index);
    rightRange->setEnabled(index == 1);
  });
  QObject::connect(rightRange, &QPushButton::clicked, &dialog, [&] {
    QDialog editor(&dialog);
    editor.setWindowTitle(QStringLiteral("设置右轴范围"));
    QFormLayout entries(&editor);
    const auto range = canvas->axisScaleDiv(QwtPlot::yRight);
    QLineEdit low(BladeAnalysis::number(range.lowerBound())), high(BladeAnalysis::number(range.upperBound()));
    entries.addRow(QStringLiteral("下限"), &low);
    entries.addRow(QStringLiteral("上限"), &high);
    QDialogButtonBox save(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    entries.addRow(&save);
    QObject::connect(&save, &QDialogButtonBox::rejected, &editor, &QDialog::reject);
    QObject::connect(&save, &QDialogButtonBox::accepted, &editor, [&] {
      bool a = false, b = false;
      const double minimum = low.text().toDouble(&a), maximum = high.text().toDouble(&b);
      if (!a || !b || !std::isfinite(minimum) || !std::isfinite(maximum) || maximum <= minimum)
      {
        QMessageBox::warning(&editor, editor.windowTitle(), QStringLiteral("请输入有效范围。"));
        return;
      }
      canvas->setAxisScale(QwtPlot::yRight, minimum, maximum);
      single->replot();
      editor.accept();
    });
    editor.exec();
  });
  synchronize(single, single->currentBoundingRect());
  dialog.exec();
}
}

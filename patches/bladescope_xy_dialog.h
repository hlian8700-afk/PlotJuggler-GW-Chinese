#pragma once
#include "bladescope_xy_math.h"
#include "bladescope_trend.h"
#include <functional>

namespace BladeXY
{
inline void open(QWidget* parent, PJ::PlotDataMapRef& source,
                 const std::function<PlotWidget*()>& newPlot)
{
  QStringList names;
  for (const auto& entry : source.numeric)
    if (entry.second.size()) names << QString::fromStdString(entry.first);
  names.sort(Qt::CaseInsensitive);
  if (names.size() < 2)
  {
    QMessageBox::information(parent, QStringLiteral("XY 分析"), QStringLiteral("请先载入至少两个数值测点。"));
    return;
  }

  QDialog dialog(parent);
  dialog.setWindowTitle(QStringLiteral("XY 拟合与差值统计"));
  QFormLayout form(&dialog);
  QComboBox x, y, model;
  x.addItems(names);
  y.addItems(names);
  y.setCurrentIndex(1);
  model.addItems({QStringLiteral("仅配对散点"), QStringLiteral("线性"), QStringLiteral("二次"),
                  QStringLiteral("三次"), QStringLiteral("四次"), QStringLiteral("五次"),
                  QStringLiteral("六次"), QStringLiteral("移动最小二乘")});
  QDoubleSpinBox tolerance;
  tolerance.setRange(0,3600);
  tolerance.setDecimals(6);
  QSpinBox neighbors;
  neighbors.setRange(3,5000);
  neighbors.setValue(41);
  neighbors.setEnabled(false);
  QCheckBox difference(QStringLiteral("确认 X、Y 单位一致，输出 Y−X 统计"));
  QLabel status;
  status.setWordWrap(true);
  QPushButton calculate(QStringLiteral("计算并生成 XY 图"));
  form.addRow(QStringLiteral("X 测点"), &x);
  form.addRow(QStringLiteral("Y 测点"), &y);
  form.addRow(QStringLiteral("时间匹配容差（秒）"), &tolerance);
  form.addRow(QStringLiteral("模型"), &model);
  form.addRow(QStringLiteral("局部拟合邻点数"), &neighbors);
  form.addRow(&difference);
  form.addRow(&calculate);
  form.addRow(&status);
  QObject::connect(&model, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog,
                   [&](int index) { neighbors.setEnabled(index == 7); });

  QObject::connect(&calculate, &QPushButton::clicked, &dialog, [&] {
    auto snapshot = [&](const QString& name) {
      Points points;
      const auto& data = source.numeric.at(name.toStdString());
      points.reserve(data.size());
      for (size_t i = 0; i < data.size(); ++i)
      {
        const auto p = data.at(i);
        points.push_back({p.x, p.y});
      }
      return points;
    };
    const auto label = y.currentText() + QStringLiteral(" 对 ") + x.currentText();
    const auto xData = std::make_shared<Points>(snapshot(x.currentText()));
    const auto yData = std::make_shared<Points>(snapshot(y.currentText()));
    const auto dt = tolerance.value();
    const int selection = model.currentIndex();
    const size_t window = selection == 7 ? size_t(neighbors.value()) : 0;
    const bool showDelta = difference.isChecked();
    calculate.setEnabled(false);
    status.setText(QStringLiteral("计算中…"));
    BladeTrend::background<Result>(&dialog, [=] {
      return analyze(*xData, *yData, dt, selection < 7 ? selection : 0, window);
    }, [&, label, selection, showDelta](Result result, const QString& error) {
      calculate.setEnabled(true);
      if (!error.isEmpty()) { status.setText(error); return; }
      auto* plot = newPlot();
      if (!plot) return;
      if (!QMetaObject::invokeMethod(plot, "setModeXY", Qt::DirectConnection, Q_ARG(bool, true)))
      {
        status.setText(QStringLiteral("原版 XY 模式未能启动。"));
        return;
      }
      std::vector<std::string> created;
      QStringList plotted;
      const auto add = [&](const QString& title, const auto& rows, auto coordinates, bool scatter) {
        const auto stem = (QStringLiteral("XY 分析/") + title).toStdString();
        auto base = stem;
        for (int suffix = 2; source.numeric.count(base + "/X") || source.numeric.count(base + "/Y"); ++suffix)
          base = stem + " [" + std::to_string(suffix) + "]";
        const auto xn = base + "/X", yn = base + "/Y";
        source.addNumeric(xn);
        source.addNumeric(yn);
        created.push_back(xn);
        created.push_back(yn);
        size_t i = 0;
        for (const auto& row : rows)
        {
          const auto point = coordinates(row, i++);
          source.numeric.at(xn).pushBack({point.time, point.x});
          source.numeric.at(yn).pushBack({point.time, point.y});
        }
        auto* layer = plot->addCurveXY(xn, yn, title);
        if (!layer) return false;
        plotted << title;
        layer->curve->setStyle(scatter ? QwtPlotCurve::Dots : QwtPlotCurve::Lines);
        return true;
      };
      const bool measured = add(label, result.observed, [](Pair point, size_t) { return point; }, true);
      const bool fitted = result.fitted.empty() || (measured &&
          add(label + QStringLiteral(" · 拟合"), result.fitted,
              [](Point point, size_t index) { return Pair{double(index),point.x,point.y}; }, false));
      if (!measured || !fitted)
      {
        for (const auto& title : plotted) plot->removeCurve(title);
        for (const auto& name : created) source.numeric.erase(name);
        status.setText(QStringLiteral("原版 XY 接口未能添加分析曲线。"));
        return;
      }
      emit plot->curveListChanged();
      plot->zoomOut(false);
      plot->replot();
      emit plot->undoableChange();
      auto message = QStringLiteral("配对样本 %1，拟合点 %2")
          .arg(qulonglong(result.observed.size())).arg(qulonglong(result.fitted.size()));
      if (selection > 0 && selection < 7)
      {
        const auto& p = result.polynomial;
        if (selection == 1)
        {
          const auto slope = p.coefficients[1] / p.scale;
          message += QStringLiteral("\n斜率 %1，截距 %2").arg(slope,0,'g',10).arg(p.coefficients[0] - slope*p.origin,0,'g',10);
        }
        else
        {
          QStringList coefficients;
          for (double value : p.coefficients) coefficients << QString::number(value,'g',10);
          message += QStringLiteral("\nz=(x−%1)/%2，按升幂排列的系数 %3")
              .arg(p.origin,0,'g',10).arg(p.scale,0,'g',10).arg(coefficients.join(", "));
        }
      }
      if (showDelta)
        message += QStringLiteral("\nY−X 最大 %1，平均 %2，总体标准差 %3")
            .arg(result.maximum,0,'g',10).arg(result.mean,0,'g',10).arg(result.deviation,0,'g',10);
      status.setText(message);
    });
  });
  dialog.exec();
}
}

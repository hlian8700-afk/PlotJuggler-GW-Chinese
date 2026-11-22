#pragma once
#include "bladescope_fit_panel.h"
#include <QScreen>
#include "qwt_scale_div.h"
#include "qwt_text.h"

namespace BladeFitUI
{

inline void verifyM01(QWidget* window, PJ::PlotDataMapRef& data, QToolButton* entry,
                      QStackedWidget* stack, const QString& output)
{
  auto fail = [window, output](const QString& step) {
    const auto message = QStringLiteral("M01 acceptance failed: ") + step;
    QFile log(QDir(output).filePath("failure.txt"));
    if (log.open(QIODevice::WriteOnly)) log.write(message.toUtf8());
    if (auto* screen = window->screen())
      screen->grabWindow(0).save(QDir(output).filePath("failure.png"));
    qCritical().noquote() << message;
    QCoreApplication::exit(1);
  };

  QTimer::singleShot(0, window, [=, &data] {
    const auto workbookPath = QDir(output).filePath("acceptance-power-curve.xlsx");
    const CurveSheet expected{{QStringList{"项目动态功率曲线","","",""},
      QStringList{"Turbine Type","Air Density (Kg/m3)","Turbulent Intensity",""},
      QStringList{"Test turbine","ρ=1.031","Iref=0.142","inflow=0.3"},
      QStringList{"Wind(m/s)","Power (kW)","Ct","Cp"},
      QStringList{"2.5","95.69","0.9399","0.2884"}, QStringList{"3","227.47","0.9299","0.3968"}},3,0,1};
    const auto bytes = writeCurveSheet(expected);
    QFile workbookFile(workbookPath);
    if (bytes.isEmpty() || !workbookFile.open(QIODevice::WriteOnly) || workbookFile.write(bytes) != bytes.size()) {
      fail(QStringLiteral("write standard power curve XLSX"));
      return;
    }
    workbookFile.close();
    try {
      const auto actual = readCurveSheet(workbookPath);
      if (actual.rows != expected.rows || curvePoints(actual) != curvePoints(expected)) {
        fail(QStringLiteral("standard power curve XLSX round-trip"));
        return;
      }
    } catch (const std::exception& ex) {
      fail(QString::fromUtf8(ex.what()));
      return;
    }
    auto& wind = data.addNumeric("BladeFitTestWind")->second;
    auto& power = data.addNumeric("BladeFitTestPower")->second;
    auto& pitch = data.addNumeric("BladeFitTestPitch")->second;
    for (int i = 0; i < 6; ++i) {
      const double v = 6 + i / 2;
      wind.pushBack({double(i), v});
      power.pushBack({double(i), 10 * v});
      pitch.pushBack({double(i), 85.0 + i});
    }
    QSettings settings;
    const QString key = QStringLiteral("BladeScope/curveAnalysis");
    const auto previous = settings.value(key);
    const QJsonObject config{{"role0", "BladeFitTestWind"}, {"role1", "BladeFitTestPower"},
                             {"role4", "BladeFitTestPitch"}, {"diameter", 221},
                             {"rated", 1500}, {"cutIn", 3}, {"ratedWind", 12}, {"cutOut", 25},
                             {"width", 1}, {"tolerance", 0}};
    settings.setValue(key, QJsonDocument(config).toJson(QJsonDocument::Compact));
    auto* original = stack->currentWidget();
    entry->click();
    if (previous.isValid()) settings.setValue(key, previous);
    else settings.remove(key);

    auto* panel = stack->findChild<QWidget*>("bladeFitPanel");
    if (!panel || stack->currentWidget() == original) {
      fail(QStringLiteral("open analysis page"));
      return;
    }
    auto* kind = panel->findChild<QComboBox*>("bladeFitKind");
    auto* reload = panel->findChild<QPushButton*>("bladeFitReload");
    auto* plot = panel->findChild<QwtPlot*>("bladeFitPlot");
    if (!kind || !reload || !plot) {
      fail(QStringLiteral("locate analysis controls"));
      return;
    }
    using Layers = QMap<QString, QVector<QPointF>>;
    struct SwitchState { int phase = 0; Layers initial; double upper = 0; };
    auto state = std::make_shared<SwitchState>();
    auto capture = [plot] {
      Layers layers;
      for (auto* item : plot->itemList(QwtPlotItem::Rtti_PlotCurve)) {
        auto* curve = static_cast<QwtPlotCurve*>(item);
        auto& points = layers[curve->title().text()];
        for (size_t i = 0; i < curve->dataSize(); ++i) points.append(curve->sample(int(i)));
      }
      return layers;
    };
    auto* poll = new QTimer(panel);
    auto elapsed = std::make_shared<QElapsedTimer>();
    elapsed->start();
    QObject::connect(poll, &QTimer::timeout, panel, [=] {
      if (!panel->property("bladeFitResultsReady").toBool()) {
        if (elapsed->elapsed() > 20000) {
          poll->stop();
          auto* status = panel->findChild<QLabel*>("bladeFitStatus");
          fail(QStringLiteral("wait for results: ") + (status ? status->text() : QString()));
        }
        return;
      }
      poll->stop();
      const auto upper = plot->axisScaleDiv(QwtPlot::xBottom).upperBound();
      if (state->phase == 1) {
        if (upper < 85) {
          fail(QStringLiteral("switch to M53 pitch axis"));
          return;
        }
        state->phase = 2;
        elapsed->restart();
        kind->setCurrentIndex(int(M01));
        poll->start(25);
        return;
      }
      if (state->phase >= 2) {
        if (capture() != state->initial || std::abs(upper - state->upper) > 1e-8) {
          fail(QStringLiteral("M01 layers or axis changed after switch or reload"));
          return;
        }
        if (state->phase == 2) {
          state->phase = 3;
          elapsed->restart();
          reload->click();
          poll->start(25);
          return;
        }
      }
      auto inspect = [&](const char* buttonName, int rows, bool exportCsv) {
        auto* button = panel->findChild<QPushButton*>(buttonName);
        if (!button) return false;
        bool verified = false;
        const QString path = QDir(output).filePath("m01-bins.csv");
        QTimer inspectTimer;
        inspectTimer.setSingleShot(true);
        QObject::connect(&inspectTimer, &QTimer::timeout, panel, [&] {
          auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
          if (!dialog) return;
          auto* table = dialog->findChild<QTableWidget*>("bladeFitResultTable");
          if (table && table->rowCount() == rows) {
            if (exportCsv) {
              verified = table->columnCount() == 12;
              for (int row = 0; verified && row < rows; ++row)
                verified = table->item(row, 1)->text().toDouble() == 6 + row &&
                           table->item(row, 2)->text().toDouble() == 60 + 10 * row &&
                           table->item(row, 3)->text().toInt() == 2;
              auto* save = dialog->findChild<QPushButton*>("bladeFitExportCsv");
              if (verified && save) {
                QTimer chooserTimer;
                chooserTimer.setSingleShot(true);
                QObject::connect(&chooserTimer, &QTimer::timeout, dialog, [&] {
                  auto* chooser = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
                  if (!chooser) return;
                  chooser->setDirectory(output);
                  chooser->selectFile(QFileInfo(path).fileName());
                  QMetaObject::invokeMethod(chooser, "accept", Qt::DirectConnection);
                });
                chooserTimer.start(0);
                save->click();
                QFile csv(path);
                verified = csv.open(QIODevice::ReadOnly) &&
                           csv.readAll().replace("\r\n", "\n").contains(
                             "\"6\",\"6\",\"60\",\"2\",\"0\",\"60\",\"60\"");
              } else verified = false;
            } else {
              const auto coefficients = table->item(0, 6)->text().split(',');
              verified = coefficients.size() == 3 &&
                         std::abs(coefficients[0].toDouble() - 70) < 1e-8 &&
                         std::abs(coefficients[1].toDouble() - 10) < 1e-8 &&
                         std::abs(coefficients[2].toDouble()) < 1e-8;
            }
          }
          dialog->accept();
        });
        inspectTimer.start(0);
        button->click();
        return verified;
      };
      if (state->phase == 0) {
        state->initial = capture();
        state->upper = upper;
        if (state->initial.size() != 4 || upper > 30) {
          fail(QStringLiteral("initial M01 drawing"));
          return;
        }
        state->phase = 1;
        elapsed->restart();
        kind->setCurrentIndex(int(M53));
        poll->start(25);
        return;
      }
      if (!inspect("bladeFitBins", 3, true)) {
        fail(QStringLiteral("inspect bins or export CSV"));
        return;
      }
      if (!inspect("bladeFitFormula", 1, false)) {
        fail(QStringLiteral("inspect fitted coefficients"));
        return;
      }
      auto* back = panel->findChild<QPushButton*>("bladeFitBack");
      if (back) back->click();
      if (!back || stack->currentWidget() != original) {
        fail(QStringLiteral("return to time series"));
        return;
      }
      QCoreApplication::exit(0);
    });
    poll->start(25);
  });
}
}

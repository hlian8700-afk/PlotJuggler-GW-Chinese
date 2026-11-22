#pragma once
#include "bladescope_fit_panel.h"
#include "bladescope_sqlite_recording.h"
#include "bladescope_txt_recording.h"
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
    const auto textPath = QDir(output).filePath("acceptance-high-rate.TXT");
    QFile textFixture(textPath);
    const QByteArray textBytes(
        "\xef\xbb\xbf" "WindParkName: Test\r\nTurbineID : 6\r\n"
        "SoftVersion: Test\r\nController: Test\r\n"
        "TimeStamp,grRotorSpeed,grGeneratorSpeed\r\n"
        "2025-07-14-15:32:28.329,9,103\r\n"
        "2025-07-14-15:32:28.309,6,100\r\n"
        "2025-07-14-15:32:28.319,8,102\r\n"
        "2025-07-14-15:32:28.319,7,invalid\r\n"
        "invalid,999,999\r\n");
    if (!textFixture.open(QIODevice::WriteOnly) || textFixture.write(textBytes)!=textBytes.size()) {
      fail(QStringLiteral("write TXT recording fixture"));
      return;
    }
    textFixture.close();
    const std::atomic_bool txtCanceled{false};
    auto highRate = BladeTxtRecording::read(textPath, {}, txtCanceled);
    const auto rotor = highRate.data.numeric.find("grRotorSpeed");
    const auto generator = highRate.data.numeric.find("grGeneratorSpeed");
    if (!highRate.error.isEmpty() || highRate.rows!=4 || highRate.duplicateTime!=1 ||
        highRate.invalidTime!=1 || highRate.invalidValues!=1 ||
        rotor==highRate.data.numeric.end() || rotor->second.size()!=4 ||
        generator==highRate.data.numeric.end() || generator->second.size()!=3 ||
        rotor->second.at(0).y!=6 || rotor->second.at(1).y!=8 || rotor->second.at(2).y!=7 ||
        rotor->second.at(1).x!=rotor->second.at(2).x ||
        std::abs(rotor->second.at(1).x-rotor->second.at(0).x-0.010)>1e-6 ||
        generator->second.at(1).x!=rotor->second.at(1).x) {
      fail(QStringLiteral("TXT metadata, time precision, duplicate order or channel alignment"));
      return;
    }
    const auto recordingPath = QDir(output).filePath("acceptance-recording.DATA");
    const auto connectionName = QUuid::createUuid().toString();
    bool fixtureReady = false;
    {
      auto database = QSqlDatabase::addDatabase("QSQLITE", connectionName);
      database.setDatabaseName(recordingPath);
      const bool opened = database.open();
      QSqlQuery fixture(database);
      fixtureReady = opened &&
        fixture.exec(R"(CREATE TABLE oneseconddata ("WTUR.Tm.Rw.Dt" TEXT, "WGEN.Spd.Ra.F32" REAL, "state flag" TEXT))") &&
        fixture.exec(R"(INSERT INTO oneseconddata VALUES
          ('2026-11-06 00:00:03.250', 103, 'true'),
          ('2026-11-06 00:00:00.100', 100, 'false'),
          ('2026-11-06 00:00:02.125', NULL, 'invalid'),
          ('2026-11-06 00:00:01.500', 101, 'false'),
          ('invalid', 999, 'true'))") &&
        fixture.exec(R"(CREATE TABLE realtimedata (time INTEGER, power REAL))") &&
        fixture.exec(R"(INSERT INTO realtimedata VALUES (2000, 20), (1000, 10))") &&
        fixture.exec(R"(CREATE TABLE realtimedata_1 (time TEXT, power REAL))");
    }
    QSqlDatabase::removeDatabase(connectionName);
    if (!fixtureReady || !BladeSqliteRecording::validHeader(recordingPath)) {
      fail(QStringLiteral("create SQLite DATA fixture or load Qt SQLite driver"));
      return;
    }
    const auto stamp = BladeRecordingIndex::timestamp;
    std::atomic_bool canceled{false};
    auto recording = BladeSqliteRecording::read(recordingPath,
        {"oneseconddata", "WTUR.Tm.Rw.Dt", {"WGEN.Spd.Ra.F32", "state flag"},
         stamp("2026-11-06 00:00:01.500"), stamp("2026-11-06 00:00:03.250"), false},
        {"WGEN.Spd.Ra.F32"}, canceled);
    const auto rpm = recording.data.numeric.find("WGEN.Spd.Ra.F32 [2]");
    const auto flags = recording.data.numeric.find("state flag");
    if (!recording.error.isEmpty() || recording.rows!=3 || recording.invalidTime!=1 ||
        recording.invalidValues!=2 || rpm==recording.data.numeric.end() ||
        rpm->second.size()!=2 || rpm->second.at(0).y!=101 || rpm->second.at(1).y!=103 ||
        rpm->second.at(0).x!=stamp("2026-11-06 00:00:01.500") ||
        flags==recording.data.numeric.end() || flags->second.size()!=2 ||
        flags->second.at(0).y!=0 || flags->second.at(1).y!=1) {
      fail(QStringLiteral("SQLite column selection, time ordering, nulls or name collision"));
      return;
    }
    BladeSqliteRecording::Connection catalog(recordingPath);
    if (BladeSqliteRecording::tables(catalog.db).size()!=3) {
      fail(QStringLiteral("enumerate SQLite data tables"));
      return;
    }
    auto millis = BladeSqliteRecording::read(recordingPath,
        {"realtimedata", "time", {"power"}, 1.0, 2.0, true}, {}, canceled);
    const auto powerSeries = millis.data.numeric.find("power");
    if (!millis.error.isEmpty() || powerSeries==millis.data.numeric.end() ||
        powerSeries->second.size()!=2 || powerSeries->second.at(0).x!=1.0 ||
        powerSeries->second.at(1).x!=2.0) {
      fail(QStringLiteral("SQLite millisecond timestamps"));
      return;
    }
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

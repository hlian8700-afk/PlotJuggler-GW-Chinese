#pragma once
#include "bladescope_fit_panel.h"
#include "bladescope_label_xlsx.h"
#include "bladescope_sqlite_recording.h"
#include "bladescope_txt_recording.h"
#include "bladescope_time_display.h"
#include <QScreen>
#include "qwt_scale_div.h"
#include "qwt_text.h"

namespace BladeFitUI
{
inline void verifyTorqueProjection(QWidget* window,const QString& output,std::function<void(QString)> fail)
{
  auto data=std::make_shared<PJ::PlotDataMapRef>();
  for(int i=0;i<12;++i){
    data->addNumeric("unit_speed")->second.pushBack({double(i),1000.+i*10});
    data->addNumeric("unit_power")->second.pushBack({double(i),1000.+i*20});
  }
  QSettings saved;const auto key=QStringLiteral("BladeScope/curveAnalysis");const auto previous=saved.value(key);const bool existed=saved.contains(key);
  QJsonObject cfg{{"role1","unit_power"},{"role3","unit_speed"},{"rated",6250},{"rpm",1677},{"rpmWidth",10},{"rejectFlags",false}};
  saved.setValue(key,QJsonDocument(cfg).toJson());auto* panel=new Panel(*data,[]{},window);panel->resize(1000,650);panel->show();
  if(existed)saved.setValue(key,previous);else saved.remove(key);
  auto* kind=panel->findChild<QComboBox*>("bladeFitKind");kind->setCurrentIndex(M33);panel->enter();
  auto* timer=new QTimer(panel);auto elapsed=std::make_shared<QElapsedTimer>();elapsed->start();
  QObject::connect(timer,&QTimer::timeout,panel,[=]{
    (void)data;
    if(!panel->property("bladeFitResultsReady").toBool()){if(elapsed->elapsed()>15000){timer->stop();fail("M33 UI computation timeout");}return;}
    timer->stop();auto* unit=panel->findChild<QComboBox*>("bladeTorqueOutput");auto* chart=panel->findChild<QwtPlot*>("bladeFitPlot");
    if(!unit||!chart||unit->currentIndex()!=NewtonMetre){fail("M33 default output unit");return;}
    auto ordinate=[&]{for(auto* item:chart->itemList())if(auto* curve=dynamic_cast<QwtPlotCurve*>(item))
      if(curve->title().text()==QStringLiteral("分箱均值")&&curve->dataSize())return curve->sample(0).y();return -1.;};
    const double before=ordinate();unit->setCurrentIndex(KiloNewtonMetre);QMetaObject::invokeMethod(unit,"activated",Qt::DirectConnection,Q_ARG(int,KiloNewtonMetre));
    if(before<=0||std::abs(ordinate()*1000-before)>1e-7||!chart->axisTitle(QwtPlot::yLeft).text().contains("kN")){fail("M33 UI output projection");return;}
    panel->grab().save(QDir(output).filePath("m33-unit-projection.png"));panel->hide();panel->deleteLater();
    if(existed)QSettings().setValue(key,previous);else QSettings().remove(key);
    QCoreApplication::exit(0);
  });timer->start(25);
}

inline void verifyYaw(QWidget* window,const QString& output,std::function<void(QString)> fail)
{
  auto data=std::make_shared<PJ::PlotDataMapRef>();
  auto add=[&](const char* name,double t,double value){auto& curve=data->addNumeric(name)->second;curve.pushBack({t,value});};
  int t=0;
  for(int band=0;band<2;++band)for(double angle:{0.,4.5,-4.5,5.5,-5.5,6.5,-6.5,7.5,-7.5})for(int n=0;n<3;++n){
    add("yaw_wind",t,6.5+band);add("yaw_angle",t,angle);add("yaw_power",t,500+500*band+(angle>0?40:0));++t;
  }
  QSettings saved;const auto key=QStringLiteral("BladeScope/curveAnalysis");const bool existed=saved.contains(key);const auto previous=saved.value(key);
  const QJsonObject config{{"role0","yaw_wind"},{"role1","yaw_power"},{"role6","yaw_angle"},
    {"m56_config",QJsonObject{{"minSamples",2}}}};
  saved.setValue(key,QJsonDocument(config).toJson(QJsonDocument::Compact));
  auto* panel=new Panel(*data,[]{},window);panel->resize(1200,700);panel->show();
  if(existed)saved.setValue(key,previous);else saved.remove(key);
  auto* kind=panel->findChild<QComboBox*>("bladeFitKind");kind->setCurrentIndex(M56);panel->enter();
  auto* timer=new QTimer(panel);auto elapsed=std::make_shared<QElapsedTimer>();elapsed->start();
  QObject::connect(timer,&QTimer::timeout,panel,[=]{
    (void)data;
    if(!panel->property("bladeFitResultsReady").toBool()){
      if(elapsed->elapsed()>15000){timer->stop();fail("M56 did not compute");}return;
    }
    timer->stop();auto* pairs=panel->findChild<QTableWidget*>("m56Results0");
    auto* bins=panel->findChild<QTableWidget*>("m56Results1");
    auto* view=panel->findChild<QComboBox*>("m56View");auto* unit=panel->findChild<QComboBox*>("m56Unit");auto* wind=panel->findChild<QComboBox*>("m56WindBand");
    if(!pairs||!bins||!view||!unit||!wind||pairs->rowCount()!=8){fail("M56 tables or controls missing");return;}
    for(int row=0;row<pairs->rowCount();++row)if(std::abs(pairs->item(row,12)->text().toDouble()-40)>1e-8){fail("M56 weighted power difference");return;}
    const int computations=panel->property("m56CoreComputations").toInt();unit->setCurrentIndex(1);view->setCurrentIndex(1);wind->setCurrentIndex(1);
    if(panel->property("m56CoreComputations").toInt()!=computations||pairs->rowCount()!=4){fail("M56 view switches recomputed or wind filter failed");return;}
    QFile file(QDir(output).filePath("m56-pairs.csv"));if(!file.open(QIODevice::WriteOnly)){fail("M56 CSV fixture");return;}
    QTextStream stream(&file);stream.setCodec("UTF-8");
    for(int row=0;row<pairs->rowCount();++row){QStringList values;for(int col=0;col<pairs->columnCount();++col)values<<pairs->item(row,col)->text();stream<<values.join(',')<<'\n';}
    stream.flush();file.close();panel->grab().save(QDir(output).filePath("m56-pair-view.png"));
    panel->hide();panel->deleteLater();verifyTorqueProjection(window,output,fail);
  });timer->start(25);
}

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
    const auto mappingPath=QDir(output).filePath("acceptance-labels.xlsx");
    const CurveSheet mapping{{QStringList{"系统","确认程度","中文含义","原始字段"},
                              QStringList{"变流器","已核对","发电机转速","WGEN.Spd.Ra.F32"}},0,3,2};
    const auto mappingBytes=writeCurveSheet(mapping);
    QFile mappingFile(mappingPath);
    if (mappingBytes.isEmpty() || !mappingFile.open(QIODevice::WriteOnly) ||
        mappingFile.write(mappingBytes)!=mappingBytes.size()) {
      fail(QStringLiteral("write XLSX label mapping fixture"));
      return;
    }
    mappingFile.close();
    const auto importedMapping=BladeLabels::readWorkbook(mappingPath);
    if (importedMapping.size()!=1 || importedMapping.value("WGEN.Spd.Ra.F32").toArray()!=
          QJsonArray{QStringLiteral("发电机转速"),QStringLiteral("变流器")} ||
        !window->windowTitle().startsWith("BladeScope")) {
      fail(QStringLiteral("XLSX mapping header recognition or BladeScope window title"));
      return;
    }
    const auto textPath = QDir(output).filePath("acceptance-high-rate.TXT");
    QFile textFixture(textPath);
    const QByteArray textBytes(
        "\xef\xbb\xbf" "WindParkName: Test\r\nTurbineID : 6\r\n"
        "SoftVersion: Test\r\nController: Test\r\n"
        "grRotorSpeed,TimeStamp,grGeneratorSpeed\r\n"
        "9,2025-07-14-15:32:28.329,103\r\n"
        "6,2025-07-14-15:32:28.309,100\r\n"
        "8,2025-07-14-15:32:28.319,102\r\n"
        "7,2025-07-14-15:32:28.319,invalid\r\n"
        "999,invalid,999\r\n");
    if (!textFixture.open(QIODevice::WriteOnly) || textFixture.write(textBytes)!=textBytes.size()) {
      fail(QStringLiteral("write TXT recording fixture"));
      return;
    }
    textFixture.close();
    const std::atomic_bool txtCanceled{false};
    auto highRate = BladeTxtRecording::read(textPath, 1, {}, txtCanceled);
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
    const double sampleTime = rotor->second.at(0).x;
    using namespace BladeTimeDisplay;
    if (format(sampleTime, true, true, true, decimals(Milliseconds, 1.0)) != "2025-07-14 15:32:28.309" ||
        format(sampleTime, true, true, false, decimals(Seconds, 0.01)) != "15:32:28" ||
        decimals(Automatic, 1.0) != 0 || decimals(Automatic, 0.01) != 3) {
      fail(QStringLiteral("single-line timeline date and precision"));
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
      verifyYaw(window,output,[fail](QString message){fail(message);});
    });
    poll->start(25);
  });
}
}

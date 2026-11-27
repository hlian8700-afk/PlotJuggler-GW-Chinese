#include "bladescope_diagnosis_panel.h"
#include <cassert>
#include <iostream>
using namespace BladeDiagnosisUI;
int main(int argc,char** argv){
 QApplication app(argc,argv);QCoreApplication::setOrganizationName("BladeScopeSyntheticTests");QCoreApplication::setApplicationName("DiagnosisImport");QSettings().clear();
 const auto csv=parseCsv("time,code,state\r\n\"2024-02-08 12:00:00.000\",\"Fault,\"\"A\"\"\",1\r\n\"2024-02-08 12:00:01.000\",\"line1\nline2\",0\r\n");
 assert(csv.rows.size()==2&&csv.rows[0][1]=="Fault,\"A\""&&csv.rows[1][1]=="line1\nline2");
 ImportOptions o;o.stateColumn=2;o.utcOffsetSeconds=28800;
 auto imported=importEvents(csv,o,"synthetic.csv","SC");
 assert(imported.events.size()==2&&imported.errors.empty());
 assert(iso(imported.events[0].original)=="2024-02-08 04:00:00.000");
 assert(parseTime("2024-02-08T12:00:00.000+08:00",o)==imported.events[0].original);
 assert(parseTime("2024-02-08T04:00:00Z",o)==imported.events[0].original);
 o.numericMilliseconds=false;assert(parseTime("1707364800",o)==1707364800000LL);
 o.numericMilliseconds=true;assert(parseTime("1707364800000",o)==1707364800000LL);
 bool duplicate=false;try{parseCsv("time,time\n1,2\n");}catch(const std::runtime_error&){duplicate=true;}assert(duplicate);
 bool quote=false;try{parseCsv("time,code\n1,\"missing");}catch(const std::runtime_error&){quote=true;}assert(quote);
 bool columns=false;try{parseCsv("time,code\n1,x,y\n");}catch(const std::runtime_error&){columns=true;}assert(columns);
 auto bad=importEvents(parseCsv("time,code\nbad,x\n1,\n2,valid\n"),ImportOptions{},"synthetic.csv","ActionList");
 assert(bad.events.size()==1&&bad.errors.size()==2);
 std::atomic_bool cancelled{true};QString large="time,code\n";for(int i=0;i<10000;++i)large+="1,x\n";
 bool stopped=false;try{parseCsv(large,',',&cancelled);}catch(const std::runtime_error&){stopped=true;}assert(stopped);
 // Shared SC / ActionList byte-level decoding and structural rejection.
 const auto utf8=QString::fromUtf8("时间;代码;状态\n2025-02-08T12:00:00Z;故障A;1\n").toUtf8();
 auto detected=detectCsv(utf8);
 assert(detected.encoding=="UTF-8"&&detected.separator==';'&&detected.csv.headers[0]==QString::fromUtf8("时间"));
 auto* gb=QTextCodec::codecForName("GB18030");
 auto gbBytes=gb->fromUnicode(QString::fromUtf8("时间\t动作\n2025-02-08T12:00:00Z\t启动\n"));
 assert(detectCsv(gbBytes).encoding=="GB18030");
 auto* utf16=QTextCodec::codecForName("UTF-16LE");
 auto utf16Bytes=QByteArray::fromHex("fffe")+utf16->fromUnicode(QString::fromUtf8("时间,代码\n2025-02-08T12:00:00Z,A\n"));
 assert(detectCsv(utf16Bytes).csv.rows.size()==1);
 bool wrongEncoding=false;try{detectCsv(utf8,"UTF-16LE");}catch(const std::runtime_error&){wrongEncoding=true;}assert(wrongEncoding);
 auto preamble=parseCsv("metadata\ntime,code,\n1,A,\n2,B\n3,C,unexpected\n",',',nullptr,2,true);
 assert(preamble.rows.size()==2&&preamble.errors.size()==1&&preamble.skippedPreamble==1);
 assert(preamble.recordNumbers[0]==3&&preamble.recordNumbers[1]==4);
 auto accounted=importEvents(preamble,ImportOptions{},"synthetic.csv","SC");
 assert(accounted.events.size()==2&&accounted.errors.size()==1&&accounted.events[0].row==3);
 bool single=false;try{detectCsv("entire header\none record\n");}catch(const std::runtime_error&){single=true;}assert(single);
 ImportOptions desc;desc.descriptionColumn=2;
 auto explained=importEvents(parseCsv("time,code,description\n1,A,detail\n"),desc,"synthetic.csv","ActionList");
 assert(explained.events[0].description=="detail");

 // Vendor metadata is deliberately not CSV (including an unmatched quote).
 const auto vendorText=QString::fromUtf8(
   "SeqNo,WindParkName: DEMO\nWindTurbineNumber: 9\nTurbineStyle: Demo\nProgramVersion: \"legacy\n"
   "ParameterVersion: Demo\nErrorCodeVersion: Demo\nController: Demo\n"
   "SeqNo,stTriggerTime,stDescription,iOperation\n"
   "1,DT#2025-02-08-14:10:43.778,DEMO_FAULT,14\n"
   "2,DT#2025-02-08-14:10:44.000,DEMO_EVENT,1\n//END\n");
 auto vendorCsv=detectCsv(vendorText.toUtf8()).csv;
 assert(vendorCsv.skippedPreamble==7&&vendorCsv.skippedFooter==1&&vendorCsv.rows.size()==2);
 assert(vendorCsv.recordNumbers[0]==9);
 auto manualVendor=parseCsv(vendorText,',',nullptr,8);assert(manualVendor.rows==vendorCsv.rows);
 ImportOptions vendorOptions;vendorOptions.timeColumn=1;vendorOptions.codeColumn=2;vendorOptions.utcOffsetSeconds=28800;
 auto vendorEvents=importEvents(vendorCsv,vendorOptions,"synthetic.csv","SC");
 assert(vendorEvents.errors.empty()&&vendorEvents.events.size()==2);
 assert(vendorEvents.events[0].originalTimeText=="DT#2025-02-08-14:10:43.778");
 assert(iso(vendorEvents.events[0].original)=="2025-02-08 06:10:43.778");
 assert(parseTime("DT#2025-2-8-14:10:43.7",vendorOptions)==parseTime("2025-02-08T06:10:43.700Z",vendorOptions));
 assert(parseTime("DATE_AND_TIME#2025-02-08-14:10:43.778_000_000+08:00",vendorOptions)==vendorEvents.events[0].original);
 bool precision=false;try{parseTime("DT#2025-02-08-14:10:43.778001",vendorOptions);}catch(...){precision=true;}assert(precision);
 bool afterFooter=false;try{parseCsv("time,code\n1,A\n//END\n2,B\n");}catch(...){afterFooter=true;}assert(afterFooter);
 auto quotedEnd=parseCsv("time,code\n1,\"first\n//END\nlast\"\n");
 assert(quotedEnd.rows.size()==1&&quotedEnd.rows[0][1].contains("//END")&&quotedEnd.skippedFooter==0);


 auto scHeader=vendorText;scHeader.replace("stTriggerTime,","stTriggerTimeStamp,");
 auto exactHeader=detectCsv(scHeader.toUtf8());assert(exactHeader.csv.rows.size()==2);
 assert(parseTime("DT#2025-2-8-16:5:0.334",vendorOptions)==parseTime("2025-02-08T08:05:00.334Z",vendorOptions));
 const auto headerless=QString("SeqNo, WindParkName: DEMO\nTurbineNumber: 1\nTurbineStyle: Demo\nProgramVersion: Demo\n"
   "1,\"2025-2-8-16:5:0.334  \",\"OPERATION_CONNECT\",\"rDemo;fDemo;\"\n"
   "2,\"2025-2-8-16:5:1.4\",\"OPERATION_CONNECT\",\"rOther;\"\n//END\n");
 auto actions=detectCsv(headerless.toUtf8(),"自动",QChar(),0,false,nullptr,true);
 assert(actions.csv.inferredHeader&&actions.csv.skippedPreamble==4&&actions.csv.rows.size()==2&&actions.csv.recordNumbers[0]==5);
 ImportOptions ao;ao.timeColumn=1;ao.codeColumn=3;assert(importEvents(actions.csv,ao,"synthetic.csv","ActionList").errors.empty());
 bool wrongKind=false;try{detectCsv(headerless.toUtf8());}catch(...){wrongKind=true;}assert(wrongKind);
 bool drew=false;
 Panel panel([]{return QStringList{"synthetic_signal"};},[&](QStringList names,double lo,double hi){
   assert(names==QStringList{"synthetic_signal"});assert(hi-lo==120);drew=true;
 },[]{});
 panel.enter();auto* list=panel.findChild<QListWidget*>("diagnosisEvidenceSignals");
 assert(list&&list->count()==1);list->item(0)->setSelected(true);
 for(auto* b:panel.findChildren<QPushButton*>())if(b->text()=="在录波视图查看所选证据")b->click();
 assert(drew);assert(panel.findChild<QTableWidget*>("diagnosisEventTable")->columnCount()==8);
 // Exercise the real bounded mapping dialog and asynchronous publication for both file kinds.
 for(const auto& kind:QStringList{"SC","ActionList"}){
   QTemporaryFile input;assert(input.open());input.write(kind=="SC"?scHeader.toUtf8():headerless.toUtf8());input.flush();
   bool accepted=false;QTimer confirm;confirm.setInterval(20);
   QObject::connect(&confirm,&QTimer::timeout,&panel,[&]{
     auto* dialog=panel.findChild<QDialog*>("diagnosisImportDialog");if(!dialog)return;
     auto* preview=dialog->findChild<QTableWidget*>("diagnosisImportPreview");
     auto* buttons=dialog->findChild<QDialogButtonBox*>();
     if(!preview||preview->rowCount()!=2||!buttons||!buttons->button(QDialogButtonBox::Ok)->isEnabled())return;
     assert(dialog->width()<=QGuiApplication::primaryScreen()->availableGeometry().width());
     accepted=true;confirm.stop();buttons->button(QDialogButtonBox::Ok)->click();
   });
   QTimer::singleShot(10000,&confirm,[&]{confirm.stop();for(auto* d:panel.findChildren<QDialog*>())d->reject();});
   confirm.start();panel.importEventFile(input.fileName(),kind);assert(accepted);
   QEventLoop waiting;QTimer check;check.setInterval(20);
   const int expected=2;
   QObject::connect(&check,&QTimer::timeout,&waiting,[&]{if(panel.findChild<QTableWidget*>("diagnosisEventTable")->rowCount()==expected)waiting.quit();});
   QTimer::singleShot(10000,&waiting,&QEventLoop::quit);check.start();waiting.exec();
   assert(panel.findChild<QTableWidget*>("diagnosisEventTable")->rowCount()==expected);
   assert(panel.findChild<QLabel*>("diagnosisEventQuality")->text().contains(kind=="SC"?"已加载 2 条":"已加载 4 条"));
   QMetaObject::invokeMethod(panel.findChild<QTableWidget*>("diagnosisEventTable"),"cellClicked",Qt::DirectConnection,Q_ARG(int,0),Q_ARG(int,0));
   assert(panel.findChild<QPlainTextEdit*>("diagnosisSingleEventDetails")->toPlainText().contains("DT#"));
 }

 bool trialStarted=false,trialPassed=false;QTimer trialTimer;trialTimer.setInterval(20);
 QObject::connect(&trialTimer,&QTimer::timeout,&panel,[&]{
   auto* dialog=panel.findChild<QDialog*>("diagnosisTrialDialog");if(!dialog)return;
   auto* out=dialog->findChild<QPlainTextEdit*>("trialOutput");
   if(!trialStarted){
     trialStarted=true;dialog->findChild<QComboBox*>("trialPrimary")->setCurrentIndex(1);
     dialog->findChild<QLineEdit*>("trialThreshold")->setText("100");
     dialog->findChild<QCheckBox*>("trialUnitsConfirmed")->setChecked(true);
     dialog->findChild<QPushButton*>("trialRun")->click();
   }
   if(out->toPlainText().contains("试验条件成立")){
     trialPassed=true;assert(dialog->findChild<QPushButton*>("trialExport")->isEnabled());
     dialog->findChild<QLineEdit*>("trialThreshold")->setText("101");assert(!dialog->findChild<QPushButton*>("trialExport")->isEnabled());
     trialTimer.stop();dialog->reject();
   }
 });
 QTimer::singleShot(20000,&trialTimer,[&]{trialTimer.stop();for(auto* dialog:panel.findChildren<QDialog*>())dialog->reject();});
 trialTimer.start();
 showTrial(&panel,{"synthetic_signal"},"demo","model",0,1,20,[](QString,double,double){
   std::vector<BladeDiagnosis::RawSample> values;for(int i=0;i<=100;++i)values.push_back({i*.01,120});return values;
 });
 assert(trialPassed);


 bool packTested=false;QTimer packTimer;packTimer.setInterval(20);
 QObject::connect(&packTimer,&QTimer::timeout,&panel,[&]{
   auto* dialog=panel.findChild<QDialog*>("diagnosisPackDialog");if(!dialog||packTested)return;
   packTested=true;assert(dialog->findChild<QTableWidget*>("conditionPackRules")->rowCount()==4);
   assert(dialog->findChild<QTableWidget*>("conditionPackMappings")->rowCount()==5);
   assert(!dialog->findChild<QCheckBox*>("conditionPackConfirmed")->isChecked());
   assert(!dialog->findChild<QCheckBox*>("conditionPackPending")->isChecked());
   dialog->findChild<QPushButton*>("conditionPackRun")->click();
   assert(dialog->findChild<QTableWidget*>("conditionPackResults")->rowCount()==4);
   assert(dialog->findChild<QTableWidget*>("conditionPackResults")->item(0,2)->text()==QString::fromUtf8("草稿未执行"));
   assert(dialog->findChild<QPushButton*>("conditionPackExport")->isEnabled());
   dialog->findChild<QCheckBox*>("conditionPackPending")->setChecked(true);
   assert(!dialog->findChild<QPushButton*>("conditionPackExport")->isEnabled());packTimer.stop();dialog->reject();
 });
 QTimer::singleShot(20000,&packTimer,[&]{packTimer.stop();for(auto* dialog:panel.findChildren<QDialog*>())dialog->reject();});packTimer.start();
 showConditionPack(&panel,{"synthetic_signal"},"demo","model",0,1,[](QString,double,double){return std::vector<BladeDiagnosis::RawSample>{{0,1},{1,1}};},[](QStringList,double,double){});
 assert(packTested);

 ConditionPack fusionPack;fusionPack.id="demo";fusionPack.version="1";fusionPack.vendor="demo";fusionPack.model="model";
 ConditionRule wave;wave.id="wave";wave.title="wave";wave.primaryRole="speed";wave.unit="rpm";wave.state=BladeDiagnosis::RuleState::PendingValidation;wave.source="invented";wave.spec.threshold=100;fusionPack.rules<<wave;
 CandidateRule candidate;candidate.id="candidate";candidate.title="invented candidate";candidate.state=BladeDiagnosis::RuleState::PendingValidation;candidate.source="invented";candidate.conditions=QStringList{"wave"};candidate.codes.append({"SC","numeric_prefix","0100010"});fusionPack.candidates<<candidate;
 ConditionResult calculated;calculated.id="wave";calculated.ran=true;calculated.trial.state=BladeDiagnosis::TrialState::Supported;
 EventRow recorded;recorded.kind="SC";recorded.code="0100010_SC_DEMO";recorded.aligned=500;recorded.original=500;recorded.source="synthetic.csv";recorded.row=1;
 bool fusionPassed=false;int fusionStage=0;QTimer fusionTimer;fusionTimer.setInterval(20);
 QObject::connect(&fusionTimer,&QTimer::timeout,&panel,[&]{
   auto* dialog=panel.findChild<QDialog*>("diagnosisFusionDialog");if(!dialog)return;
   if(fusionStage==0){fusionStage=1;assert(!dialog->findChild<QCheckBox*>("fusionIdentity")->isChecked());dialog->findChild<QPushButton*>("fusionRun")->click();}
   else if(fusionStage==1){auto* result=dialog->findChild<QTableWidget*>("fusionResults");if(result->rowCount()!=1)return;assert(result->item(0,2)->text().contains("部分支持"));fusionStage=2;dialog->findChild<QCheckBox*>("fusionIdentity")->setChecked(true);dialog->findChild<QPushButton*>("fusionRun")->click();}
   else if(fusionStage==2){auto* result=dialog->findChild<QTableWidget*>("fusionResults");if(result->rowCount()!=1)return;assert(result->item(0,2)->text().contains("候选证据共同满足"));assert(dialog->findChild<QPushButton*>("fusionExport")->isEnabled());dialog->findChild<QCheckBox*>("fusionSCComplete")->setChecked(true);assert(!dialog->findChild<QPushButton*>("fusionExport")->isEnabled());fusionPassed=true;fusionStage=3;fusionTimer.stop();dialog->reject();}
 });
 QTimer::singleShot(20000,&fusionTimer,[&]{fusionTimer.stop();for(auto* dialog:panel.findChildren<QDialog*>())dialog->reject();});fusionTimer.start();
 showCandidateFusion(&panel,fusionPack,{"demo","model",{}},{calculated},numericalDefinitionHash(fusionPack),{},{recorded},0,1,true);
 assert(fusionPassed);

 // Batch stays embedded, preserves attempts and completed reports, and exposes detail/back.
 auto* batchEntry=panel.findChild<QPushButton*>("diagnosisBatchEntry");assert(batchEntry);batchEntry->click();
 assert(panel.findChild<BatchPage*>("diagnosisBatchPage"));
 QTemporaryDir batchDirectory;assert(batchDirectory.isValid());const auto batchPath=batchDirectory.filePath("recording.csv");
 {QFile f(batchPath);assert(f.open(QIODevice::WriteOnly));f.write("TimeStamp,speed\n0,120\n1,130\n2,140\n");}
 ConditionPack batchPack;batchPack.id="batch-demo";batchPack.version="1";batchPack.vendor="demo";batchPack.model="model";
 ConditionRule batchRule;batchRule.id="speed";batchRule.title="invented";batchRule.primaryRole="speed";batchRule.unit="rpm";batchRule.source="synthetic";batchRule.state=BladeDiagnosis::RuleState::PendingValidation;batchRule.spec.threshold=100;batchRule.spec.maximumGapMs=1000;batchPack.rules<<batchRule;
 const auto batchKey="BladeScope/conditionMappings/"+QString::fromLatin1(QCryptographicHash::hash(QByteArray("demo\nmodel\n\nbatch-demo\n1"),QCryptographicHash::Sha256).toHex());
 QSettings().setValue(batchKey,QJsonDocument(QJsonObject{{"speed",QJsonObject{{"field","speed"},{"unit","rpm"},{"scale",1},{"axis",0}}}}).toJson());
 BatchPage batch([]{return BladeDiagnosis::Scope{"demo","model",{}};},[]{});
 batch.setPack(batchPack);batch.addFiles({batchPath,batchPath});batch.setReportDirectory(batchDirectory.path());
 auto* batchTable=batch.findChild<QTableWidget*>("diagnosisBatchTasks");assert(batchTable&&batchTable->rowCount()==1);
 batch.findChild<QCheckBox*>("batchIncludePending")->setChecked(true);assert(!batch.findChild<QCheckBox*>("batchMappingsConfirmed")->isChecked());batch.findChild<QCheckBox*>("batchMappingsConfirmed")->setChecked(true);
 batch.runPending();assert(batchTable->item(0,5)->text()=="1");const auto firstReport=batchTable->item(0,7)->text();assert(QFileInfo::exists(firstReport));
 batch.runPending();assert(batchTable->item(0,5)->text()=="1");assert(batch.summaryCsv().contains("recording.csv"));
 batchTable->cellDoubleClicked(0,0);assert(batch.findChild<QPlainTextEdit*>("batchResultDetails")->toPlainText().contains("bladescope.batch-job.v1"));
 assert(QFileInfo::exists(firstReport));
 std::cout<<"CSV quoting, explicit units/time zones, rejection accounting, cancellation and diagnosis workspace passed\n";
}

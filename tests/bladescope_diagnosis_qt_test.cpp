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
 assert(iso(vendorEvents.events[0].original)=="2025-02-08 06:10:43.778");
 assert(parseTime("DT#2025-2-8-14:10:43.7",vendorOptions)==parseTime("2025-02-08T06:10:43.700Z",vendorOptions));
 assert(parseTime("DATE_AND_TIME#2025-02-08-14:10:43.778_000_000+08:00",vendorOptions)==vendorEvents.events[0].original);
 bool precision=false;try{parseTime("DT#2025-02-08-14:10:43.778001",vendorOptions);}catch(...){precision=true;}assert(precision);
 bool afterFooter=false;try{parseCsv("time,code\n1,A\n//END\n2,B\n");}catch(...){afterFooter=true;}assert(afterFooter);
 auto quotedEnd=parseCsv("time,code\n1,\"first\n//END\nlast\"\n");
 assert(quotedEnd.rows.size()==1&&quotedEnd.rows[0][1].contains("//END")&&quotedEnd.skippedFooter==0);

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
   QTemporaryFile input;assert(input.open());input.write(vendorText.toUtf8());input.flush();
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
   const int expected=kind=="SC"?2:4;
   QObject::connect(&check,&QTimer::timeout,&waiting,[&]{if(panel.findChild<QTableWidget*>("diagnosisEventTable")->rowCount()==expected)waiting.quit();});
   QTimer::singleShot(10000,&waiting,&QEventLoop::quit);check.start();waiting.exec();
   assert(panel.findChild<QTableWidget*>("diagnosisEventTable")->rowCount()==expected);
 }
 std::cout<<"CSV quoting, explicit units/time zones, rejection accounting, cancellation and diagnosis workspace passed\n";
}

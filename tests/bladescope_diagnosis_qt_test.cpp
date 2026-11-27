#include "bladescope_diagnosis_panel.h"
#include <cassert>
#include <iostream>
using namespace BladeDiagnosisUI;
int main(int argc,char** argv){
 QApplication app(argc,argv);
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
 bool drew=false;
 Panel panel([]{return QStringList{"synthetic_signal"};},[&](QStringList names,double lo,double hi){
   assert(names==QStringList{"synthetic_signal"});assert(hi-lo==120);drew=true;
 },[]{});
 panel.enter();auto* list=panel.findChild<QListWidget*>("diagnosisEvidenceSignals");
 assert(list&&list->count()==1);list->item(0)->setSelected(true);
 for(auto* b:panel.findChildren<QPushButton*>())if(b->text()=="在录波视图查看所选证据")b->click();
 assert(drew);assert(panel.findChild<QTableWidget*>("diagnosisEventTable")->columnCount()==7);
 std::cout<<"CSV quoting, explicit units/time zones, rejection accounting, cancellation and diagnosis workspace passed\n";
}

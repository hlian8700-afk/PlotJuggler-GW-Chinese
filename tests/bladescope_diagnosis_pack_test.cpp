#include "bladescope_diagnosis_pack.h"
#include <cassert>
using namespace BladeDiagnosisUI;
using namespace BladeDiagnosis;
int main(){
 ConditionPack p;p.id="demo";p.version="1";p.vendor="demo";p.model="model";
 for(int i=0;i<4;++i){ConditionRule r;r.id=QString::number(i);r.title=r.id;r.spec.family=static_cast<TrialFamily>(i);r.unit=i==1?"deg":i==2?"m/s2":"rpm";r.primaryRole="role"+r.id;r.state=RuleState::PendingValidation;r.source="invented-test";r.spec.threshold=i==0?100:1;r.spec.durationMs=500;r.spec.maximumGapMs=20;if(i==1)r.referenceRole="command";p.rules<<r;}
 const auto bytes=QJsonDocument(conditionPackJson(p)).toJson();p=parseConditionPack(bytes);assert(p.rules.size()==4&&p.sourceHash.size()==64);
 QMap<QString,RoleBinding> bindings;ConditionSnapshots data;
 for(const auto& r:p.rules){bindings[r.primaryRole]={r.primaryRole,r.unit,1,r.spec.family==TrialFamily::PitchFollowing?1:0};auto& values=data[r.primaryRole];for(int i=0;i<=100;++i)values.push_back({i*.01,150});}
 bindings["command"]={"command","deg",1,1};for(int i=0;i<=100;++i)data["command"].push_back({i*.01,140});
 auto results=runConditionPack(p,{"demo","model",{}},bindings,data,{0,1000},true,false);for(const auto& r:results)assert(!r.ran);
 results=runConditionPack(p,{"demo","model",{}},bindings,data,{0,1000},true,true);assert(results[0].trial.state==TrialState::Supported&&results[1].trial.state==TrialState::Supported&&results[2].trial.state==TrialState::NotSupported&&results[3].trial.state==TrialState::NotSupported);
 auto wrong=runConditionPack(p,{"other","model",{}},bindings,data,{0,1000},true,true);for(const auto& r:wrong)assert(!r.ran&&r.status==QString::fromUtf8("机型不适用"));
 auto missing=bindings;missing.remove("role0");assert(!runConditionPack(p,{"demo","model",{}},missing,data,{0,1000},true,true)[0].ran);
 auto mismatch=bindings;mismatch["command"].axis=2;assert(runConditionPack(p,{"demo","model",{}},mismatch,data,{0,1000},true,true)[1].trial.state==TrialState::Invalid);
 auto conflict=bindings;conflict["role1"].field="role0";assert(!runConditionPack(p,{"demo","model",{}},conflict,data,{0,1000},true,true)[0].ran);
 assert(std::abs(conditionUnitGain("rad/s","rpm")-9.5492965855)<1e-9);assert(conditionUnitGain("m/s","mm/s")==1000);assert(conditionUnitGain("g","m/s2")==9.80665);
 bool dimensions=false;try{conditionUnitGain("mm/s","m/s2");}catch(...){dimensions=true;}assert(dimensions);
 auto invalid=p;invalid.rules[0].state=RuleState::Validated;
 bool proof=false;try{parseConditionPack(QJsonDocument(conditionPackJson(invalid)).toJson());}catch(...){proof=true;}assert(proof);
 invalid.rules[0].reviewer="demo-reviewer";invalid.rules[0].reviewDate="2025-01-01";invalid.rules[0].cases=QStringList{"invented-case"};invalid=parseConditionPack(QJsonDocument(conditionPackJson(invalid)).toJson());assert(runConditionPack(invalid,{"demo","model",{}},bindings,data,{0,1000},true,false)[0].ran);
 auto duplicate=conditionPackJson(p);auto rules=duplicate["conditions"].toArray();rules.append(rules[0]);duplicate["conditions"]=rules;bool duplicateId=false;try{parseConditionPack(QJsonDocument(duplicate).toJson());}catch(...){duplicateId=true;}assert(duplicateId);
 auto unknown=conditionPackJson(p);unknown["script"]="never-executed";bool rejected=false;try{parseConditionPack(QJsonDocument(unknown).toJson());}catch(...){rejected=true;}assert(rejected);
 const auto firstHash=conditionSnapshotHash(data.at("role0"));auto changed=data.at("role0");changed[0].value+=1;assert(firstHash!=conditionSnapshotHash(changed));assert(firstHash==conditionSnapshotHash(data.at("role0")));
 std::atomic_bool cancelled{true};assert(runConditionPack(p,{"demo","model",{}},bindings,data,{0,1000},true,true,&cancelled).empty());
 p.rules[0].state=RuleState::Draft;p.rules[1].state=RuleState::Disabled;results=runConditionPack(p,{"demo","model",{}},bindings,data,{0,1000},true,true);assert(!results[0].ran&&!results[1].ran);
}

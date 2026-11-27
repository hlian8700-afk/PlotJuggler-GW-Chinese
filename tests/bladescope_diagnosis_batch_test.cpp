#include "bladescope_diagnosis_batch.h"
#include <cassert>
using namespace BladeDiagnosisUI;
using namespace BladeDiagnosis;
static void write(const QString& path,const QByteArray& bytes){QFile f(path);assert(f.open(QIODevice::WriteOnly));assert(f.write(bytes)==bytes.size());}
int main(){
 QTemporaryDir dir;assert(dir.isValid());const auto path=dir.filePath("recording.csv");
 write(path,"WindParkName: DEMO\nTurbineID: 2\nTimeStamp,speed\n0,120\n1,130\n2,140\n//END\n");
 BatchOptions options;options.scope={"demo","model",{}};options.beforeMs=1000;options.afterMs=1000;options.mappingsConfirmed=true;options.includePending=true;
 BatchJob job;job.id="synthetic";job.tr=path;job.attempt=1;std::atomic_bool cancel{false};
 auto wave=readBatchWindow(job,options,{"speed","missing"},&cancel);assert(wave.center==1000&&wave.audit["read_passes"].toInt()==2&&wave.data.at("speed").size()==3&&!wave.data.count("missing"));
 job.manualCenter=true;job.center=1000;wave=readBatchWindow(job,options,{"speed"},&cancel);assert(wave.audit["read_passes"].toInt()==1);
 ConditionPack pack;pack.id="demo";pack.version="1";pack.vendor="demo";pack.model="model";ConditionRule rule;rule.id="speed";rule.title="invented";rule.primaryRole="speed";rule.unit="rpm";rule.source="synthetic";rule.state=RuleState::PendingValidation;rule.spec.threshold=100;rule.spec.maximumGapMs=1000;pack.rules<<rule;
 pack=parseConditionPack(QJsonDocument(conditionPackJson(pack)).toJson());QMap<QString,RoleBinding> bindings;bindings["speed"]={"speed","rpm",1,0};
 auto result=runBatchJob(job,pack,bindings,options,&cancel);assert(result.error.isEmpty()&&result.report["numerical_results"].toArray()[0].toObject()["ran"].toBool());
 options.outputDirectory=dir.path();result=runBatchJob(job,pack,bindings,options,&cancel);assert(!result.reportPath.isEmpty()&&QFileInfo::exists(result.reportPath));auto repeated=runBatchJob(job,pack,bindings,options,&cancel);assert(repeated.state==QString::fromUtf8("失败")&&!repeated.error.isEmpty());
 ++job.attempt;cancel=true;assert(runBatchJob(job,pack,bindings,options,&cancel).state==QString::fromUtf8("已取消"));cancel=false;
 auto wrong=options;wrong.scope.model="other";assert(runBatchJob(job,pack,bindings,wrong,&cancel).state==QString::fromUtf8("机型不适用"));
 auto draft=pack;draft.rules[0].state=RuleState::Draft;assert(runBatchJob(job,draft,bindings,options,&cancel).state==QString::fromUtf8("没有可执行条件"));
 write(path,"TimeStamp,speed\n0,1\n2,2\n1,3\n");result=runBatchJob(job,pack,bindings,options,&cancel);assert(result.state==QString::fromUtf8("失败"));
 write(path,"TimeStamp,speed\n0,1\n1,2\n2,3\n");bindings["speed"].field="absent";result=runBatchJob(job,pack,bindings,options,&cancel);assert(result.state==QString::fromUtf8("数据不足/质量异常"));
 assert(filenameCenter("2025-02-08-22-11-13-777_demo.txt",options.time)==parseTime("2025-02-08T22:11:13.777",options.time));
 options.outputDirectory.clear();bindings["speed"].field="speed";auto bad=job;bad.id="missing-file";bad.tr=dir.filePath("missing.csv");auto next=job;next.id="next-file";
 auto queue=runBatchQueue({bad,next},pack,bindings,options,&cancel);assert(queue.size()==2&&queue[0].state==QString::fromUtf8("失败")&&queue[1].error.isEmpty());
 queue=runBatchQueue({next,bad},pack,bindings,options,&cancel,[&](int,const BatchResult&){cancel=true;});assert(queue.size()==1);cancel=false;

}

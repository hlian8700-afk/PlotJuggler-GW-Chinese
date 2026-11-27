#pragma once
#include "bladescope_diagnosis_fusion.h"
#include <functional>
namespace BladeDiagnosisUI {
struct BatchJob {QString id,tr,sc,actions;bool manualCenter=false;qint64 center=0;int attempt=0;};
struct BatchOptions {
  BladeDiagnosis::Scope scope;ImportOptions time;QString encoding="auto",timeColumn="TimeStamp";QChar delimiter;
  int beforeMs=30000,afterMs=30000,scOffsetMs=0;bool mappingsConfirmed=false,includePending=false,eventsConfirmed=false,scComplete=false,actionsComplete=false;
  QString outputDirectory;int maximumSamplesPerField=2000000;
};
struct BatchResult {QString id,state,error,reportPath;QJsonObject report;int attempt=0;};
struct CachedBatchEvents {QVector<EventRow> events;QMap<QString,QString> metadata;QString sha256;};
struct BatchRunCache {QCache<QString,CachedBatchEvents> events;int eventHits=0;BatchRunCache():events(65536){};};
struct WaveWindow {ConditionSnapshots data;QMap<QString,QString> metadata;QJsonObject audit;qint64 center=0;};
inline QStringList waveCells(const QString& line,QChar delimiter){
  QStringList cells;QString value;bool quoted=false,closed=false;
  for(int i=0;i<line.size();++i){const auto c=line[i];if(quoted){if(c=='"'){if(i+1<line.size()&&line[i+1]=='"'){value+='"';++i;}else{quoted=false;closed=true;}}else value+=c;continue;}
    if(c=='"'){packCheck(value.isEmpty()&&!closed,"录波 CSV 引号位置无效");quoted=true;}
    else if(c==delimiter){cells<<value;value.clear();closed=false;}
    else {packCheck(!closed||c.isSpace(),"录波 CSV 闭引号后有非法字符");if(!closed)value+=c;}}
  packCheck(!quoted,"批量录波数值文件暂不支持跨行引号记录");cells<<value;return cells;
}
inline QString fileCodec(QFile& file,QString requested){
  if(requested!="auto")return requested;const auto bytes=file.peek(4096);
  if(bytes.startsWith(QByteArray::fromHex("fffe")))return "UTF-16LE";if(bytes.startsWith(QByteArray::fromHex("feff")))return "UTF-16BE";
  for(int trim=0;trim<=3&&trim<bytes.size();++trim)try{decodeCsv(bytes.left(bytes.size()-trim),"UTF-8");return "UTF-8";}catch(...){}return "GB18030";
}
inline qint64 filenameCenter(const QString& path,const ImportOptions& time){
  const auto match=QRegularExpression("(\\d{4}-\\d{2}-\\d{2})-(\\d{2})-(\\d{2})-(\\d{2})-(\\d{3})(?:_|$)").match(QFileInfo(path).completeBaseName());
  if(!match.hasMatch())return std::numeric_limits<qint64>::min();return parseTime(match.captured(1)+"T"+match.captured(2)+":"+match.captured(3)+":"+match.captured(4)+"."+match.captured(5),time);
}
inline QString metaKey(QString name){name=name.trimmed();if(name=="WindTurbineNumber"||name=="TurbineNumber"||name=="TurbineID")return "unit";if(name=="TurbineStyle")return "model";if(name=="ProgramVersion"||name=="SoftVersion")return "firmware";if(name=="Controller")return "controller";if(name=="WindParkName")return "park";return {};}
inline void captureMetadata(QMap<QString,QString>& metadata,const QString& line){const int separator=line.indexOf(':');if(separator<0)return;auto name=line.left(separator).trimmed();if(name.startsWith("SeqNo,"))name=name.mid(6).trimmed();const auto key=metaKey(name);if(!key.isEmpty())metadata[key]=line.mid(separator+1).trimmed();}
inline void batchCancelled(const std::atomic_bool* cancel){if(BladeDiagnosis::trialCancelled(cancel))throw std::runtime_error("已取消");}
inline WaveWindow readBatchWindow(const BatchJob& job,const BatchOptions& options,const QSet<QString>& requested,const std::atomic_bool* cancel){
  packCheck(!job.tr.startsWith("\\\\")&&!job.tr.startsWith("//"),"此阶段仅支持本地文件；共享目录超时隔离尚未接入");QFile file(job.tr);packCheck(file.open(QIODevice::ReadOnly),"录波文件无法打开");packCheck(file.size()<=2LL*1024*1024*1024,"单个录波文件超过 2 GiB 上限");
  const QFileInfo initialInfo(job.tr);WaveWindow output;const auto codec=fileCodec(file,options.encoding);qint64 center=job.manualCenter?job.center:filenameCenter(job.tr,options.time);bool needScan=center==std::numeric_limits<qint64>::min();int passes=needScan?2:1;
  QStringList header;QChar delimiter;int timeIndex=-1;QMap<QString,int> columns;quint64 rows=0;double first=std::numeric_limits<double>::infinity(),last=-first;
  for(int pass=0;pass<passes;++pass){batchCancelled(cancel);packCheck(file.seek(0),"录波文件无法重置读取位置");QTextStream stream(&file);stream.setCodec(codec.toLatin1().constData());stream.setAutoDetectUnicode(options.encoding=="auto");bool haveHeader=false;int lineNumber=0;rows=0;first=std::numeric_limits<double>::infinity();last=-first;double previous=-first;
    QMap<QString,BladeDiagnosis::RawSample> before,after;quint64 retained=0;
    while(!stream.atEnd()){
      batchCancelled(cancel);QString line=stream.readLine(1024*1024+1);++lineNumber;packCheck(line.size()<=1024*1024,"录波记录超过 1 MiB 上限");packCheck(!line.contains(QChar(0xfffd)),"录波文本存在解码替换字符，请明确选择正确编码");if(line.startsWith(QChar(0xfeff)))line.remove(0,1);if(line.trimmed().isEmpty())continue;
      if(!haveHeader){packCheck(lineNumber<=256,"未在前 256 行找到录波时间表头");const QVector<QChar> candidates=options.delimiter.isNull()?QVector<QChar>{',','\t',';'}:QVector<QChar>{options.delimiter};int matched=0;QStringList names;QChar candidateDelimiter;
        for(const auto candidate:candidates){try{auto parsed=waveCells(line,candidate);for(auto& name:parsed)name=name.trimmed();if(parsed.contains(options.timeColumn)){++matched;names=parsed;candidateDelimiter=candidate;}}catch(...) {}}
        if(!matched){captureMetadata(output.metadata,line);continue;}packCheck(matched==1,"录波分隔符存在歧义，请手动选择");while(!names.empty()&&names.last().isEmpty())names.removeLast();packCheck(QSet<QString>(names.begin(),names.end()).size()==names.size()&&!names.contains(QString()),"录波表头重复或存在空列");packCheck(names.size()<=10000,"录波列数超过一万列上限");header=names;delimiter=candidateDelimiter;timeIndex=header.indexOf(options.timeColumn);columns.clear();for(const auto& field:requested)if(header.contains(field))columns[field]=header.indexOf(field);haveHeader=true;continue;
      }
      if(line.trimmed()=="//END"){while(!stream.atEnd()){batchCancelled(cancel);const auto tail=stream.readLine(1024*1024+1);packCheck(tail.size()<=1024*1024&&tail.trimmed().isEmpty(),"录波结束标记之后仍有内容");}break;}
      auto cells=waveCells(line,delimiter);while(cells.size()>header.size()&&cells.last().trimmed().isEmpty())cells.removeLast();packCheck(cells.size()==header.size(),QString("录波第 %1 行列数不一致").arg(lineNumber).toUtf8().constData());
      qint64 timestamp;try{timestamp=parseTime(cells[timeIndex],options.time);}catch(const std::exception& e){throw std::runtime_error(QString("录波第 %1 行时间无效：%2").arg(lineNumber).arg(QString::fromUtf8(e.what())).toUtf8().constData());}
      const double t=timestamp/1000.;packCheck(t>=previous,"录波时间出现倒序，未排序或静默修复");previous=t;first=std::min(first,t);last=std::max(last,t);++rows;
      if(needScan&&pass==0)continue;const auto lo=(center-qint64(options.beforeMs))/1000.,hi=(center+qint64(options.afterMs))/1000.;
      for(auto column=columns.begin();column!=columns.end();++column){bool valid=false;double value=cells[column.value()].trimmed().toDouble(&valid);if(!valid)value=std::numeric_limits<double>::quiet_NaN();const BladeDiagnosis::RawSample sample{t,value};
        if(t<lo){before[column.key()]=sample;continue;}if(t>hi){if(!after.contains(column.key()))after[column.key()]=sample;continue;}
        auto& samples=output.data[column.key()];packCheck(int(samples.size())<options.maximumSamplesPerField,"单个测点窗口超过样本上限，请缩小窗口");packCheck(++retained<=8000000,"窗口累计超过八百万个测点样本，请缩小窗口");samples.push_back(sample);
      }
    }
    packCheck(haveHeader&&rows>0,"录波缺少表头或数据记录");if(needScan&&pass==0){center=qRound64((first+(last-first)/2)*1000);continue;}
    for(const auto& field:requested){if(!columns.contains(field))continue;auto& samples=output.data[field];if(before.contains(field))samples.insert(samples.begin(),before.value(field));if(after.contains(field))samples.push_back(after.value(field));}
    packCheck(stream.status()==QTextStream::Ok||stream.status()==QTextStream::ReadPastEnd,"录波读取失败");
  }
  packCheck(QFileInfo(job.tr).size()==initialInfo.size()&&QFileInfo(job.tr).lastModified()==initialInfo.lastModified(),"读取期间录波发生变化，未混用新旧数据");output.center=center;QJsonObject metadata;for(auto i=output.metadata.begin();i!=output.metadata.end();++i)metadata[i.key()]=i.value();output.audit={{"path",job.tr},{"encoding",codec},{"time_column",options.timeColumn},{"utc_offset_seconds",options.time.utcOffsetSeconds},{"numeric_milliseconds",options.time.numericMilliseconds},{"rows_scanned",double(rows)},{"read_passes",passes},{"file_bytes",double(file.size())},{"source_start_utc",iso(qRound64(first*1000))},{"source_end_utc",iso(qRound64(last*1000))},{"metadata",metadata}};return output;
}
inline QVector<EventRow> readBatchEvents(QString path,QString kind,const BatchOptions& options,const QMap<QString,QString>& trMeta,const std::atomic_bool* cancel,BatchRunCache* cache=nullptr){
  if(path.isEmpty())return {};packCheck(!path.startsWith("\\\\")&&!path.startsWith("//"),"批量关联事件暂仅支持本地文件");batchCancelled(cancel);const QFileInfo info(path);const auto cacheKey=info.absoluteFilePath()+"|"+QString::number(info.size())+"|"+QString::number(info.lastModified().toMSecsSinceEpoch())+"|"+kind+"|"+options.encoding+"|"+QString::number(options.time.utcOffsetSeconds)+"|"+QString::number(options.time.numericMilliseconds)+"|"+options.time.dateFormat;
  auto verify=[&](const CachedBatchEvents& source){for(const auto& key:QStringList{"unit","park","model","controller","firmware"})if(source.metadata.contains(key)&&trMeta.contains(key))packCheck(source.metadata.value(key)==trMeta.value(key),QString("事件与录波 %1 标识不匹配").arg(key).toUtf8().constData());if(source.metadata.contains("model"))packCheck(source.metadata.value("model").toStdString()==options.scope.model,"关联事件机型与当前上下文不匹配");};
  if(cache)if(const auto* existing=cache->events.object(cacheKey)){verify(*existing);++cache->eventHits;return existing->events;}
  QFile file(path);packCheck(file.open(QIODevice::ReadOnly),"关联事件文件无法打开");packCheck(file.size()<=64*1024*1024,"事件文件超过 64 MiB 上限");const auto bytes=file.readAll();auto detected=detectCsv(bytes,options.encoding=="auto"?"自动":options.encoding,QChar(),0,false,cancel,kind=="ActionList");
  auto time=options.time;time.timeColumn=detected.csv.headers.indexOf("TimeStamp");if(kind=="SC"){time.timeColumn=-1;for(int i=0;i<detected.csv.headers.size();++i)if(QStringList{"sttriggertimestamp","sttriggertime","timestamp","time"}.contains(detected.csv.headers[i].toLower())){time.timeColumn=i;break;}time.codeColumn=detected.csv.headers.indexOf("stDescription");if(time.codeColumn<0)time.codeColumn=detected.csv.headers.indexOf("code");}else time.codeColumn=detected.csv.inferredHeader?3:detected.csv.headers.indexOf("ActionList");
  packCheck(time.timeColumn>=0&&time.codeColumn>=0,"事件列名不属于当前批量配置支持的格式，请先在单条导入中核对");time.stateColumn=-1;time.descriptionColumn=-1;
  QMap<QString,QString> metadata;const auto text=decodeCsv(bytes,detected.encoding);const auto lines=text.split('\n');for(int i=0;i<qMin(detected.csv.skippedPreamble,lines.size());++i)captureMetadata(metadata,lines[i]);for(const auto& key:QStringList{"unit","park","model","controller","firmware"})if(metadata.contains(key)&&trMeta.contains(key))packCheck(metadata.value(key)==trMeta.value(key),QString("事件与录波 %1 标识不匹配，未关联其他机组记录").arg(key).toUtf8().constData());
  if(metadata.contains("model"))packCheck(metadata.value("model").toStdString()==options.scope.model,"关联事件机型与当前上下文不匹配");
  auto result=importEvents(detected.csv,time,path,kind,cancel);packCheck(result.errors.isEmpty(),"关联事件存在无效记录，批量模式未忽略任何记录");packCheck(QFileInfo(path).size()==info.size()&&QFileInfo(path).lastModified()==info.lastModified(),"读取期间事件文件发生变化，未混用新旧记录");if(cache){auto* entry=new CachedBatchEvents{result.events,metadata,QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())};cache->events.insert(cacheKey,entry,int((bytes.size()+1023)/1024));}return result.events;
}
inline BatchResult runBatchJob(const BatchJob& job,const ConditionPack& pack,const QMap<QString,RoleBinding>& bindings,const BatchOptions& options,const std::atomic_bool* cancel,BatchRunCache* cache=nullptr){
  BatchResult result;const int cacheHitsBefore=cache?cache->eventHits:0;result.id=job.id;result.attempt=job.attempt;
  try{
    batchCancelled(cancel);packCheck(QRegularExpression("^[A-Za-z0-9_-]{1,80}$").match(job.id).hasMatch()&&job.attempt>0,"任务 ID 或尝试编号无效");packCheck(!options.outputDirectory.startsWith("\\\\")&&!options.outputDirectory.startsWith("//"),"批量输出暂仅支持本地目录");packCheck(!options.timeColumn.trimmed().isEmpty()&&options.timeColumn.size()<=512,"录波时间列名无效");packCheck(options.beforeMs>=0&&options.afterMs>=0&&(options.beforeMs||options.afterMs)&&options.maximumSamplesPerField>0,"批量窗口或样本限制无效");
    packCheck(options.mappingsConfirmed,"批量机型、时间、单位映射未确认");
    if(pack.vendor.toStdString()!=options.scope.vendor||pack.model.toStdString()!=options.scope.model||(!pack.controller.isEmpty()&&pack.controller.toStdString()!=options.scope.controller)){result.state="机型不适用";result.error="规则包与批量机型/固件上下文不匹配，未读取录波";return result;}
    bool eligible=false;for(const auto& rule:pack.rules)eligible|=rule.state==BladeDiagnosis::RuleState::Validated||(options.includePending&&rule.state==BladeDiagnosis::RuleState::PendingValidation);for(const auto& rule:pack.candidates)eligible|=rule.state==BladeDiagnosis::RuleState::Validated||(options.includePending&&rule.state==BladeDiagnosis::RuleState::PendingValidation);if(!eligible){result.state="没有可执行条件";result.error="草稿及停用项不执行；待验证项需显式选择仅试跑";return result;}
    QSet<QString> fields;for(const auto& rule:pack.rules)if(rule.state==BladeDiagnosis::RuleState::Validated||(options.includePending&&rule.state==BladeDiagnosis::RuleState::PendingValidation)){if(!bindings.value(rule.primaryRole).field.isEmpty())fields.insert(bindings.value(rule.primaryRole).field);if(!rule.referenceRole.isEmpty()&&!bindings.value(rule.referenceRole).field.isEmpty())fields.insert(bindings.value(rule.referenceRole).field);}
    auto wave=readBatchWindow(job,options,fields,cancel);if(wave.metadata.contains("model"))packCheck(wave.metadata.value("model").toStdString()==options.scope.model,"录波机型声明与当前上下文不匹配");if(!options.scope.controller.empty())packCheck(wave.metadata.value("controller").toStdString()==options.scope.controller||wave.metadata.value("firmware").toStdString()==options.scope.controller,"录波控制器/固件无法匹配已声明的规则范围");
    const BladeDiagnosis::Interval window{wave.center-options.beforeMs,wave.center+options.afterMs};auto events=readBatchEvents(job.sc,"SC",options,wave.metadata,cancel,cache);events+=readBatchEvents(job.actions,"ActionList",options,wave.metadata,cancel,cache);
    for(auto& event:events)event.aligned=BladeDiagnosis::alignedScTime(event.original,event.kind=="SC"?options.scOffsetMs:0);
    auto calculations=runConditionPack(pack,options.scope,bindings,wave.data,window,true,options.includePending,cancel);auto fusion=runCandidateFusion(pack,options.scope,calculations,numericalDefinitionHash(pack),events,window,options.includePending,options.eventsConfirmed,options.scComplete&&!job.sc.isEmpty(),options.actionsComplete&&!job.actions.isEmpty(),cancel);batchCancelled(cancel);
    QJsonArray numerical,candidates;QJsonObject snapshots,bindingInfo;int insufficient=0,executed=0,candidateExecuted=0;
    for(int i=0;i<calculations.size();++i){const auto& r=calculations[i];if(r.ran)++executed;const auto state=pack.rules[i].state;const bool eligible=state==BladeDiagnosis::RuleState::Validated||(options.includePending&&state==BladeDiagnosis::RuleState::PendingValidation);if(eligible&&(!r.ran||r.trial.state==BladeDiagnosis::TrialState::Unknown||r.trial.state==BladeDiagnosis::TrialState::Invalid))++insufficient;numerical.append(QJsonObject{{"id",r.id},{"status",r.status},{"reason",r.reason},{"ran",r.ran},{"metric",std::isfinite(r.trial.metric)?QJsonValue(r.trial.metric):QJsonValue(QJsonValue::Null)},{"samples",double(r.trial.samples)},{"merged",double(r.trial.merged)},{"reference_merged",double(r.trial.referenceMerged)},{"unmatched",double(r.trial.unmatched)}});}
    for(int i=0;i<fusion.size();++i){const auto& r=fusion[i];const auto state=pack.candidates[i].state;const bool eligible=state==BladeDiagnosis::RuleState::Validated||(options.includePending&&state==BladeDiagnosis::RuleState::PendingValidation);if(eligible){++candidateExecuted;if(r.truth==BladeDiagnosis::Truth::Unknown)++insufficient;}candidates.append(QJsonObject{{"id",r.id},{"title",r.title},{"status",r.status},{"reason",r.reason},{"truth",r.truth==BladeDiagnosis::Truth::Yes?"yes":r.truth==BladeDiagnosis::Truth::No?"no":"unknown"},{"evidence",r.evidence}});}
    for(const auto& field:wave.data)snapshots[field.first]=QJsonObject{{"samples",double(field.second.size())},{"sha256",conditionSnapshotHash(field.second,cancel)}};for(auto i=bindings.begin();i!=bindings.end();++i)bindingInfo[i.key()]=QJsonObject{{"field",i->field},{"unit",i->unit},{"scale",i->scale},{"axis",i->axis}};batchCancelled(cancel);
    result.state=insufficient?"数据不足/质量异常":"完成证据计算";if(!executed&&!candidateExecuted&&!insufficient)result.state="没有可执行条件";
    result.report={{"format","bladescope.batch-job.v1"},{"software","3.17.0.25"},{"job_id",job.id},{"attempt",job.attempt},{"state",result.state},{"pack",conditionPackJson(pack)},{"pack_sha256",QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(conditionPackJson(pack)).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex())},{"bindings",bindingInfo},{"actual_scope",QJsonObject{{"vendor",QString::fromStdString(options.scope.vendor)},{"model",QString::fromStdString(options.scope.model)},{"controller",QString::fromStdString(options.scope.controller)}}},{"tr",wave.audit},{"sc",job.sc},{"actions",job.actions},{"event_snapshot_sha256",eventSnapshotHash(events,cancel)},{"center_utc",iso(wave.center)},{"start_utc",iso(window.begin)},{"end_utc",iso(window.end)},{"snapshot_audit",snapshots},{"time_zone_offset_seconds",options.time.utcOffsetSeconds},{"sc_offset_ms",options.scOffsetMs},{"event_cache_hits_this_job",cache?cache->eventHits-cacheHitsBefore:0},{"include_pending",options.includePending},{"events_identity_confirmed_by_user",options.eventsConfirmed},{"sc_coverage_confirmed_by_user",options.scComplete},{"actions_coverage_confirmed_by_user",options.actionsComplete},{"numerical_results",numerical},{"candidate_results",candidates},{"notice","条件/候选证据，不是已确认的故障根因；缺测不等于正常"}};
    batchCancelled(cancel);if(!options.outputDirectory.isEmpty()){packCheck(QDir(options.outputDirectory).exists(),"报告目录不存在");const auto path=QDir(options.outputDirectory).filePath(job.id+QString("-attempt-%1.json").arg(job.attempt));packCheck(!QFileInfo::exists(path),"报告文件已存在，未覆盖旧结果");QSaveFile file(path);const auto bytes=QJsonDocument(result.report).toJson();packCheck(file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size(),"报告写入失败");batchCancelled(cancel);packCheck(file.commit(),"报告提交失败");result.reportPath=path;}
  }catch(const std::exception& e){result.error=QString::fromUtf8(e.what());result.state=BladeDiagnosis::trialCancelled(cancel)?"已取消":"失败";result.report={};}return result;
}
// A run owns immutable parameter copies. One file failure never stops later jobs.
inline QVector<BatchResult> runBatchQueue(QVector<BatchJob> jobs,ConditionPack pack,QMap<QString,RoleBinding> bindings,BatchOptions options,const std::atomic_bool* cancel,const std::function<void(int,const BatchResult&)>& completed={}){
  packCheck(jobs.size()<=200,"单批最多 200 个任务");QSet<QString> ids;
  for(const auto& job:jobs){packCheck(!ids.contains(job.id),"批量任务 ID 重复");ids.insert(job.id);}
  QVector<BatchResult> results;BatchRunCache cache;
  for(int i=0;i<jobs.size();++i){if(BladeDiagnosis::trialCancelled(cancel))break;auto result=runBatchJob(jobs[i],pack,bindings,options,cancel,&cache);if(completed)completed(i,result);if(!result.reportPath.isEmpty())result.report={};results<<result;if(BladeDiagnosis::trialCancelled(cancel))break;}
  return results;
}

}

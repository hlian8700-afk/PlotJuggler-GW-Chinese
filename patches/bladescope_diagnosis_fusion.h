#pragma once
#include "bladescope_diagnosis_pack.h"
#include "bladescope_diagnosis_events.h"
namespace BladeDiagnosisUI {
struct CandidateResult {QString id,title,status,reason;BladeDiagnosis::Truth truth=BladeDiagnosis::Truth::Unknown;QJsonArray evidence;};
inline QString numericalDefinitionHash(const ConditionPack& pack){auto object=conditionPackJson(pack);object.remove("candidates");return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(object).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());}
inline bool matchesCode(const CodeRequirement& requirement,const EventRow& event){
  if(event.kind!=requirement.kind)return false;const auto code=event.code.trimmed();
  if(requirement.mode=="exact")return code==requirement.code;
  if(requirement.mode=="numeric_prefix")return code==requirement.code||code.startsWith(requirement.code+"_");
  if(requirement.mode=="action_token"){for(const auto& token:code.split(';'))if(token.trimmed()==requirement.code)return true;}
  return false;
}
inline QVector<CandidateResult> runCandidateFusion(const ConditionPack& pack,const BladeDiagnosis::Scope& actual,
             const QVector<ConditionResult>& calculations,const QString& calculationHash,const QVector<EventRow>& events,
             BladeDiagnosis::Interval window,bool includePending,bool eventsConfirmed,bool scComplete,bool actionsComplete,const std::atomic_bool* cancel=nullptr){
  using namespace BladeDiagnosis;packCheck(window.valid(),"融合时间窗口无效");QVector<CandidateResult> results;QMap<QString,ConditionResult> values;for(const auto& value:calculations)values[value.id]=value;
  QMap<QString,ConditionRule> definitions;for(const auto& rule:pack.rules)definitions[rule.id]=rule;
  const bool current=calculationHash==numericalDefinitionHash(pack);
  QSet<QString> wanted;bool wantTokens=false;
  const bool scopeMatches=pack.vendor.toStdString()==actual.vendor&&pack.model.toStdString()==actual.model&&(pack.controller.isEmpty()||pack.controller.toStdString()==actual.controller);
  if(scopeMatches&&eventsConfirmed)for(const auto& rule:pack.candidates)if(rule.state==RuleState::Validated||(includePending&&rule.state==RuleState::PendingValidation))for(const auto& term:rule.codes){wanted.insert(term.kind+"\n"+term.mode+"\n"+term.code);wantTokens|=term.mode=="action_token";}
  QMap<QString,QVector<const EventRow*>> index;
  for(const auto& event:events){if(trialCancelled(cancel))return {};if(wanted.isEmpty())break;if(!window.contains(event.aligned))continue;
    const auto code=event.code.trimmed();auto add=[&](QString mode,QString key){const auto full=event.kind+"\n"+mode+"\n"+key;if(wanted.contains(full))index[full].append(&event);};add("exact",code);
    if(event.kind=="SC")add("numeric_prefix",code.section('_',0,0));
    else if(event.kind=="ActionList"&&wantTokens){QSet<QString> tokens;for(const auto& value:code.split(';'))if(!value.trimmed().isEmpty())tokens.insert(value.trimmed());for(const auto& token:tokens)add("action_token",token);}
  }

  for(const auto& rule:pack.candidates){
    if(trialCancelled(cancel))return {};CandidateResult out;out.id=rule.id;out.title=rule.title;
    if(pack.vendor.toStdString()!=actual.vendor||pack.model.toStdString()!=actual.model||(!pack.controller.isEmpty()&&pack.controller.toStdString()!=actual.controller)){out.status="机型不适用";out.reason="厂商、机型或控制器范围不匹配";results<<out;continue;}
    if(rule.state==RuleState::Draft||rule.state==RuleState::Disabled){out.status=rule.state==RuleState::Draft?"草稿未执行":"已停用";out.reason="候选规则未执行";results<<out;continue;}
    if(rule.state!=RuleState::Validated&&!(rule.state==RuleState::PendingValidation&&includePending)){out.status="待验证未执行";out.reason="未包含待验证候选";results<<out;continue;}
    if(rule.conditions.empty()&&rule.codes.empty()){out.status="数据不足";out.reason="候选没有可执行要求";results<<out;continue;}
    std::vector<Truth> terms;bool experimental=rule.state!=RuleState::Validated;QStringList reasons;
    for(const auto& id:rule.conditions){
      Truth truth=Truth::Unknown;QString reason;const auto definition=definitions.value(id);const auto value=values.value(id);
      if(!definitions.contains(id))reason="引用的波形条件不存在";
      else if(definition.state==RuleState::Draft||definition.state==RuleState::Disabled||(definition.state==RuleState::PendingValidation&&!includePending))reason="依赖条件未启用或仍待验证";
      else if(!current)reason="当前包的条件定义已变化，需重新计算波形条件";
      else if(!values.contains(id)||!value.ran)reason="依赖条件未执行或映射缺失";
      else if(value.trial.state==TrialState::Supported){truth=Truth::Yes;reason="波形条件成立";}
      else if(value.trial.state==TrialState::NotSupported){truth=Truth::No;reason="波形条件不成立（不是无故障结论）";}
      else reason=value.reason.isEmpty()?"波形数据或参数不足":value.reason;
      experimental|=definitions.contains(id)&&definition.state!=RuleState::Validated;terms.push_back(truth);reasons<<id+"："+reason;
      out.evidence.append(QJsonObject{{"type","condition"},{"id",id},{"truth",truth==Truth::Yes?"yes":truth==Truth::No?"no":"unknown"},{"reason",reason},{"metric",std::isfinite(value.trial.metric)?QJsonValue(value.trial.metric):QJsonValue(QJsonValue::Null)},{"primary_field",value.primaryField},{"reference_field",value.referenceField}});
    }
    for(const auto& requirement:rule.codes){
      const auto hits=index.value(requirement.kind+"\n"+requirement.mode+"\n"+requirement.code);QJsonArray matched;
      const int detailLimit=qMax(1,256/qMax(1,rule.codes.size()));for(int i=0;i<qMin(hits.size(),detailLimit);++i){const auto& event=*hits[i];matched.append(QJsonObject{{"kind",event.kind},{"code",event.code},{"source",event.source},{"record",event.row},{"original_time_text",event.originalTimeText},{"original_utc",iso(event.original)},{"aligned_utc",iso(event.aligned)},{"state_raw",event.state},{"description",event.description}});}
      const bool coverage=eventsConfirmed&&(requirement.kind=="SC"?scComplete:actionsComplete);
      const auto truth=!hits.isEmpty()?Truth::Yes:coverage?Truth::No:Truth::Unknown;
      const QString reason=!eventsConfirmed?"事件日志的机组或时间定义尚未确认":!hits.isEmpty()?"命中日志记录；不推断故障持续状态":coverage?"用户确认日志完整覆盖窗口，本窗口没有匹配记录":"未命中记录且覆盖未确认，不能据此判无故障";
      terms.push_back(truth);reasons<<requirement.kind+" "+requirement.code+"："+reason;
      out.evidence.append(QJsonObject{{"type","event"},{"kind",requirement.kind},{"match",requirement.mode},{"code",requirement.code},{"semantics","record_presence"},{"events_identity_confirmed_by_user",eventsConfirmed},{"coverage_confirmed_by_user",coverage},{"truth",truth==Truth::Yes?"yes":truth==Truth::No?"no":"unknown"},{"reason",reason},{"matched",matched},{"matched_total",hits.size()},{"matched_detail_limit",detailLimit},{"matched_detail_truncated",hits.size()>detailLimit}});
    }
    out.truth=conjunction(terms);
    if(out.truth==Truth::Yes)out.status=rule.conditions.empty()?"日志要求满足（仅记录证据）":rule.codes.empty()?"波形要求满足（需核实）":"候选证据共同满足（需核实）";
    else if(out.truth==Truth::No)out.status="要求未共同满足";
    else out.status=std::find(terms.begin(),terms.end(),Truth::Yes)!=terms.end()?"部分支持 / 证据不完整":"数据不足";
    if(experimental)out.status="试验："+out.status;out.reason=reasons.join('\n');results<<out;
  }
  return results;
}
}

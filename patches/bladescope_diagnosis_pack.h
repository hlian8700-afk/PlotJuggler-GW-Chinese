#pragma once
#include <QtCore>
#include <QtEndian>
#include <cstring>
#include "bladescope_diagnosis_trial.h"
namespace BladeDiagnosisUI {
struct ConditionRule {
  QString id,title,primaryRole,referenceRole,unit,source,sourceHash,sourceVersion,reviewer,reviewDate;
  QStringList cases;int sourceRow=0;BladeDiagnosis::RuleState state=BladeDiagnosis::RuleState::Draft;
  BladeDiagnosis::TrialSpec spec;
};
struct ConditionPack {QString id,version,vendor,model,controller,sourceHash;QVector<ConditionRule> rules;};
struct RoleBinding {QString field,unit;double scale=1;int axis=0;};
struct ConditionResult {QString id,title,status,reason,primaryField,referenceField;BladeDiagnosis::TrialResult trial;bool ran=false;};
inline QString packStateName(BladeDiagnosis::RuleState state){
  using S=BladeDiagnosis::RuleState;switch(state){case S::Draft:return "draft";case S::PendingValidation:return "pending_validation";case S::Validated:return "validated";case S::Disabled:return "disabled";}return "draft";
}
inline QString packFamilyName(BladeDiagnosis::TrialFamily f){
  using F=BladeDiagnosis::TrialFamily;switch(f){case F::Overspeed:return "overspeed";case F::PitchFollowing:return "pitch_following";case F::VibrationAC:return "vibration_ac_rms";case F::SpeedOscillation:return "speed_fluctuation_rms";}return {};
}
inline void packCheck(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
inline void packKeys(const QJsonObject& object,const QStringList& allowed){for(auto i=object.begin();i!=object.end();++i)packCheck(allowed.contains(i.key()),"规则包包含未知字段；未忽略不认识的配置");}
inline QString packText(const QJsonObject& o,const char* key,bool required=true,int maximum=512){const auto v=o.value(key);packCheck(v.isString()||(!required&&v.isUndefined()),"规则包文本字段类型无效");const auto s=v.toString().trimmed();packCheck((!required||!s.isEmpty())&&s.size()<=maximum,"规则包文本字段为空或超长");return s;}
inline int packInt(const QJsonObject& o,const char* key,int fallback,int lo,int hi){const auto v=o.value(key);if(v.isUndefined())return fallback;const double n=v.toDouble(std::numeric_limits<double>::quiet_NaN());packCheck(std::isfinite(n)&&n==std::floor(n)&&n>=lo&&n<=hi,"规则包整数参数无效");return int(n);}
inline bool packBool(const QJsonObject& o,const char* key,bool fallback){const auto v=o.value(key);packCheck(v.isUndefined()||v.isBool(),"规则包布尔参数无效");return v.toBool(fallback);}
inline QJsonObject conditionJson(const ConditionRule& r){
  QJsonArray cases;for(const auto& id:r.cases)cases.append(id);
  const auto& s=r.spec;
  return {{"id",r.id},{"title",r.title},{"family",packFamilyName(s.family)},{"state",packStateName(r.state)},
    {"primary_role",r.primaryRole},{"reference_role",r.referenceRole},{"unit",r.unit},
    {"threshold",std::isfinite(s.threshold)?QJsonValue(s.threshold):QJsonValue(QJsonValue::Null)},
    {"duration_ms",double(s.durationMs)},{"maximum_gap_ms",double(s.maximumGapMs)},{"tolerance_ms",double(s.toleranceMs)},
    {"minimum_cycles",s.minimumCycles},{"merge_identical",s.mergeIdentical},{"source",r.source},{"source_row",r.sourceRow},{"source_sha256",r.sourceHash},{"source_version",r.sourceVersion},
    {"reviewer",r.reviewer},{"review_date",r.reviewDate},{"case_ids",cases}};
}
inline QJsonObject conditionPackJson(const ConditionPack& pack){QJsonArray rules;for(const auto& r:pack.rules)rules.append(conditionJson(r));
  return {{"format","bladescope.condition-pack.v1"},{"id",pack.id},{"version",pack.version},{"vendor",pack.vendor},{"model",pack.model},{"controller",pack.controller},{"conditions",rules}};
}
inline ConditionPack parseConditionPack(const QByteArray& bytes){
  packCheck(bytes.size()<=2*1024*1024,"规则包超过 2 MiB 上限");QJsonParseError error;auto document=QJsonDocument::fromJson(bytes,&error);
  packCheck(error.error==QJsonParseError::NoError&&document.isObject(),"规则包不是有效 JSON 对象");const auto root=document.object();
  packKeys(root,{"format","id","version","vendor","model","controller","conditions"});packCheck(root.value("format")=="bladescope.condition-pack.v1","规则包格式或版本不受支持");
  ConditionPack pack;pack.id=packText(root,"id");pack.version=packText(root,"version");pack.vendor=packText(root,"vendor");pack.model=packText(root,"model");pack.controller=packText(root,"controller",false);
  packCheck(root.value("conditions").isArray(),"conditions 必须是数组");const auto list=root.value("conditions").toArray();packCheck(list.size()<=64,"每包最多 64 条条件");QSet<QString> ids;QMap<QString,QString> roleUnits;
  for(const auto& item:list){
    packCheck(item.isObject(),"条件必须是对象");const auto o=item.toObject();
    packKeys(o,{"id","title","family","state","primary_role","reference_role","unit","threshold","duration_ms","maximum_gap_ms","tolerance_ms","minimum_cycles","merge_identical","source","source_row","source_sha256","source_version","reviewer","review_date","case_ids"});
    ConditionRule r;r.id=packText(o,"id");r.title=packText(o,"title");packCheck(!ids.contains(r.id),"条件 ID 重复");ids.insert(r.id);
    const auto family=packText(o,"family");const QStringList families{"overspeed","pitch_following","vibration_ac_rms","speed_fluctuation_rms"};packCheck(families.contains(family),"未知条件类型");r.spec.family=static_cast<BladeDiagnosis::TrialFamily>(families.indexOf(family));
    const auto state=packText(o,"state");const QStringList states{"draft","pending_validation","validated","disabled"};packCheck(states.contains(state),"未知生命周期状态");r.state=static_cast<BladeDiagnosis::RuleState>(states.indexOf(state));
    // Core enum order is Draft, PendingValidation, Validated, Disabled.
    r.primaryRole=packText(o,"primary_role");r.referenceRole=packText(o,"reference_role",false);r.unit=packText(o,"unit");
    const bool pitch=r.spec.family==BladeDiagnosis::TrialFamily::PitchFollowing;
    packCheck(r.spec.family==BladeDiagnosis::TrialFamily::VibrationAC?(r.unit=="m/s2"||r.unit=="mm/s"):(pitch?r.unit=="deg":r.unit=="rpm"),"目标单位与条件类型不兼容");
    if(pitch)packCheck(!r.referenceRole.isEmpty()&&r.referenceRole!=r.primaryRole,"变桨实际和指令必须使用不同角色");
    else packCheck(r.referenceRole.isEmpty(),"此条件不接受指令角色");
    for(const auto& role:pitch?QStringList{r.primaryRole,r.referenceRole}:QStringList{r.primaryRole}){
      packCheck(QRegularExpression("^[A-Za-z0-9_.-]{1,80}$").match(role).hasMatch(),"角色 ID 必须为短英文标识");
      packCheck(!roleUnits.contains(role)||roleUnits.value(role)==r.unit,"同一角色对应多个不兼容目标单位");roleUnits[role]=r.unit;
    }
    packCheck(roleUnits.size()<=32,"每包最多 32 个测点角色");
    const auto threshold=o.value("threshold");packCheck(threshold.isNull()||threshold.isDouble(),"阈值必须为数值或 null");
    r.spec.threshold=threshold.isDouble()?threshold.toDouble():std::numeric_limits<double>::quiet_NaN();
    packCheck(std::isnan(r.spec.threshold)||(std::isfinite(r.spec.threshold)&&r.spec.threshold>0),"阈值必须是有限正数");
    r.spec.durationMs=packInt(o,"duration_ms",1000,1,3600000);r.spec.maximumGapMs=packInt(o,"maximum_gap_ms",100,1,3600000);
    r.spec.toleranceMs=packInt(o,"tolerance_ms",0,0,60000);r.spec.minimumCycles=packInt(o,"minimum_cycles",2,1,1000);r.spec.mergeIdentical=packBool(o,"merge_identical",false);
    r.source=packText(o,"source",false,4096);r.sourceHash=packText(o,"source_sha256",false);r.sourceVersion=packText(o,"source_version",false);packCheck(r.sourceHash.isEmpty()||QRegularExpression("^[0-9a-fA-F]{64}$").match(r.sourceHash).hasMatch(),"来源 SHA256 摘要无效");r.sourceRow=packInt(o,"source_row",0,0,10000000);r.reviewer=packText(o,"reviewer",false);r.reviewDate=packText(o,"review_date",false);
    packCheck(o.value("case_ids").isArray()||o.value("case_ids").isUndefined(),"case_ids 必须为数组");
    for(const auto& id:o.value("case_ids").toArray()){packCheck(id.isString()&&!id.toString().trimmed().isEmpty()&&id.toString().size()<=256,"验证案例编号无效");r.cases<<id.toString();}packCheck(r.cases.size()<=100,"验证案例编号过多");
    if(r.state==BladeDiagnosis::RuleState::PendingValidation||r.state==BladeDiagnosis::RuleState::Validated)packCheck(std::isfinite(r.spec.threshold)&&!r.source.isEmpty(),"待验证/已验证条件缺少阈值或来源");
    if(r.state==BladeDiagnosis::RuleState::Validated)packCheck(!r.reviewer.isEmpty()&&QDate::fromString(r.reviewDate,Qt::ISODate).isValid()&&!r.cases.empty(),"已验证状态缺少审核人、日期或案例编号；不会自动升级状态");
    pack.rules<<r;
  }
  pack.sourceHash=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());return pack;
}
inline QMap<QString,QString> conditionRoles(const ConditionPack& pack){QMap<QString,QString> roles;for(const auto& r:pack.rules){roles[r.primaryRole]=r.unit;if(!r.referenceRole.isEmpty())roles[r.referenceRole]=r.unit;}return roles;}
inline double conditionUnitGain(QString source,QString target){
  if(source==target)return 1;
  if(source=="rad/s"&&target=="rpm")return 60./6.2831853071795864769;
  if(source=="rad"&&target=="deg")return 180./3.14159265358979323846;
  if(source=="g"&&target=="m/s2")return 9.80665;
  if(source=="m/s"&&target=="mm/s")return 1000;
  throw std::runtime_error("输入与目标单位不兼容；不跨量纲换算");
}
inline QString conditionSnapshotHash(const std::vector<BladeDiagnosis::RawSample>& data,const std::atomic_bool* cancelled=nullptr){
  static_assert(sizeof(double)==8&&std::numeric_limits<double>::is_iec559,"Snapshot fingerprint requires IEEE binary64");
  QCryptographicHash hash(QCryptographicHash::Sha256);hash.addData("bladescope-f64le-v1");QByteArray block;block.reserve(16384);
  for(const auto& sample:data){if(BladeDiagnosis::trialCancelled(cancelled))return {};for(double value:{sample.time,sample.value}){quint64 bits;std::memcpy(&bits,&value,8);bits=qToLittleEndian(bits);block.append(reinterpret_cast<const char*>(&bits),8);}if(block.size()>=16384){hash.addData(block);block.clear();}}
  hash.addData(block);return QString::fromLatin1(hash.result().toHex());
}
using ConditionSnapshots=std::map<QString,std::vector<BladeDiagnosis::RawSample>>;
inline QVector<ConditionResult> runConditionPack(const ConditionPack& pack,const BladeDiagnosis::Scope& actual,
                   const QMap<QString,RoleBinding>& bindings,const ConditionSnapshots& data,BladeDiagnosis::Interval window,
                   bool confirmed,bool includePending,const std::atomic_bool* cancelled=nullptr){
  QVector<ConditionResult> results;QMap<QString,RoleBinding> declarations;QSet<QString> conflictingFields;
  for(const auto& r:pack.rules)if(r.state==BladeDiagnosis::RuleState::Validated||(includePending&&r.state==BladeDiagnosis::RuleState::PendingValidation)){
    for(const auto& role:r.referenceRole.isEmpty()?QStringList{r.primaryRole}:QStringList{r.primaryRole,r.referenceRole}){
      const auto b=bindings.value(role);if(b.field.isEmpty())continue;
      if(declarations.contains(b.field)){const auto prior=declarations.value(b.field);if(prior.unit!=b.unit||prior.scale!=b.scale||prior.axis!=b.axis)conflictingFields.insert(b.field);}
      else declarations[b.field]=b;
    }
  }
  for(const auto& r:pack.rules){
    if(BladeDiagnosis::trialCancelled(cancelled))break;
    ConditionResult item;item.id=r.id;item.title=r.title;
    auto add=[&](QString status,QString reason){item.status=status;item.reason=reason;results<<item;};
    if(pack.vendor.toStdString()!=actual.vendor||pack.model.toStdString()!=actual.model||(!pack.controller.isEmpty()&&pack.controller.toStdString()!=actual.controller)){add("机型不适用","规则包厂商/机型/控制器与当前上下文不匹配");continue;}
    if(r.state==BladeDiagnosis::RuleState::Disabled){add("已停用","条件未执行");continue;}
    if(r.state==BladeDiagnosis::RuleState::Draft){add("草稿未执行","请先补齐阈值与来源并标为待验证");continue;}
    if(r.state==BladeDiagnosis::RuleState::PendingValidation&&!includePending){add("待验证未执行","未勾选包含待验证条件（仅试跑）");continue;}
    if(!confirmed){add("单位/映射未确认","请明确确认当前机组测点角色、单位与同轴关系");continue;}
    try{
      const auto primary=bindings.value(r.primaryRole);item.primaryField=primary.field;
      packCheck(!conflictingFields.contains(primary.field),"同一测点在多个角色中存在单位、倍率或轴号冲突");
      packCheck(!primary.field.isEmpty()&&data.count(primary.field),"主要角色未映射或测点缺失");
      auto spec=r.spec;spec.scope={pack.vendor.toStdString(),pack.model.toStdString(),pack.controller.toStdString()};spec.unitsConfirmed=true;
      packCheck(std::isfinite(primary.scale)&&primary.scale>0,"测点倍率无效");spec.gain=conditionUnitGain(primary.unit,r.unit)*primary.scale;spec.primaryAxis=primary.axis;
      std::vector<BladeDiagnosis::RawSample> empty;const auto* reference=&empty;
      if(spec.family==BladeDiagnosis::TrialFamily::PitchFollowing){
        const auto binding=bindings.value(r.referenceRole);item.referenceField=binding.field;
        packCheck(!conflictingFields.contains(binding.field),"同一指令测点在多个角色中存在单位、倍率或轴号冲突");
        packCheck(!binding.field.isEmpty()&&binding.field!=primary.field&&data.count(binding.field),"指令角色未映射、缺失或与实际测点相同");
        packCheck(std::isfinite(binding.scale)&&binding.scale>0,"指令倍率无效");spec.referenceGain=conditionUnitGain(binding.unit,r.unit)*binding.scale;spec.referenceAxis=binding.axis;reference=&data.at(binding.field);
      }
      item.trial=BladeDiagnosis::runTrial(spec,actual,window,data.at(primary.field),*reference,cancelled);item.ran=true;
      switch(item.trial.state){case BladeDiagnosis::TrialState::Supported:item.status="条件成立";break;case BladeDiagnosis::TrialState::NotSupported:item.status="条件不成立";break;
        case BladeDiagnosis::TrialState::Unknown:item.status="数据不足/质量异常";break;case BladeDiagnosis::TrialState::NotApplicable:item.status="机型不适用";break;case BladeDiagnosis::TrialState::Invalid:item.status="参数或同轴关系无效";break;}
      item.reason=QString::fromStdString(item.trial.reason);results<<item;
    }catch(const std::exception& error){add("数据不足/映射无效",QString::fromUtf8(error.what()));}
  }
  if(BladeDiagnosis::trialCancelled(cancelled))results.clear();return results;
}
}

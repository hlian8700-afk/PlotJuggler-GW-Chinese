#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace BladeDiagnosis {
enum class Truth { Unknown, No, Yes };
enum class Outcome { Supported, Partial, Conflict, Insufficient, NotApplicable };
enum class RuleState { Draft, PendingValidation, Validated, Disabled };
struct Scope {
  std::string vendor, model, controller;
  bool matches(const Scope& actual) const {
    return !vendor.empty() && vendor==actual.vendor &&
      (model.empty() || model==actual.model) &&
      (controller.empty() || controller==actual.controller);
  }
};
struct Origin {std::string file,sheet,version,sha256;std::uint64_t row=0;};
struct Interval {
  std::int64_t begin=0,end=0;
  bool valid() const{return begin<=end;}
  bool contains(std::int64_t t) const{return valid()&&begin<=t&&t<=end;}
};
inline std::int64_t alignedScTime(std::int64_t original,std::int64_t offset) {
  if((offset>0&&original>std::numeric_limits<std::int64_t>::max()-offset)||
     (offset<0&&original<std::numeric_limits<std::int64_t>::min()-offset))
    throw std::overflow_error("SC time offset overflow");
  return original+offset; // Positive offset moves SC events later; TR is unchanged.
}
struct Event {
  std::string code,id,source;
  std::int64_t originalTime=0,alignedTime=0;
  int axis=0;bool active=true;
  Origin origin;
};
struct Evidence {
  std::string feature,unit,reason;
  int axis=0;
  Truth value=Truth::Unknown;
  double measured=std::numeric_limits<double>::quiet_NaN();
  Interval interval;
  bool coverageComplete=false;
};
struct CodeTerm {std::string code;bool bindAxis=false;};
struct FeatureTerm {std::string feature;bool bindAxis=false;};
struct Rule {
  std::string id,category,candidate,advice;
  Scope scope;Origin origin;
  RuleState state=RuleState::Draft;
  int priority=0;
  std::vector<CodeTerm> allCodes;
  std::vector<FeatureTerm> allFeatures;
};
struct Match {
  std::string ruleId,candidate,advice;
  Outcome outcome=Outcome::Insufficient;
  std::vector<std::string> reasons,eventIds;
  std::vector<Evidence> evidence;
  Origin origin;
};
inline Truth conjunction(const std::vector<Truth>& values) {
  bool unknown=false;
  for(auto value:values){if(value==Truth::No)return Truth::No;unknown|=value==Truth::Unknown;}
  return unknown?Truth::Unknown:Truth::Yes;
}
inline Match evaluate(const Rule& rule,const Scope& scope,int axis,Interval window,
                      const std::vector<Event>& events,const std::vector<Evidence>& evidence,
                      bool scCoverageComplete) {
  Match out;out.ruleId=rule.id;out.candidate=rule.candidate;out.advice=rule.advice;out.origin=rule.origin;
  if(!window.valid())throw std::invalid_argument("invalid analysis window");
  if(rule.state!=RuleState::Validated || !rule.scope.matches(scope)){
    out.outcome=Outcome::NotApplicable;
    out.reasons.push_back(rule.state!=RuleState::Validated?"Rule is not validated":"Vendor/model/controller does not match");return out;
  }
  if(rule.allCodes.empty()&&rule.allFeatures.empty()){
    out.reasons.push_back("Rule has no executable predicates");return out;
  }
  std::vector<Truth> conditions;
  for(const auto& term:rule.allCodes){
    if(term.bindAxis&&(axis<1||axis>3)){conditions.push_back(Truth::Unknown);out.reasons.push_back("Blade axis is not specified");continue;}
    const auto found=std::find_if(events.begin(),events.end(),[&](const Event& event){
      return event.active&&event.code==term.code&&window.contains(event.alignedTime)&&(!term.bindAxis||event.axis==axis);
    });
    if(found==events.end()){
      conditions.push_back(scCoverageComplete?Truth::No:Truth::Unknown);
      out.reasons.push_back("Code missing: "+term.code+(scCoverageComplete?"":"; SC coverage incomplete"));
    }else {conditions.push_back(Truth::Yes);out.eventIds.push_back(found->id);}
  }
  for(const auto& term:rule.allFeatures){
    std::vector<Evidence> relevant;
    for(const auto& item:evidence)
      if(item.feature==term.feature&&(!term.bindAxis||(axis>=1&&axis<=3&&item.axis==axis))&&
         item.interval.valid()&&item.interval.begin<=window.end&&item.interval.end>=window.begin)relevant.push_back(item);
    if(relevant.empty()){conditions.push_back(Truth::Unknown);out.reasons.push_back("Feature unavailable: "+term.feature);continue;}
    bool yes=false,no=false,unknown=false;
    for(const auto& item:relevant){
      out.evidence.push_back(item);
      if(item.value==Truth::Unknown||!item.coverageComplete){unknown=true;out.reasons.push_back(item.reason.empty()?"Incomplete feature coverage: "+term.feature:item.reason);}
      else if(item.value==Truth::Yes)yes=true;else no=true;
    }
    if(yes&&no){out.outcome=Outcome::Conflict;out.reasons.push_back("Conflicting feature evidence: "+term.feature);return out;}
    conditions.push_back(yes?Truth::Yes:unknown?Truth::Unknown:Truth::No);
    if(no&&!yes&&!unknown)out.reasons.push_back("Feature not supported: "+term.feature);
  }
  const auto result=conjunction(conditions);
  if(result==Truth::Yes)out.outcome=Outcome::Supported;
  else if(result==Truth::No){
    // A disproved candidate is not a cross-sensor contradiction.
    out.outcome=Outcome::NotApplicable;
  }else{
    out.outcome=std::find(conditions.begin(),conditions.end(),Truth::Yes)!=conditions.end()?Outcome::Partial:Outcome::Insufficient;
  }
  return out;
}
struct TimedPredicate {std::int64_t time;Truth value;};
// Sample-and-hold semantics. Unknown values and excessive gaps break continuity.
inline Truth sustained(const std::vector<TimedPredicate>& values,std::int64_t duration,std::int64_t maxGap) {
  if(duration<0||maxGap<=0)throw std::invalid_argument("invalid duration/gap");
  if(values.empty())return Truth::Unknown;
  for(size_t i=1;i<values.size();++i)if(values[i].time<=values[i-1].time)
    throw std::invalid_argument("timestamps must be strictly increasing");
  bool uncertain=false;std::optional<std::int64_t> begin;
  for(size_t i=0;i<values.size();++i){
    if(i&&static_cast<std::uint64_t>(values[i].time)-static_cast<std::uint64_t>(values[i-1].time)>static_cast<std::uint64_t>(maxGap)){begin.reset();uncertain=true;}
    if(values[i].value==Truth::Unknown){begin.reset();uncertain=true;continue;}
    if(values[i].value==Truth::No){begin.reset();continue;}
    if(!begin)begin=values[i].time;
    if(static_cast<std::uint64_t>(values[i].time)-static_cast<std::uint64_t>(*begin)>=static_cast<std::uint64_t>(duration))return Truth::Yes;
  }
  return uncertain?Truth::Unknown:Truth::No;
}
struct FileSlice {std::string path,kind;Interval coverage;};
struct Coverage {bool complete=false;std::vector<FileSlice> slices;std::vector<Interval> gaps;};
// Coverage refers to file time spans; signal-level gaps are tracked separately.
inline Coverage selectFiles(std::vector<FileSlice> files,Interval window) {
  if(!window.valid())throw std::invalid_argument("invalid window");
  if(!files.empty())for(const auto& file:files)if(file.kind!=files.front().kind)throw std::invalid_argument("Select TR/SC coverage separately");
  std::sort(files.begin(),files.end(),[](const auto& a,const auto& b){return a.coverage.begin<b.coverage.begin;});
  Coverage result;auto cursor=window.begin;
  for(const auto& file:files){
    if(!file.coverage.valid()||file.coverage.end<window.begin||file.coverage.begin>window.end)continue;
    if(file.coverage.begin>cursor)result.gaps.push_back({cursor,file.coverage.begin});
    result.slices.push_back(file);cursor=std::max(cursor,std::min(window.end,file.coverage.end));
  }
  if(result.slices.empty()||cursor<window.end)result.gaps.push_back({cursor,window.end});
  result.complete=!result.slices.empty()&&result.gaps.empty();return result;
}
} // namespace BladeDiagnosis

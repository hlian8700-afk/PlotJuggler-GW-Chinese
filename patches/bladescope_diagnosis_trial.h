#pragma once
#include "bladescope_diagnosis_core.h"
#include <atomic>
namespace BladeDiagnosis {
enum class TrialFamily { Overspeed, PitchFollowing, VibrationAC, SpeedOscillation };
enum class TrialState { Supported, NotSupported, Unknown, NotApplicable, Invalid };
struct TrialSpec {
  TrialFamily family=TrialFamily::Overspeed;
  Scope scope;double threshold=std::numeric_limits<double>::quiet_NaN();
  std::int64_t durationMs=1000,maximumGapMs=100,toleranceMs=0;
  int minimumCycles=2,primaryAxis=0,referenceAxis=0;bool mergeIdentical=false,unitsConfirmed=false;
  double gain=1,referenceGain=1;
};
struct TrialResult {
  TrialState state=TrialState::Unknown;double metric=std::numeric_limits<double>::quiet_NaN();
  double longestSeconds=0;std::size_t samples=0,merged=0,referenceMerged=0,unmatched=0;
  int cycles=0;WindowQuality quality,referenceQuality;std::string reason;
};
inline bool trialCancelled(const std::atomic_bool* flag){return flag&&flag->load();}
inline std::vector<RawSample> trialNormalize(const std::vector<RawSample>& input,bool merge,std::size_t& removed){
  std::vector<RawSample> out;out.reserve(input.size());
  for(const auto& sample:input){
    if(merge&&!out.empty()&&std::isfinite(sample.time)&&std::isfinite(sample.value)&&
       sample.time==out.back().time&&sample.value==out.back().value){++removed;continue;}
    out.push_back(sample);
  }
  return out;
}
inline TrialResult runTrial(const TrialSpec& spec,const Scope& actual,Interval window,
                           const std::vector<RawSample>& original,const std::vector<RawSample>& reference={},
                           const std::atomic_bool* cancel=nullptr){
  TrialResult out;
  if(spec.scope.vendor.empty()||spec.scope.model.empty()||!spec.scope.matches(actual)){
    out.state=TrialState::NotApplicable;out.reason="Vendor/model scope is missing or does not match";return out;
  }
  if(static_cast<int>(spec.family)<0||static_cast<int>(spec.family)>3){out.state=TrialState::Invalid;out.reason="Unknown trial family";return out;}
  if(spec.family==TrialFamily::PitchFollowing&&(spec.primaryAxis<1||spec.primaryAxis>3||spec.referenceAxis!=spec.primaryAxis)){out.state=TrialState::Invalid;out.reason="Pitch actual and command must be mapped to the same confirmed blade axis";return out;}
  if(!window.valid()||window.begin==window.end||!spec.unitsConfirmed||!std::isfinite(spec.threshold)||spec.threshold<=0||
     !std::isfinite(spec.gain)||spec.gain<=0||!std::isfinite(spec.referenceGain)||spec.referenceGain<=0||
     spec.maximumGapMs<=0||spec.durationMs<=0||spec.toleranceMs<0||spec.minimumCycles<1){
    out.state=TrialState::Invalid;out.reason="Confirm units, positive threshold/gains, window and timing parameters";return out;
  }
  if(trialCancelled(cancel)){out.reason="Cancelled";return out;}
  auto primary=trialNormalize(original,spec.mergeIdentical,out.merged);
  out.quality=inspectWindow(primary,window,spec.maximumGapMs);
  if(!out.quality.ready){out.reason="Primary data missing, truncated, invalid, duplicated, reversed or gapped";return out;}
  std::vector<RawSample> other;
  if(spec.family==TrialFamily::PitchFollowing){
    other=trialNormalize(reference,spec.mergeIdentical,out.referenceMerged);
    out.referenceQuality=inspectWindow(other,window,spec.maximumGapMs);
    if(!out.referenceQuality.ready){out.reason="Reference data quality is insufficient";return out;}
    for(std::size_t i=1;i<other.size();++i)if(other[i].time<=other[i-1].time){out.reason="Reference boundary timestamps must be strictly increasing";return out;}
  }
  const double lo=window.begin/1000.,hi=window.end/1000.;std::vector<RawSample> samples;
  std::size_t cursor=0;
  for(const auto& sample:primary){
    if((samples.size()%8192)==0&&trialCancelled(cancel)){out.reason="Cancelled";return out;}
    if(sample.time<lo||sample.time>hi)continue;
    double value=sample.value*spec.gain;
    if(spec.family==TrialFamily::PitchFollowing){
      while(cursor+1<other.size()&&other[cursor+1].time<=sample.time)++cursor;
      auto chosen=cursor;
      if(cursor+1<other.size()&&std::abs(other[cursor+1].time-sample.time)<std::abs(other[cursor].time-sample.time))chosen=cursor+1;
      if(std::abs(other[chosen].time-sample.time)>spec.toleranceMs/1000.+1e-6){++out.unmatched;continue;}
      value=std::abs(value-other[chosen].value*spec.referenceGain);
    }
    if(!std::isfinite(value)){out.reason="Converted value overflow";return out;}
    samples.push_back({sample.time,value});
  }
  out.samples=samples.size();
  if(out.unmatched||samples.size()<2){out.reason="Unmatched reference timestamps or insufficient samples";return out;}
  if(spec.family==TrialFamily::Overspeed||spec.family==TrialFamily::PitchFollowing){
    std::vector<TimedPredicate> predicates;std::optional<std::int64_t> begin;std::int64_t previous=0;
    out.metric=-std::numeric_limits<double>::infinity();
    for(const auto& sample:samples){
      if(std::abs(sample.time)>253402300799.){out.reason="Timestamp outside supported range";return out;}
      const auto time=static_cast<std::int64_t>(std::llround(sample.time*1000));
      if(!predicates.empty()&&time<=previous){out.reason="Timestamps cannot be represented as unique milliseconds";return out;}
      previous=time;const bool high=sample.value>spec.threshold;out.metric=std::max(out.metric,sample.value);
      predicates.push_back({time,high?Truth::Yes:Truth::No});
      if(high){if(!begin)begin=time;out.longestSeconds=std::max(out.longestSeconds,(time-*begin)/1000.);}else begin.reset();
    }
    out.state=sustained(predicates,spec.durationMs,spec.maximumGapMs)==Truth::Yes?TrialState::Supported:TrialState::NotSupported;
  }else{
    long double mean=0,meanT=0;const auto origin=samples.front().time;
    for(const auto& sample:samples){mean+=sample.value;meanT+=sample.time-origin;}
    mean/=samples.size();meanT/=samples.size();long double covariance=0,varianceT=0;
    if(spec.family==TrialFamily::SpeedOscillation)for(const auto& sample:samples){
      const long double dt=sample.time-origin-meanT;covariance+=dt*(sample.value-mean);varianceT+=dt*dt;
    }
    const auto slope=varianceT>0?covariance/varianceT:0;long double sum=0;int previousSign=0,crossings=0;
    const double deadband=spec.threshold*.05;
    for(const auto& sample:samples){
      if(trialCancelled(cancel)){out.reason="Cancelled";return out;}
      const auto residual=sample.value-mean-slope*(sample.time-origin-meanT);sum+=residual*residual;
      const int sign=residual>deadband?1:residual< -deadband?-1:0;
      if(sign){if(previousSign&&sign!=previousSign)++crossings;previousSign=sign;}
    }
    out.metric=std::sqrt(static_cast<double>(sum/samples.size()));out.cycles=crossings/2;
    if(!std::isfinite(out.metric)){out.reason="RMS calculation overflow";return out;}
    const bool supported=out.metric>spec.threshold&&(spec.family!=TrialFamily::SpeedOscillation||out.cycles>=spec.minimumCycles);
    out.state=supported?TrialState::Supported:TrialState::NotSupported;
  }
  out.reason="Experimental condition only; not a validated turbine root cause";return out;
}
}

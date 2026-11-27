#include "bladescope_diagnosis_trial.h"
#include <cassert>
using namespace BladeDiagnosis;
int main(){
 Scope scope{"demo","model",""};TrialSpec spec;spec.scope=scope;spec.unitsConfirmed=true;spec.threshold=100;spec.durationMs=500;spec.maximumGapMs=20;
 std::vector<RawSample> a,b;for(int i=0;i<=100;++i){a.push_back({i*.01,120});b.push_back({i*.01,110});}
 auto r=runTrial(spec,scope,{0,1000},a);assert(r.state==TrialState::Supported&&r.longestSeconds==1);
 auto wrong=scope;wrong.vendor="other";assert(runTrial(spec,wrong,{0,1000},a).state==TrialState::NotApplicable);
 spec.unitsConfirmed=false;assert(runTrial(spec,scope,{0,1000},a).state==TrialState::Invalid);spec.unitsConfirmed=true;
 a.insert(a.begin()+1,a.front());assert(runTrial(spec,scope,{0,1000},a).state==TrialState::Unknown);
 spec.mergeIdentical=true;r=runTrial(spec,scope,{0,1000},a);assert(r.state==TrialState::Supported&&r.merged==1&&a.size()==102);
 a[1].value=999;assert(runTrial(spec,scope,{0,1000},a).state==TrialState::Unknown);a.erase(a.begin()+1);
 spec.family=TrialFamily::PitchFollowing;spec.threshold=5;
 assert(runTrial(spec,scope,{0,1000},a,b).state==TrialState::Invalid);spec.primaryAxis=1;spec.referenceAxis=2;assert(runTrial(spec,scope,{0,1000},a,b).state==TrialState::Invalid);spec.referenceAxis=1;assert(runTrial(spec,scope,{0,1000},a,b).state==TrialState::Supported);
 for(auto& s:b)s.time+=.002;
 assert(runTrial(spec,scope,{10,990},a,b).state==TrialState::Unknown);spec.toleranceMs=3;assert(runTrial(spec,scope,{10,990},a,b).state==TrialState::Supported);
 spec.family=TrialFamily::VibrationAC;spec.threshold=1;r=runTrial(spec,scope,{0,1000},a);assert(r.state==TrialState::NotSupported&&r.metric==0);
 a.clear();for(int i=0;i<=1000;++i)a.push_back({i*.01,100+3*std::sin(i*.01*6.283185307179586)});
 spec.family=TrialFamily::VibrationAC;spec.threshold=1;r=runTrial(spec,scope,{0,10000},a);assert(r.state==TrialState::Supported&&r.metric>2.1&&r.metric<2.13);
 spec.family=TrialFamily::SpeedOscillation;spec.threshold=1;spec.minimumCycles=3;r=runTrial(spec,scope,{0,10000},a);assert(r.state==TrialState::Supported&&r.cycles>=8);
 for(auto& s:a)s.value=100+20*s.time;
 r=runTrial(spec,scope,{0,10000},a);assert(r.state==TrialState::NotSupported&&r.metric<1e-10);
 assert(runTrial(spec,scope,{-1000,10000},a).state==TrialState::Unknown);
 std::atomic_bool cancelled{true};assert(runTrial(spec,scope,{0,10000},a,{},&cancelled).state==TrialState::Unknown);
}

#include "bladescope_diagnosis_core.h"
#include <cassert>
#include <iostream>
using namespace BladeDiagnosis;
int main(){
 assert(alignedScTime(1000,250)==1250);assert(alignedScTime(1000,-250)==750);
 bool overflow=false;try{alignedScTime(INT64_MAX,1);}catch(const std::overflow_error&){overflow=true;}assert(overflow);
 Scope gw{"Goldwind","GW221","v1"},my{"Mingyang","MY2","v1"};
 Rule r;r.id="test";r.scope=gw;r.state=RuleState::Validated;
 r.allCodes={{"following",true},{"drive",true}};r.allFeatures={{"lag",true}};
 Event a;a.code="following";a.id="e1";a.axis=1;a.alignedTime=100;
 Event b=a;b.code="drive";b.id="e2";b.axis=2;
 Evidence lag;lag.feature="lag";lag.axis=1;lag.value=Truth::Yes;lag.interval={90,120};lag.coverageComplete=true;
 auto result=evaluate(r,gw,1,{80,130},{a,b},{lag},true);
 assert(result.outcome==Outcome::NotApplicable); // Different blades must not form an AND.
 b.axis=1;assert(evaluate(r,gw,1,{80,130},{a,b},{lag},true).outcome==Outcome::Supported);
 assert(evaluate(r,my,1,{80,130},{a,b},{lag},true).outcome==Outcome::NotApplicable);
 assert(evaluate(r,gw,1,{80,130},{a,b},{},true).outcome==Outcome::Partial);
 lag.coverageComplete=false;assert(evaluate(r,gw,1,{80,130},{a,b},{lag},true).outcome==Outcome::Partial);
 lag.coverageComplete=true;auto contradictory=lag;contradictory.value=Truth::No;
 assert(evaluate(r,gw,1,{80,130},{a,b},{lag,contradictory},true).outcome==Outcome::Conflict);
 r.state=RuleState::Draft;assert(evaluate(r,gw,1,{80,130},{a,b},{lag},true).outcome==Outcome::NotApplicable);
 assert(sustained({{0,Truth::Yes},{400,Truth::Yes},{1100,Truth::Yes}},1000,800)==Truth::Yes);
 assert(sustained({{0,Truth::Yes},{1100,Truth::Yes}},1000,500)==Truth::Unknown);
 assert(sustained({{0,Truth::Yes},{400,Truth::No},{1100,Truth::Yes}},1000,800)==Truth::No);
 bool duplicate=false;try{sustained({{0,Truth::Yes},{0,Truth::Yes}},1,100);}catch(const std::invalid_argument&){duplicate=true;}assert(duplicate);
 auto spans=selectFiles({{"day2","SC",{100,200}},{"day1","SC",{0,100}}},{80,150});
 assert(spans.complete&&spans.slices.size()==2);
 assert(!selectFiles({{"day1","SC",{0,100}},{"day2","SC",{120,200}}},{80,150}).complete);
 assert(!selectFiles({}, {80,150}).complete);
 std::cout<<"Diagnosis scope, same-axis matching, tri-state evidence, time alignment, duration and cross-file coverage passed\n";
}

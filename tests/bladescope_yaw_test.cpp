#include "bladescope_yaw_analysis.h"
#include <cassert>
#include <iostream>
using namespace BladeFit;
int main()
{
  const auto day=[](double t){return static_cast<long long>(t/86400)+1;};
  Params p;p.tolerance=0;
  BladeYaw::Config c;c.minSamples=2;
  auto fixture=[&](double offset){
    Channels ch;double t=0;
    auto add=[&](double wind,double angle,int count){for(int n=0;n<count;++n){
      const double power=wind<7?500:1000;
      ch[Wind].push_back({t,wind});ch[Yaw].push_back({t,angle});ch[Power].push_back({t,power+(angle>0?offset:0)});++t;
    }};
    for(double wind:{6.5,7.5}){
      add(wind,0,3);
      for(double angle:{4.,5.,6.,7.,8.}){add(wind,angle,wind<7?4:2);add(wind,-angle,wind<7?2:5);}
    }
    return ch;
  };
  auto same=BladeYaw::analyze(fixture(0),p,c,{},day);
  auto equal=BladeYaw::aggregate(same);assert(equal.weight&&std::abs(equal.delta)<1e-10);
  auto result=BladeYaw::analyze(fixture(40),p,c,{},day);
  auto summary=BladeYaw::aggregate(result);assert(std::abs(summary.delta-40)<1e-9);
  assert(std::abs(summary.windDelta)<1e-12);assert(std::abs(summary.residualDelta-40)<1e-9);
  assert(result.zeroAngle==6&&result.pairCandidates+result.zeroAngle==result.accepted);
  assert(BladeYaw::index(4,4,8,1)==0&&BladeYaw::index(5,4,8,1)==1&&BladeYaw::index(8,4,8,1)==3);
  assert(BladeYaw::normalize(180)==-180&&BladeYaw::normalize(540)==-180);
  assert(BladeYaw::normalize(-540)==-180&&!std::signbit(BladeYaw::normalize(-0.)));
  size_t sum=result.accepted;for(auto n:result.rejected)sum+=n;assert(sum==result.input);
  c.minBaselinePower=2000;auto low=BladeYaw::analyze(fixture(40),p,c,{},day);
  assert(low.excludedPercent==low.accepted&&std::isfinite(low.samples.front().residual));
  c.minBaselinePower=100;c.mirror=false;c.negativeLow=-9;c.negativeHigh=-5;
  auto custom=fixture(40);for(auto& angle:custom[Yaw])if(angle.y<0)angle.y-=1;
  auto asymmetric=BladeYaw::analyze(custom,p,c,{},day);
  assert(std::abs(BladeYaw::aggregate(asymmetric).delta-40)<1e-9);
  c.mirror=true;
  Channels missingBaseline=fixture(0);for(auto& angle:missingBaseline[Yaw])if(angle.y==0)angle.y=2.5;
  auto noBase=BladeYaw::analyze(missingBaseline,p,c,{},day);assert(noBase.missingBaseline==noBase.accepted);
  assert(BladeYaw::aggregate(noBase).weight); // Raw-power pairing survives missing residual baselines.
  Channels single=fixture(0);for(auto& angle:single[Yaw])if(angle.y<0)angle.y=1;
  auto oneSide=BladeYaw::analyze(single,p,c,{},day);assert(!BladeYaw::aggregate(oneSide).weight&&oneSide.unmatched);
  Channels invalid=fixture(0);invalid[Wind][0].y=missing();invalid[Power][1].x+=.25;
  auto rejected=BladeYaw::analyze(invalid,p,c,{},day);
  assert(rejected.rejected[BladeYaw::Nonfinite]==1&&rejected.rejected[BladeYaw::Alignment]==1);
  c.baselineMode=1;p.reference={{6,500},{9,2000}};
  auto standard=BladeYaw::analyze(fixture(0),p,c,{},day);assert(standard.missingBaseline==0);
  std::atomic_bool stop{true};bool canceled=false;
  try{BladeYaw::analyze(fixture(0),p,c,{},day,&stop);}catch(const std::runtime_error&){canceled=true;}assert(canceled);
  c.pairStep=1.5;bool bad=false;try{BladeYaw::validate(c,p);}catch(const std::runtime_error&){bad=true;}assert(bad);
  std::cout<<"M56 residual, mirrored/custom boundaries, weighted matching, missing data and cancellation passed\n";
}

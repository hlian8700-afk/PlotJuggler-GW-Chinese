#pragma once
#include "bladescope_fit_math.h"
#include <tuple>
#include <functional>

namespace BladeYaw
{
using namespace BladeFit;
struct Config
{
  double windLow=6,windHigh=9,windStep=1,angleLow=-20,angleHigh=20,angleStep=1;
  double baselineLow=-2,baselineHigh=2,minBaselinePower=100;
  double positiveLow=4,positiveHigh=8,negativeLow=-8,negativeHigh=-4,pairStep=1;
  double physicalWindHigh=60,physicalPowerLow=0,physicalPowerHigh=20000;
  double deadband=1,minCoverage=.5,mixedFraction=.3;
  int minSamples=20,baselineMode=0;
  bool mirror=true,poolDates=false,groupDates=true,rejectYaw=false;
};
enum Rejection { Time,Alignment,Nonfinite,Physical,State,Fault,Curtailment,Transition,YawActive,Window,RejectionCount };
inline const char* reasonName(int i)
{
  static const char* names[]={"时间无效或范围外","时间匹配失败","必需信号非有限值","物理范围外",
    "运行状态不符","故障或标志无效","限功率或标志无效","启停或标志无效","偏航活动或标志无效","分析窗口外"};
  return names[i];
}
struct Sample
{
  double time,wind,angle,power,baseline=missing(),residual=missing(),percent=missing();
  long long day;int windBin;SampleId id;
};
struct Stats
{
  Bin wind,power,residual,percent,angle;
  void add(const Sample& s){
    wind.add(0,s.wind);power.add(0,s.power);angle.add(0,s.angle);
    if(std::isfinite(s.residual))residual.add(0,s.residual);
    if(std::isfinite(s.percent))percent.add(0,s.percent);
  }
};
using BaseKey=std::pair<long long,int>;
using CellKey=std::tuple<long long,int,int>;
struct Cell
{
  long long day;int windBin,pairBin;Stats positive,negative;
  size_t weight=0;std::string reason;
};
struct ResidualBin
{
  long long day;int windBin,angleBin;Stats stats;
};
struct Result
{
  Config config;Params filters;
  size_t input=0,accepted=0,missingBaseline=0,sparseBaseline=0,excludedPercent=0;
  size_t zeroAngle=0,outsidePairs=0,pairCandidates=0,unmatched=0;
  std::array<size_t,RejectionCount> rejected{};
  std::vector<Sample> samples;
  std::vector<size_t> display;
  std::map<BaseKey,Bin> baselines;
  std::vector<Cell> cells;
  std::vector<ResidualBin> bins;
  std::set<long long> days;
  std::vector<std::string> unavailable;
  double first=missing(),last=missing();
};
inline void canceled(const std::atomic_bool* flag){if(flag&&flag->load())throw std::runtime_error("计算已取消");}
inline double normalize(double angle)
{
  double value=std::fmod(angle+180.,360.);if(value<0)value+=360.;value-=180.;
  return value==0?0:value;
}
inline int binCount(double lo,double hi,double step,bool exact=false)
{
  if(!std::isfinite(lo)||!std::isfinite(hi)||!std::isfinite(step)||step<=0||hi<=lo)
    throw std::runtime_error("M56 区间和步长须为有效数值，且下限小于上限");
  const double count=(hi-lo)/step;
  if(count>10000)throw std::runtime_error("M56 单维箱数不能超过 10000");
  if(exact&&std::abs(count-std::round(count))>1e-7)
    throw std::runtime_error("配对区间宽度须为细分步长的整数倍");
  return int(std::ceil(count-1e-10));
}
inline int index(double value,double lo,double hi,double step)
{
  if(value<lo||value>hi)return -1;
  return std::min(binCount(lo,hi,step)-1,int(std::floor((value-lo)/step+1e-10)));
}
inline std::pair<double,double> negativeRange(const Config& c)
{return c.mirror?std::make_pair(-c.positiveHigh,-c.positiveLow):std::make_pair(c.negativeLow,c.negativeHigh);}
inline void validate(const Config& c,const Params& p)
{
  binCount(c.windLow,c.windHigh,c.windStep);binCount(c.angleLow,c.angleHigh,c.angleStep);
  binCount(c.positiveLow,c.positiveHigh,c.pairStep,true);
  auto negative=negativeRange(c);binCount(negative.first,negative.second,c.pairStep,true);
  if(c.windLow<=0||c.angleLow< -180||c.angleHigh>=180||c.positiveLow<0||c.positiveHigh>=180||
     negative.first< -180||negative.second>0||
     std::abs((c.positiveHigh-c.positiveLow)-(negative.second-negative.first))>1e-6)
    throw std::runtime_error("请检查风速范围、正负配对区间方向和等宽关系");
  if(c.positiveLow<c.angleLow||c.positiveHigh>c.angleHigh||negative.first<c.angleLow||negative.second>c.angleHigh)
    throw std::runtime_error("配对区间须位于分析角度范围内");
  if(!std::isfinite(c.baselineLow)||!std::isfinite(c.baselineHigh)||c.baselineLow>c.baselineHigh||
     c.baselineLow<c.angleLow||c.baselineHigh>c.angleHigh||c.baselineMode<0||c.baselineMode>1||
     c.minSamples<1||!std::isfinite(c.minBaselinePower)||c.minBaselinePower<0||
     !std::isfinite(c.physicalWindHigh)||c.physicalWindHigh<c.windHigh||
     !std::isfinite(c.physicalPowerLow)||!std::isfinite(c.physicalPowerHigh)||c.physicalPowerHigh<=c.physicalPowerLow||
     !std::isfinite(c.deadband)||c.deadband<0||!std::isfinite(c.minCoverage)||c.minCoverage<0||c.minCoverage>1||
     !std::isfinite(c.mixedFraction)||c.mixedFraction<=0||c.mixedFraction>.5||
     !std::isfinite(p.tolerance)||p.tolerance<0||p.end<p.start)
    throw std::runtime_error("请检查 M56 基线、样本数、物理范围、时间和结论阈值");
  if(c.baselineMode==1){
    if(p.reference.size()<2)throw std::runtime_error("请先导入标准功率曲线");
    for(size_t i=0;i<p.reference.size();++i)
      if(!std::isfinite(p.reference[i].x)||!std::isfinite(p.reference[i].y)||
         (i&&p.reference[i].x<=p.reference[i-1].x))throw std::runtime_error("标准功率曲线须按风速严格递增");
  }
}
struct Aggregate
{
  double positive=0,negative=0,delta=0,windDelta=0,residualDelta=missing(),percentDelta=missing(),coverage=0;
  size_t weight=0,eligible=0,candidates=0,matched=0,positiveCells=0,negativeCells=0;
  std::string outcome;
};
inline Aggregate aggregate(const Result& r,int windBand=-1,int pairBand=-1)
{
  Aggregate a;double residualSum=0,percentSum=0;size_t residualWeight=0,percentWeight=0;
  for(const auto& c:r.cells){
    if((windBand>=0&&c.windBin!=windBand)||(pairBand>=0&&c.pairBin!=pairBand))continue;
    a.candidates+=c.positive.power.n+c.negative.power.n;
    if(!c.weight)continue;
    ++a.eligible;a.matched+=c.positive.power.n+c.negative.power.n;a.weight+=c.weight;
    const double delta=c.positive.power.y-c.negative.power.y;
    a.positive+=c.weight*c.positive.power.y;a.negative+=c.weight*c.negative.power.y;
    a.windDelta+=c.weight*(c.positive.wind.y-c.negative.wind.y);
    a.positiveCells+=delta>r.config.deadband;a.negativeCells+=delta< -r.config.deadband;
    if(c.positive.residual.n==c.positive.power.n&&c.negative.residual.n==c.negative.power.n){
      residualSum+=c.weight*(c.positive.residual.y-c.negative.residual.y);residualWeight+=c.weight;
    }
    if(c.positive.percent.n==c.positive.power.n&&c.negative.percent.n==c.negative.power.n){
      percentSum+=c.weight*(c.positive.percent.y-c.negative.percent.y);percentWeight+=c.weight;
    }
  }
  a.coverage=a.candidates?double(a.matched)/a.candidates:0;
  if(a.weight){a.positive/=a.weight;a.negative/=a.weight;a.windDelta/=a.weight;a.delta=a.positive-a.negative;}
  if(residualWeight)a.residualDelta=residualSum/residualWeight;
  if(percentWeight)a.percentDelta=percentSum/percentWeight;
  if(!a.weight||a.coverage<r.config.minCoverage)a.outcome="覆盖不足";
  else if(a.positiveCells>=a.eligible*r.config.mixedFraction&&a.negativeCells>=a.eligible*r.config.mixedFraction)a.outcome="不同运行条件下结果不一致";
  else if(a.delta>r.config.deadband)a.outcome="正侧平均功率较高";
  else if(a.delta< -r.config.deadband)a.outcome="负侧平均功率较高";
  else a.outcome="功率差处于死区内";
  return a;
}
template<class Day> Result analyze(const Channels& channels,const Params& p,const Config& c,
                                   const Masks& excluded,Day day,const std::atomic_bool* stop=nullptr)
{
  validate(c,p);canceled(stop);Result r;r.config=c;r.filters=p;
  for(int role:{int(Wind),int(Power),int(Yaw)})if(channels[role].empty())throw std::runtime_error("M56 需要风速、有功功率和带符号对风角度");
  for(const auto& channel:channels)
    for(size_t i=1;i<channel.size();++i)
      if(!std::isfinite(channel[i].x)||channel[i].x<channel[i-1].x)throw std::runtime_error("测点时间须有限且非递减，请重新导入排序");
  if(p.filterState&&channels[Status].empty())throw std::runtime_error("已启用状态筛选，请关联运行状态");
  if(c.rejectYaw&&channels[YawActiveFlag].empty())throw std::runtime_error("请关联偏航运行信号");
  for(int role:{int(FaultFlag),int(CurtailFlag),int(TransitionFlag)})
    if(channels[role].empty())r.unavailable.push_back(std::string(role==FaultFlag?"故障":role==CurtailFlag?"限功率":"启停")+"标志未关联");
  if(!c.rejectYaw)r.unavailable.push_back("未启用偏航活动筛选");
  std::array<size_t,NRoles> cursors{};
  for(size_t i=0;i<channels[Wind].size();++i){
    if(i%4096==0)canceled(stop);
    ++r.input;
    const auto anchor=channels[Wind][i];const double t=anchor.x;
    if(!std::isfinite(t)||t<p.start||t>p.end||excluded.count({Wind,i})){++r.rejected[Time];continue;}
    auto read=[&](int role){return role==Wind?anchor.y:match(channels[role],cursors[role],t,p.tolerance,role>=Status);};
    auto aligned=[&](int role){
      read(role);size_t n=cursors[role];const auto& ch=channels[role];
      if(n&& (n==ch.size()|| (ch[n].x!=t && t-ch[n-1].x<=ch[n].x-t)))--n;
      return n<ch.size()&&std::abs(ch[n].x-t)<=p.tolerance;
    };
    if(!aligned(Power)||!aligned(Yaw)){++r.rejected[Alignment];continue;}
    const double wind=anchor.y,power=read(Power),rawAngle=read(Yaw);
    if(!std::isfinite(wind)||!std::isfinite(power)||!std::isfinite(rawAngle)){++r.rejected[Nonfinite];continue;}
    const double angle=normalize(rawAngle);
    bool physical=wind<0||wind>c.physicalWindHigh||power<c.physicalPowerLow||power>c.physicalPowerHigh;
    for(const auto& limit:p.limits){const double v=read(limit.first);physical|=!std::isfinite(v)||v<limit.second.first||v>limit.second.second;}
    if(physical){++r.rejected[Physical];continue;}
    if(p.filterState&&read(Status)!=p.normal){++r.rejected[State];continue;}
    bool flagged=false;
    if(p.rejectFlags)for(int role:{int(FaultFlag),int(CurtailFlag),int(TransitionFlag)}){
      if(channels[role].empty())continue;
      if(read(role)!=0){++r.rejected[role==FaultFlag?Fault:role==CurtailFlag?Curtailment:Transition];flagged=true;break;}
    }
    if(flagged)continue;
    if(c.rejectYaw&&read(YawActiveFlag)!=0){++r.rejected[YawActive];continue;}
    if(wind<c.windLow||wind>c.windHigh||angle<c.angleLow||angle>c.angleHigh){++r.rejected[Window];continue;}
    Sample s{t,wind,angle,power,missing(),missing(),missing(),day(t),index(wind,c.windLow,c.windHigh,c.windStep),{Wind,i}};
    r.samples.push_back(s);r.days.insert(s.day);++r.accepted;
    if(!std::isfinite(r.first))r.first=t;
    r.last=t;
    if(c.baselineMode==0&&angle>=c.baselineLow&&angle<=c.baselineHigh)
      r.baselines[{c.poolDates?0:s.day,s.windBin}].add(wind,power);
  }
  canceled(stop);std::map<CellKey,Stats> angleBins;std::map<CellKey,Cell> cells;
  const auto neg=negativeRange(c);
  for(size_t i=0;i<r.samples.size();++i){
    if(i%4096==0)canceled(stop);
    auto& s=r.samples[i];
    if(c.baselineMode==1)s.baseline=interpolate(p.reference,s.wind);
    else{
      auto it=r.baselines.find({c.poolDates?0:s.day,s.windBin});
      if(it==r.baselines.end())++r.missingBaseline;
      else if(it->second.n<size_t(c.minSamples))++r.sparseBaseline;
      else s.baseline=it->second.y;
    }
    if(c.baselineMode==1&&!std::isfinite(s.baseline))++r.missingBaseline;
    if(std::isfinite(s.baseline)){
      s.residual=s.power-s.baseline;
      if(s.baseline>c.minBaselinePower)s.percent=100*s.residual/s.baseline;
    }
    if(!std::isfinite(s.percent))++r.excludedPercent;
    const long long group=c.groupDates?s.day:0;
    angleBins[{group,s.windBin,index(s.angle,c.angleLow,c.angleHigh,c.angleStep)}].add(s);
    if(s.angle==0){++r.zeroAngle;continue;}
    int pair=-1;
    if(s.angle>0)pair=index(s.angle,c.positiveLow,c.positiveHigh,c.pairStep);
    else if(s.angle>=neg.first&&s.angle<=neg.second)pair=index(-s.angle,-neg.second,-neg.first,c.pairStep);
    if(pair<0){++r.outsidePairs;continue;}
    ++r.pairCandidates;auto& cell=cells[{group,s.windBin,pair}];
    cell.day=group;cell.windBin=s.windBin;cell.pairBin=pair;
    (s.angle>0?cell.positive:cell.negative).add(s);
  }
  canceled(stop);
  for(auto& entry:cells){
    auto& cell=entry.second;const size_t a=cell.positive.power.n,b=cell.negative.power.n;
    if(!a)cell.reason="缺少正侧";else if(!b)cell.reason="缺少负侧";
    else if(a<size_t(c.minSamples))cell.reason="正侧样本不足";
    else if(b<size_t(c.minSamples))cell.reason="负侧样本不足";
    else{cell.weight=std::min(a,b);cell.reason="有效";}
    if(!cell.weight)r.unmatched+=a+b;
    r.cells.push_back(cell);
  }
  for(const auto& entry:angleBins)r.bins.push_back({std::get<0>(entry.first),std::get<1>(entry.first),std::get<2>(entry.first),entry.second});
  // Stable time-stratified display copy retains residual extremes in each block.
  const size_t block=std::max<size_t>(1,(r.samples.size()+4999)/5000);
  for(size_t begin=0;begin<r.samples.size();begin+=block){
    canceled(stop);const size_t end=std::min(r.samples.size(),begin+block);size_t low=begin,high=begin;
    for(size_t i=begin;i<end;++i)if(std::isfinite(r.samples[i].residual)){
      if(!std::isfinite(r.samples[low].residual)||r.samples[i].residual<r.samples[low].residual)low=i;
      if(!std::isfinite(r.samples[high].residual)||r.samples[i].residual>r.samples[high].residual)high=i;
    }
    std::set<size_t> keep{begin,end-1,low,high};r.display.insert(r.display.end(),keep.begin(),keep.end());
  }
  return r;
}
}

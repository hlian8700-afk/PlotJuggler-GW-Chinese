#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace BladeFit
{
constexpr double pi = 3.14159265358979323846;
inline double missing() { return std::numeric_limits<double>::quiet_NaN(); }
struct Point { double x, y; };
using Points = std::vector<Point>;

struct Polynomial
{
  double origin, scale;
  std::vector<double> coefficients;
  double operator()(double x) const
  {
    double y = 0;
    for (auto it = coefficients.rbegin(); it != coefficients.rend(); ++it)
      y = y * ((x - origin) / scale) + *it;
    return y;
  }
};

// Streaming Givens QR uses bounded memory and avoids squaring the condition number.
inline Polynomial regress(const Points& points, int degree)
{
  if (degree < 0 || degree > 6 || points.size() <= size_t(degree))
    throw std::runtime_error("有效样本数不足");
  auto bounds = std::minmax_element(points.begin(), points.end(),
                                    [](Point a, Point b) { return a.x < b.x; });
  const double lo = bounds.first->x, hi = bounds.second->x;
  Polynomial model{lo + (hi - lo) / 2, hi == lo ? 1 : (hi-lo)/2, {}};
  double r[7][8]{};
  for (const auto p : points)
  {
    double row[8]{1};
    for (int j = 1; j <= degree; ++j)
      row[j] = row[j-1] * ((p.x-model.origin)/model.scale);
    row[degree+1] = p.y;
    for (int j = 0; j <= degree; ++j)
    {
      const double length = std::hypot(r[j][j], row[j]);
      if (length == 0) continue;
      const double c = r[j][j]/length, s = row[j]/length;
      for (int k = j; k <= degree+1; ++k)
      {
        const double old = r[j][k];
        r[j][k] = c*old + s*row[k];
        row[k] = c*row[k] - s*old;
      }
    }
  }
  model.coefficients.resize(degree+1);
  for (int j=degree; j>=0; --j)
  {
    if (std::abs(r[j][j]) <= 1e-12*std::abs(r[0][0]))
      throw std::runtime_error("不同横坐标不足，无法确定拟合系数");
    double rhs = r[j][degree+1];
    for (int k=j+1; k<=degree; ++k) rhs -= r[j][k]*model.coefficients[k];
    model.coefficients[j] = rhs/r[j][j];
  }
  return model;
}

template<class F> inline Points sample(double lo, double hi, F fn)
{
  Points out;
  const int count = lo == hi ? 1 : 200;
  for (int i=0; i<count; ++i)
  {
    const double x = count==1 ? lo : lo+(hi-lo)*(i/199.);
    const double y = fn(x);
    if (std::isfinite(y)) out.push_back({x,y});
  }
  return out;
}

inline Points average(const Points& p, size_t width)
{
  if (!width) throw std::runtime_error("窗口须大于零");
  Points out;
  size_t lo=0, hi=0;
  long double sum=0;
  for (size_t i=0; i<p.size(); ++i)
  {
    const size_t end=std::min(p.size(),i+width/2+1);
    const size_t start=i>(width-1)/2 ? i-(width-1)/2 : 0;
    while (hi<end) sum+=p[hi++].y;
    while (lo<start) sum-=p[lo++].y;
    out.push_back({p[i].x,double(sum/(hi-lo))});
  }
  return out;
}

enum Role { Wind, Power, Rotor, Generator, Pitch, Direction, Yaw, TorquePct,
            Status, Temp, FaultFlag, CurtailFlag, TransitionFlag, YawActiveFlag, NRoles };
enum Kind { M01,M02,M03,M05,M06,M11,M12,M21,M22,M23,M35,M41,M32,M34,M33,
            M51,M52,M53,M55, NKind };
enum TorqueUnit { Percent, NewtonMetre, KiloNewtonMetre };
struct WorkspaceFilter { std::shared_ptr<Points> samples; int operation=0; double threshold=0; };
struct Params
{
  double rated=0, diameter=0, rho=1.225, ratio=1, rpm=0, cutIn=3, cutOut=25,
         ratedWind=0, width=.5, tolerance=1, efficiency=1, yawLimit=180;
  double start=-std::numeric_limits<double>::infinity(), end=-start;
  int degree=4, torqueDegree=2, sectors=16, normal=0;
  bool filterState=false, generator=false, absoluteYaw=true, excludeNegative=true, rejectFlags=true;
  int torqueUnit=Percent;
  double ratedTorque=0, rpmWidth=0, torqueFitMin=0, powerFitMinRpm=0, powerFitMinPower=0;
  bool torqueNonnegative=true, powerSmooth=true;
  int powerSmoothWindow=11, powerMinBinSamples=3;
  std::vector<WorkspaceFilter> workspaceFilters;
  std::map<int,std::pair<double,double>> limits;
  Points reference;
};
inline bool passesWorkspaceFilters(const Params& p,double t)
{
  for(const auto& f:p.workspaceFilters){
    const auto& series=*f.samples;if(series.empty())return false;
    auto it=std::lower_bound(series.begin(),series.end(),t,[](Point a,double b){return a.x<b;});
    if(it==series.end() || (it!=series.begin() && t-(it-1)->x<=it->x-t))--it;
    if(!std::isfinite(it->y)||std::abs(it->x-t)>p.tolerance)return false;
    const double y=it->y,v=f.threshold;
    if(!(f.operation==0?y>=v:f.operation==1?y<=v:f.operation==2?y>v:y<v))return false;
  }
  return true;
}
using Channels=std::array<Points,NRoles>;
using SeriesRow=std::array<double,NRoles>;
inline int speedRole(const Params& p) { return p.generator ? Generator : Rotor; }
inline std::vector<int> required(int k,const Params& p)
{
  switch(k){
    case M01:case M05:return {Wind,Power};
    case M02:return {Wind};
    case M03:return {Wind,Power,Rotor};
    case M06:return {Wind,Rotor};
    case M11:return {Direction,Wind};
    case M12:return {Direction,Wind};
    case M21:return {Wind,speedRole(p)};
    case M22:return {Wind,Pitch};
    case M23:return {Wind,Yaw};
    case M35:return {speedRole(p),Wind,Power};
    case M41:return {Rotor,Generator};
    case M32:case M33:return {Generator,Power};
    case M34:return {Generator,TorquePct};
    case M51:return {Temp,Wind,Power};
    case M52:return {Yaw,Wind,Power};
    case M53:return {Pitch,Wind,Power};
    default:return {Wind,Power};
  }
}
inline bool needsCp(int k)
{ return k==M03 || k==M05 || k==M35 || k>=M51; }
inline double wrap(double x,double period)
{ return x-period*std::floor(x/period); }
inline double match(const Points& series,size_t& cursor,double t,double tol,bool state)
{
  while(cursor<series.size() && series[cursor].x<t) ++cursor;
  size_t i=cursor;
  if (i && (i==series.size() || (series[i].x!=t && (state || t-series[i-1].x<=series[i].x-t)))) --i;
  if(i==series.size() || (state && series[i].x>t) || std::abs(series[i].x-t)>tol) return missing();
  return series[i].y;
}
using SampleId = std::pair<int, size_t>;
struct Record { double x,y,t; SampleId id; };
struct Bin
{
  double x=0,y=0,m2=0,lo=0,hi=0;
  size_t n=0;
  void add(double a,double b)
  {
    if(!n) lo=hi=b;
    ++n; x+=(a-x)/n;
    const double d=b-y;
    y+=d/n; m2+=d*(b-y); lo=std::min(lo,b); hi=std::max(hi,b);
  }
  double sd() const { return n>1 ? std::sqrt(std::max(0.,m2/(n-1))) : 0; }
};
struct ModelPart
{
  double low,high,origin,scale;
  std::string formula;
  std::vector<double> coefficients;
};
struct Result
{
  std::vector<Record> raw;
  std::vector<size_t> display;
  std::map<std::pair<long long,long long>,size_t> density;
  std::map<long long,Bin> bins;
  Points fit, reference;
  std::vector<Points> fitSegments;
  size_t fitSamples=0;
  std::vector<Points> sectors;
  std::vector<ModelPart> models;
  std::vector<double> sectorPower;
  std::map<long long,double> resource;
  double shape=missing(), scale=missing(), peakX=missing(),peakY=missing(),slope=missing();
  size_t rejected=0;
};
inline double interpolate(const Points& p,double x)
{
  if(p.empty() || x<p.front().x || x>p.back().x) return missing();
  auto it=std::lower_bound(p.begin(),p.end(),x,[](Point a,double b){return a.x<b;});
  if(it==p.begin() || it->x==x) return it->y;
  const auto prev=*(it-1);
  return prev.y+(it->y-prev.y)*(x-prev.x)/(it->x-prev.x);
}
inline double fittedValue(const Result& r,double x)
{
  if(r.fitSegments.empty()) return interpolate(r.fit,x);
  for(const auto& segment:r.fitSegments)
    if(x>=segment.front().x && x<=segment.back().x) return interpolate(segment,x);
  return missing();
}
inline Points shapePreserving(const Points& p)
{
  if(p.size()<3)return p;
  const size_t n=p.size();std::vector<double> slope(n-1),d(n);
  for(size_t i=0;i+1<n;++i)slope[i]=(p[i+1].y-p[i].y)/(p[i+1].x-p[i].x);
  auto endpoint=[](double h0,double h1,double s0,double s1){
    double v=((2*h0+h1)*s0-h0*s1)/(h0+h1);
    if(v*s0<=0)return 0.;if(s0*s1<=0&&std::abs(v)>3*std::abs(s0))return 3*s0;return v;};
  d[0]=endpoint(p[1].x-p[0].x,p[2].x-p[1].x,slope[0],slope[1]);
  d[n-1]=endpoint(p[n-1].x-p[n-2].x,p[n-2].x-p[n-3].x,slope[n-2],slope[n-3]);
  for(size_t i=1;i+1<n;++i){
    if(slope[i-1]*slope[i]<=0)d[i]=0;
    else{double a=p[i].x-p[i-1].x,b=p[i+1].x-p[i].x;d[i]=(3*(a+b))/((2*b+a)/slope[i-1]+(b+2*a)/slope[i]);}
  }
  Points out;out.reserve((n-1)*8+1);
  for(size_t i=0;i+1<n;++i)for(int k=0;k<8;++k){
    const double u=k/8.,h=p[i+1].x-p[i].x,u2=u*u,u3=u2*u;
    out.push_back({p[i].x+u*h,(2*u3-3*u2+1)*p[i].y+(u3-2*u2+u)*h*d[i]+(-2*u3+3*u2)*p[i+1].y+(u3-u2)*h*d[i+1]});
  }
  out.push_back(p.back());return out;
}
inline double torqueReference(const Params& p)
{
  return p.ratedTorque>0?p.ratedTorque:
    (p.rated>0 && p.rpm>0 && p.efficiency>0 ? p.rated*1000*60/(2*pi*p.rpm*p.efficiency):missing());
}
inline double torqueDisplayFactor(int kind,int outputUnit,const Params& p)
{
  if(kind!=M33 && kind!=M34)return 1.;
  const double reference=torqueReference(p);
  if(outputUnit==Percent)return kind==M34?1.:(std::isfinite(reference)?100./reference:missing());
  const double nm=kind==M33?1.:(std::isfinite(reference)?reference/100.:missing());
  return outputUnit==KiloNewtonMetre?nm/1000.:nm;
}
inline double binWidth(int kind,const Params& p)
{
  return (kind==M32||kind==M33||kind==M34||kind==M35||kind==M41) && p.rpmWidth>0 ? p.rpmWidth:p.width;
}
inline std::pair<double,double> weibull(const std::vector<Record>& records)
{
  std::vector<double> logs;
  for(auto r:records) if(r.x>0) logs.push_back(std::log(r.x));
  if(logs.size()<2) return {missing(),missing()};
  const double mean=std::accumulate(logs.begin(),logs.end(),0.)/logs.size();
  const double top=*std::max_element(logs.begin(),logs.end());
  if(top-mean<1e-12) return {missing(),missing()};
  auto equation=[&](double k, double& sum){
    sum=0; double y=0;
    for(double v:logs){const double w=std::exp(k*(v-top)); sum+=w; y+=w*v;}
    return 1/k+mean-y/sum;
  };
  double lo=.001,hi=2,sum=0;
  while(hi<1e6 && equation(hi,sum)>0) hi*=2;
  for(int i=0;i<55;++i){double mid=(lo+hi)/2;if(equation(mid,sum)>0)lo=mid;else hi=mid;}
  const double k=(lo+hi)/2; equation(k,sum);
  return {k,std::exp(top+std::log(sum/logs.size())/k)};
}
inline void curves(int kind,const Params& p,Result& r)
{
  Points bins;
  for(const auto& entry:r.bins) bins.push_back({entry.second.x,entry.second.y});
  if(bins.empty()) return;
  const double lo=bins.front().x,hi=bins.back().x;
  if(kind==M12 || kind==M11) return;
  if(kind==M02)
  {
    const auto w=weibull(r.raw); r.shape=w.first;r.scale=w.second;
    if(std::isfinite(w.first)){
      r.models.push_back({lo,hi,0,1,"F(v)=1-exp(-(v/A)^k); [k,A]",{w.first,w.second}});
      r.fit=sample(0,(r.bins.rbegin()->first+1)*p.width,[&](double v){
        const double a=std::max(0.,v-p.width/2), b=v+p.width/2;
        return 100*(std::exp(-std::pow(a/r.scale,r.shape))-std::exp(-std::pow(b/r.scale,r.shape)));
      });
    }
    return;
  }
  if(kind==M55) {r.fit=bins;return;}
  if(kind==M32)
  {
    std::map<long long,Bin> fitBins;
    for(const auto& point:r.raw)if(point.x>=p.powerFitMinRpm && point.y>=p.powerFitMinPower){
      fitBins[static_cast<long long>(std::floor(point.x/binWidth(kind,p)))].add(point.x,point.y);
    }
    // Piecewise linear interpolation is shape preserving and cannot overshoot bin means.
    // Missing bins and fit-domain boundaries never receive invented observations.
    Points segment;std::vector<size_t> counts;
    auto finish=[&]{if(segment.empty())return;
      Points knots=segment;
      if(p.powerSmooth && knots.size()>2){
        const size_t half=size_t(p.powerSmoothWindow/2);
        for(size_t i=1;i+1<knots.size();++i){
          long double sum=0,weight=0;
          for(size_t j=i>half?i-half:0;j<std::min(knots.size(),i+half+1);++j){
            const double w=double(counts[j])*(half+1-std::abs(double(i)-double(j)));sum+=w*segment[j].y;weight+=w;}
          knots[i].y=double(sum/weight);
        }
      }
      auto path=p.powerSmooth?shapePreserving(knots):knots;
      r.fitSegments.push_back(path);r.fit.insert(r.fit.end(),path.begin(),path.end());
      r.models.push_back({path.front().x,path.back().x,0,1,p.powerSmooth?
        "sample-weighted local smoothing + shape-preserving Hermite interpolation":"piecewise linear interpolation of measured bin means",{}});
      segment.clear();counts.clear();};
    long long previous=0;bool first=true;int previousState=0;
    const double idleLimit=std::max(1.,p.rated*.002);
    for(const auto& entry:fitBins){
      if(entry.second.n<size_t(p.powerMinBinSamples)){finish();first=true;continue;}
      r.fitSamples+=entry.second.n;
      const int state=entry.second.y<=idleLimit?0:entry.second.x>=p.rpm?2:1;
      if(!first && (entry.first>previous+1 || (p.powerSmooth&&state!=previousState)))finish();
      segment.push_back({entry.second.x,entry.second.y});counts.push_back(entry.second.n);previous=entry.first;previousState=state;first=false;
    }
    finish();return;
  }
  if(kind==M33 || kind==M34)
  {
    // Fit the measured operating region; keep reference models in a separate layer.
    Points eligible;
    std::map<long long,Bin> fitBins;
    for(const auto& record:r.raw) if(record.x>=p.torqueFitMin){
      ++r.fitSamples;
      fitBins[static_cast<long long>(std::floor(record.x/binWidth(kind,p)))].add(record.x,record.y);
    }
    for(const auto& entry:fitBins) eligible.push_back({entry.second.x,entry.second.y});
    if(eligible.empty()) return;
    std::array<Points,2> regions;
    for(const auto& point:eligible)regions[point.x>=p.rpm?1:0].push_back(point);
    for(const auto& region:regions){
      if(region.size()<=size_t(p.torqueDegree)) continue;
      const auto model=regress(region,p.torqueDegree);
      r.models.push_back({region.front().x,region.back().x,model.origin,model.scale,
        p.torqueNonnegative?"y=max(0,sum(p_i*z^i))":"y=sum(p_i*z^i)",model.coefficients});
      // Empty bins break a displayed curve instead of bridging unobserved regions.
      size_t begin=0;
      for(size_t i=1;i<=region.size();++i){
        if(i<region.size() && region[i].x-region[i-1].x<=2*binWidth(kind,p)) continue;
        auto segment=sample(region[begin].x,region[i-1].x,[&](double x){
          const double y=model(x);return p.torqueNonnegative?std::max(0.,y):y;
        });
        r.fit.insert(r.fit.end(),segment.begin(),segment.end());
        r.fitSegments.push_back(std::move(segment));begin=i;
      }
    }
    const double referenceStart=std::max(p.rpm,eligible.front().x);
    if(referenceStart<=eligible.back().x)
      r.reference=sample(referenceStart,eligible.back().x,[&](double x){
        return kind==M34?100.:9550*p.rated/(p.efficiency*x);
      });
    return;
  }
  if(kind==M41)
  {
    long double xx=0,xy=0;
    for(const auto& a:r.raw){xx+=a.x*a.x;xy+=a.x*a.y;}
    if(xx>0){r.slope=double(xy/xx);r.models.push_back({lo,hi,0,1,"y=k*x",{r.slope}});r.fit=sample(lo,hi,[&](double x){return x*r.slope;});}
    if(p.ratio>0) r.reference=sample(lo,hi,[&](double x){return x*p.ratio;});
    return;
  }
  if(kind==M01)
  {
    r.reference=p.reference;
    if(r.reference.empty())
      r.reference=sample(p.cutIn,p.cutOut,[&](double v){
        return p.rated*std::clamp((std::pow(v,3)-std::pow(p.cutIn,3))/
             (std::pow(p.ratedWind,3)-std::pow(p.cutIn,3)),0.,1.);
      });
  }
  const bool windSplit=kind==M01||kind==M05||kind==M06||kind==M21||kind==M22;
  const bool rpmSplit=kind==M32||kind==M33||kind==M34;
  const double split=windSplit?p.ratedWind:(rpmSplit?p.rpm:hi+1);
  // Fit only supported portions of each operating region.
  Points lower, upper, startup;
  const double knee=p.cutIn+.25*(p.ratedWind-p.cutIn);
  for(auto b:bins)
    if(kind==M01 && b.x<knee) startup.push_back(b);
    else (b.x<split?lower:upper).push_back(b);
  auto segment=[&](const Points& points,int degree,auto evaluate,bool upperRegion=false){
    if(points.empty()) return;
    const auto poly=regress(points,std::min(degree,int(points.size()-1)));
    ModelPart part{points.front().x,points.back().x,poly.origin,poly.scale,"y=sum(p_i*z^i)",poly.coefficients};
    if(kind==M01&&!upperRegion)part.formula="y=clamp(sum(p_i*z^i),0,P_rated)";
    if(kind==M34&&!upperRegion)part.formula="y=min(100,sum(p_i*z^i))";
    if(upperRegion&&(kind==M01||kind==M32||kind==M21||kind==M34)){
      part.formula="y=c";part.coefficients={kind==M21?p.rpm:kind==M34?100.:p.rated};
    }
    if(upperRegion&&kind==M33){part.formula="y=c/x";part.coefficients={9550*p.rated/p.efficiency};}
    r.models.push_back(std::move(part));
    auto pts=sample(points.front().x,points.back().x,[&](double x){return evaluate(x,poly);});
    r.fit.insert(r.fit.end(),pts.begin(),pts.end());
  };

  if(kind==M01) segment(startup,2,[&](double x,const Polynomial& pol){return std::clamp(pol(x),0.,p.rated);});
  {
    int degree=kind==M01?5:kind==M22||kind==M33?0:kind==M21||kind==M06?1:
               kind==M03?p.degree:kind==M34?p.torqueDegree:kind==M23?2:3;
    segment(lower,degree,[&](double x,const Polynomial& pol){
      const double y=pol(x);
      return kind==M01?std::clamp(y,0.,p.rated):(kind==M34?std::min(100.,y):y);
    });
  }
  segment(upper,kind==M22||kind==M06?1:2,[&](double x,const Polynomial& pol){
    switch(kind){
      case M01:case M32:return p.rated;
      case M21:return p.rpm;
      case M34:return 100.;
      case M33:return x>0?9550.*p.rated/(p.efficiency*x):missing();
      default:return pol(x);
    }
  },true);
  if(r.fit.size()>200) {const auto dense=r.fit;r.fit=sample(lo,hi,[&](double x){return interpolate(dense,x);});}
  if(kind==M03 && !r.fit.empty()){
    const auto best=std::max_element(r.fit.begin(),r.fit.end(),[](Point a,Point b){return a.y<b.y;});
    r.peakX=best->x;r.peakY=best->y;
  }
}

inline Result fitPairedTurbineCurve(int kind, const Points& paired, const Params& p)
{
  if (kind != M01 && kind != M21 && kind != M22 && kind != M03)
    throw std::runtime_error("不支持的风机拟合模型");
  if (!(p.width > 0.0) || !std::isfinite(p.width))
    throw std::runtime_error("分箱宽度必须为正数");
  if (kind != M03 && !(0.0 <= p.cutIn && p.cutIn < p.ratedWind && p.ratedWind < p.cutOut))
    throw std::runtime_error("请核对切入、额定和切出风速");
  if ((kind == M01 && !(p.rated > 0.0)) || (kind == M21 && !(p.rpm > 0.0)))
    throw std::runtime_error("请填写额定功率或额定转速");
  Result result;
  for (const auto& point : paired)
  {
    if (!std::isfinite(point.x) || !std::isfinite(point.y) || point.x < 0.0) continue;
    if (kind != M03 && (point.x < p.cutIn || point.x > p.cutOut)) continue;
    if (kind != M22 && point.y < 0.0) continue;
    const double bin = std::floor(point.x / p.width);
    if (!std::isfinite(bin) || bin >= double(std::numeric_limits<long long>::max())) continue;
    result.bins[static_cast<long long>(bin)].add(point.x, point.y);
  }
  if (result.bins.size() < 2)
    throw std::runtime_error("有效分箱不足，请检查数据范围或调整分箱宽度");
  curves(kind, p, result);
  return result;
}

using Masks=std::set<SampleId>;
template<class Month> Result analyze(int kind,const Channels& channels,const Params& p,const Masks& exclude,Month month,const std::atomic_bool* stop=nullptr)
{
  const auto requiredRoles=required(kind,p);
  for(auto role:requiredRoles) if(channels[role].empty()) throw std::runtime_error("缺少所需测点");
  if(!(p.width>0) || p.end<p.start || p.tolerance<0 || p.sectors<1)
    throw std::runtime_error("请检查分箱宽度、时间范围和匹配容差");
  if(!std::isfinite(p.rpmWidth)||p.rpmWidth<0 || !std::isfinite(p.torqueFitMin)||p.torqueFitMin<0)
    throw std::runtime_error("请检查转速分箱宽度和转矩拟合最低转速");
  if(kind==M32 && (p.powerMinBinSamples<1||p.powerSmoothWindow<1||p.powerSmoothWindow>101||p.powerSmoothWindow%2==0))
    throw std::runtime_error("M32 平滑窗口应为 1～101 的奇数，最低箱样本数至少为 1");
  if(kind==M32 && (!std::isfinite(p.powerFitMinRpm)||p.powerFitMinRpm<0||!std::isfinite(p.powerFitMinPower)||p.powerFitMinPower<0))
    throw std::runtime_error("M32 拟合阈值必须为非负有限数值");
  if(kind==M33||kind==M34){
    if(p.torqueDegree<2||p.torqueDegree>3 || !(p.efficiency>0&&p.efficiency<=1))
      throw std::runtime_error("请检查转矩拟合阶数和发电机效率");
    if(kind==M34 && (p.torqueUnit<Percent||p.torqueUnit>KiloNewtonMetre ||
       (p.torqueUnit!=Percent && !std::isfinite(torqueReference(p)))))
      throw std::runtime_error("转矩单位或额定转矩无效，请填写额定转矩或功率、发电机额定转速和效率");
  }
  if(((needsCp(kind)||kind==M06) && !(p.diameter>0)) || (needsCp(kind) && !(p.rho>0)))
    throw std::runtime_error("请填写叶轮直径和空气密度");
  if((kind==M01||kind==M32||kind==M33) && !(p.rated>0))
    throw std::runtime_error("请填写额定功率（kW）");
  if((kind==M01||kind==M05||kind==M06||kind==M21||kind==M22) &&
     !(p.ratedWind>p.cutIn && p.cutOut>p.ratedWind))
    throw std::runtime_error("请核对切入、额定、切出风速");
  if((kind==M21||kind==M32||kind==M33||kind==M34) && !(p.rpm>0))
    throw std::runtime_error("请填写所选转速所在轴的额定转速");
  if(p.filterState && channels[Status].empty()) throw std::runtime_error("请关联运行状态");
  Result out;
  std::array<size_t,NRoles> cursors{};
  const auto anchorRole = requiredRoles.front();
  const auto& anchor=channels[anchorRole];
  double typical=0;
  if(kind==M01){
    std::vector<double> steps;
    for(size_t i=1;i<anchor.size();++i){
      const double dt=anchor[i].x-anchor[i-1].x;
      if(dt>0 && anchor[i].x>=p.start && anchor[i-1].x<=p.end)steps.push_back(dt);
    }
    if(!steps.empty()){auto mid=steps.begin()+steps.size()/2;std::nth_element(steps.begin(),mid,steps.end());typical=*mid;}
  }
  const double R=p.diameter/2;
  std::vector<Bin> sectorP(p.sectors);
  out.sectors.resize(p.sectors);
  std::vector<std::array<size_t,4>> bands(p.sectors);
  for(size_t i=0;i<anchor.size();++i){
    if((i%8192)==0 && stop && stop->load()) throw std::runtime_error("计算已取消");
    const double t=anchor[i].x;
    if(t<p.start||t>p.end) continue;
    if(!passesWorkspaceFilters(p,t)){++out.rejected;continue;}
    SeriesRow row;
    row.fill(missing());
    auto read=[&](int role){return role==anchorRole?anchor[i].y:match(channels[role],cursors[role],t,p.tolerance,role==Status || role>=FaultFlag);};
    const SampleId sampleId{anchorRole, i};
    bool valid=!exclude.count(sampleId);
    for(int role:requiredRoles){row[role]=read(role);valid &= std::isfinite(row[role]);}
    for(const auto& limit:p.limits){
      double value=read(limit.first);
      valid &= std::isfinite(value) && value>=limit.second.first && value<=limit.second.second;
    }
    if(p.filterState) valid &= read(Status)==p.normal;
    if(p.rejectFlags && kind!=M02 && kind!=M11 && kind!=M12)
      for(int role:{int(FaultFlag),int(CurtailFlag),int(TransitionFlag)})
        if(!channels[role].empty()) valid &= read(role)==0;
    const double v=row[Wind],kw=row[Power],rot=row[Rotor],gen=row[Generator],yaw=wrap(row[Yaw]+180,360)-180;
    if(std::isfinite(v)&&v<0) valid=false;
    if(p.excludeNegative && kw<0) valid=false;
    if(kind==M01 && std::isfinite(v) && v>=0 && v/p.width<double(std::numeric_limits<long long>::max()))
    {
      double dt=i+1<anchor.size()?anchor[i+1].x-t:typical;
      if(dt>5*typical)dt=typical;
      dt=std::max(0.,std::min(dt,p.end-t));
      out.resource[static_cast<long long>(std::floor(v/p.width))]+=dt;
    }
    if(kind==M01 && (v<p.cutIn||v>p.cutOut||kw<=0)) valid=false;
    if(kind==M03 && (v<=0||kw<=0||(p.rated>0&&kw>=p.rated)||(p.ratedWind>0&&v>=p.ratedWind))) valid=false;
    const double cp=v>0?kw*1000/(.5*p.rho*pi*R*R*v*v*v):missing();
    const double lambda=v>0?pi*rot*R/(30*v):missing();
    double x=v,y=kw;
    switch(kind){
      case M02:y=1;break;
      case M03:x=lambda;y=cp;break;
      case M05:y=cp;break;
      case M06:y=lambda;break;
      case M11:case M12:x=wrap(row[Direction],360);y=v;break;
      case M21:y=row[speedRole(p)];break;
      case M22:y=row[Pitch];break;
      case M23:y=p.absoluteYaw?std::abs(yaw):yaw;break;
      case M35:x=row[speedRole(p)];y=cp;break;
      case M41:x=rot;y=gen;break;
      case M32:x=gen;y=kw;break;
      case M34:x=gen;y=p.torqueUnit==Percent?row[TorquePct]:
        row[TorquePct]*(p.torqueUnit==KiloNewtonMetre?1000.:1.)*100/torqueReference(p);break;
      case M33:x=gen;y=gen>0?9550.*kw/(p.efficiency*gen):missing();break;
      case M51:x=row[Temp];y=cp;break;
      case M52:x=yaw;y=cp;break;
      case M53:x=row[Pitch];y=cp;break;
      case M55:x=month(t);y=cp;break;
    }
    if(!valid||!std::isfinite(x)||!std::isfinite(y)){++out.rejected;continue;}
    out.raw.push_back({x,y,t,sampleId});
    double index=std::floor(x/(kind==M55?1:binWidth(kind,p)));
    if(!std::isfinite(index)||std::abs(index)>=double(std::numeric_limits<long long>::max()))
      throw std::runtime_error("测量值超出分箱范围，请检查单位或设置有效上下限");
    long long bin=static_cast<long long>(index);
    if(kind==M11){
      bin=int(std::floor(wrap(x+180./p.sectors,360)*p.sectors/360));
      ++bands[bin][v<4?0:v<8?1:v<12?2:3];
      const double power=read(Power);
      if(std::isfinite(power)) sectorP[bin].add(x,power);
      // Average angles on the continuous interval around the sector centre.
      const double centre = 360.0 * bin / p.sectors;
      x = centre + wrap(x - centre + 180, 360) - 180;
    }
    out.bins[bin].add(x,y);
  }
  if(out.raw.empty()) throw std::runtime_error("当前条件下没有有效样本");
  if(kind==M11){
    for(auto& entry : out.bins) entry.second.x = wrap(entry.second.x, 360);
    for(int s=0;s<p.sectors;++s){
      out.sectorPower.push_back(sectorP[s].n?sectorP[s].y:missing());
      double radius=0;
      for(int band=0;band<4;++band){
        radius+=100.*bands[s][band]/out.raw.size();
        out.sectors[s].push_back({double(band),radius});
      }
    }
  }
  if(stop && stop->load()) throw std::runtime_error("计算已取消");
  // Retain time-bucket boundaries and extremes on both axes in the display copy.
  const size_t count=out.raw.size(),buckets=50000/6;
  if(count<=50000){out.display.resize(count);std::iota(out.display.begin(),out.display.end(),size_t(0));}
  else for(size_t i=0;i<buckets;++i){
    size_t first=i*count/buckets,end=(i+1)*count/buckets;
    std::array<size_t,6> indices{first,end-1,first,first,first,first};
    for(size_t j=first+1;j<end;++j){
      const auto& pt=out.raw[j];
      if(pt.x<out.raw[indices[2]].x)indices[2]=j;
      if(pt.x>out.raw[indices[3]].x)indices[3]=j;
      if(pt.y<out.raw[indices[4]].y)indices[4]=j;
      if(pt.y>out.raw[indices[5]].y)indices[5]=j;
    }
    std::sort(indices.begin(),indices.end());
    out.display.insert(out.display.end(),indices.begin(),std::unique(indices.begin(),indices.end()));
  }
  if(kind==M12)for(const auto& pt:out.raw)
    ++out.density[{static_cast<long long>(pt.x/5),static_cast<long long>(std::clamp(pt.y,-1e12,1e12))}];
  curves(kind,p,out);
  return out;
}
}

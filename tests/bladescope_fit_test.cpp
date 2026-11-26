#include "bladescope_fit_math.h"
#include <cassert>
#include <iostream>
using namespace BladeFit;
int main(){
 Points pts;for(int i=0;i<100;i++)pts.push_back({1.79e9+i,3.+2*i+.01*i*i});
 auto f=regress(pts,2);for(auto a:pts)assert(std::abs(f(a.x)-a.y)<1e-8);
 auto avg=average({{0,2},{1,4},{2,6}},3);assert(avg[0].y==3&&avg[1].y==4&&avg[2].y==5);
 Params p;p.rated=2000;p.ratedWind=12;p.rpm=1500;p.diameter=90;p.ratio=100;
 Channels c;
 for(int i=0;i<12000;++i){
   double t=1790000000+i,v=3+18*(i%210)/210.,n=std::min(15.,1.2*v);
   double ys[]={v,std::min(2000.,2*v*v*v),n,n*100,std::max(0.,v-12),std::fmod(i*1.,360.),std::sin(i*.01)*10,80.,0.,double(i%11)};
   for(int j=0;j<=Temp;++j)c[j].push_back({t,ys[j]});
 }
 for(int k=0;k<NKind;++k){auto r=analyze(k,c,p,{},[](double){return 11;});assert(!r.raw.empty());
 for(auto q:r.fit)assert(std::isfinite(q.x)&&std::isfinite(q.y));
 if(k==M41)assert(std::abs(r.slope-100)<1e-10);
 if(k==M02)assert(r.scale>0&&r.shape>0);
 if(k==M11){double total=0;for(auto s:r.sectors)total+=s.back().y;assert(std::abs(total-100)<1e-6);}
 if(k==M33)assert(std::abs(r.raw[0].y-9550.*c[Power][0].y/c[Generator][0].y)<1e-8);
 }
 // Optional wind data must not change the timestamps used for a speed-only fit.
 Channels asynchronous;
 asynchronous[Wind]={{0.5,8},{1.5,9}};
 asynchronous[Rotor]={{0,10},{1,20}};
 asynchronous[Generator]={{0,1000},{1,2000}};
 p.tolerance=0;
 const auto month=[](double){return 11;};
 const auto transmission=analyze(M41,asynchronous,p,{},month);
 assert(transmission.raw.size()==2&&std::abs(transmission.slope-100)<1e-10);
 const auto filtered=analyze(M41,asynchronous,p,{transmission.raw.front().id},month);
 assert(filtered.raw.size()==1&&filtered.raw.front().t==1);
 const auto windOnly=analyze(M02,asynchronous,p,{transmission.raw.front().id},month);
 assert(windOnly.raw.size()==2);

 // Points on either side of north belong to the same sector and average to north.
 Channels north;
 north[Direction]={{0,359},{1,1}};
 north[Wind]={{0,6},{1,8}};
 const auto rose=analyze(M11,north,p,{},month);
 assert(rose.bins.size()==1&&rose.bins.at(0).n==2);
 assert(std::abs(rose.bins.at(0).x)<1e-10);
 // Unit conversion must affect scatter, bins, fits and exports consistently.
 Params torque;torque.rated=6250;torque.rpm=1677;torque.efficiency=1;
 torque.width=.5;torque.rpmWidth=20;torque.torqueFitMin=1000;
 assert(std::abs(torqueReference(torque)-35589.20909926103)<1e-7);
 Channels nm,percent,knm;
 for(int i=0;i<18;++i){
   const double speed=i<9?900+i*20:1400+(i-9)*20;
   const double value=110.*speed*speed/(1677.*1677.);
   for(auto* channels:{&nm,&percent,&knm}){
     (*channels)[Generator].push_back({double(i),speed});
     (*channels)[Power].push_back({double(i),speed*speed*.002});
   }
   percent[TorquePct].push_back({double(i),value});
   nm[TorquePct].push_back({double(i),value*torqueReference(torque)/100});
   knm[TorquePct].push_back({double(i),value*torqueReference(torque)/100000});
 }
 const auto pctResult=analyze(M34,percent,torque,{},month);
 torque.torqueUnit=NewtonMetre;
 const auto nmResult=analyze(M34,nm,torque,{},month);
 torque.torqueUnit=KiloNewtonMetre;
 const auto knmResult=analyze(M34,knm,torque,{},month);
 assert(nmResult.fitSegments.size()==2);
 assert(nmResult.fitSamples==13);
 assert(!std::isfinite(fittedValue(nmResult,1200)));
 assert(!std::isfinite(fittedValue(nmResult,950)));
 for(size_t i=0;i<nmResult.raw.size();++i){
   assert(std::abs(nmResult.raw[i].y-pctResult.raw[i].y)<1e-8);
   assert(std::abs(knmResult.raw[i].y-pctResult.raw[i].y)<1e-8);
 }
 for(size_t i=0;i<nmResult.fit.size();++i){
   assert(nmResult.fit[i].x>=1000&&nmResult.fit[i].y>=0);
   assert(std::abs(nmResult.fit[i].y-pctResult.fit[i].y)<1e-7);
 }
 torque.ratedTorque=30000;assert(torqueReference(torque)==30000);
 const auto converted=analyze(M33,nm,torque,{},month);
 assert(converted.fitSegments.size()==2);
 assert(converted.fit.front().y!=converted.fit.back().y);
 // Percentages over 100 are valid observations and must retain their magnitude.
 torque.torqueUnit=Percent;torque.torqueFitMin=0;
 Channels overload;
 for(int i=0;i<6;++i){overload[Generator].push_back({double(i),1600.+i*20});overload[TorquePct].push_back({double(i),120.+i});}
 const auto over=analyze(M34,overload,torque,{},month);
 assert(over.fit.front().y>100);
 std::cout<<"19 analyses and torque conversion, operating range, gap and overload regressions passed\n";
}

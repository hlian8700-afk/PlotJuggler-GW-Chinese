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
 std::cout<<"19 analyses, QR trend, moving average, torque units, rose and transmission checks passed\n";
}

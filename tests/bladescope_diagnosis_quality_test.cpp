#include "bladescope_diagnosis_core.h"
#include <cassert>
using namespace BladeDiagnosis;
int main(){
 std::vector<RawSample> s{{0,1},{.01,2},{.02,3}};
 auto q=inspectWindow(s,{0,20},20);assert(q.ready&&q.finite==3);
 s.insert(s.begin()+1,{0,1});q=inspectWindow(s,{0,20},20);assert(!q.ready&&q.duplicates==1);
 s={{0,1},{.02,3},{.01,2}};assert(inspectWindow(s,{0,20},20).backwards==1);
 s={{0,1},{.01,std::numeric_limits<double>::quiet_NaN()},{.02,3}};assert(!inspectWindow(s,{0,20},20).ready);
 s={{0,1},{.1,2}};assert(!inspectWindow(s,{0,100},20).ready);
 assert(!inspectWindow(s,{-100,100},200).boundsCovered);
 assert(inspectWindow({{-1,1},{0,2},{.01,3},{1,4}},{0,10},20).ready);
 bool rejected=false;try{inspectWindow(s,{0,100},0);}catch(...){rejected=true;}assert(rejected);
}

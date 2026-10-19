// Copyright Contributors to the OpenVDB Project
// SPDX-License-Identifier: Apache-2.0

#include <openvdb/openvdb.h>
#include <openvdb/tools/Interpolation.h>
#include <openvdb/tools/Filter.h>
#include <openvdb/tools/Dense.h>
#include <windows.h>
#include <chrono>
#include <atomic>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>
using Clock=std::chrono::steady_clock;
volatile double observed=0;
void check(bool b) { if (!b) throw std::runtime_error("algorithm validation failed"); }
int main(int argc,char** argv) try {
 int n=argc>1?std::stoi(argv[1]):1048576;
 int cpu=argc>2?std::stoi(argv[2]):16;
 check(cpu>=0&&cpu<63);
 check(SetProcessAffinityMask(GetCurrentProcess(),DWORD_PTR(1ULL<<cpu))!=0);
 openvdb::initialize();
 auto grid=openvdb::FloatGrid::create(0.0f);
 auto a=grid->getAccessor();
 for(int x=-16;x<16;++x)for(int y=-16;y<16;++y)for(int z=-16;z<16;++z)
  a.setValue(openvdb::Coord(x,y,z),float(100+x+2*y+3*z));
 std::vector<openvdb::Coord> coords;
 for(int x=-12;x<12;++x)for(int y=-12;y<12;++y)for(int z=-12;z<12;++z) coords.emplace_back(x,y,z);
 auto emit=[&](const char* name,int operations,auto setup,auto fn,auto validate) {
  for(int i=0;i<2;++i) {setup();observed=fn();validate(observed);}
  std::cout<<"{\"name\":\""<<name<<"\",\"operations\":"<<operations<<",\"elapsedMilliseconds\":[";
  for(int i=0;i<5;++i) {setup();std::atomic_signal_fence(std::memory_order_seq_cst);auto start=Clock::now();double result=fn();auto end=Clock::now();std::atomic_signal_fence(std::memory_order_seq_cst);observed=result;validate(result);if(i)std::cout<<',';std::cout<<std::chrono::duration<double,std::milli>(end-start).count();}
  std::cout<<"],\"checksum\":"<<observed<<"}";
 };
 double expected=0;for(int i=0;i<n;++i){auto c=coords[i%coords.size()];expected+=100+c.x()+2*c.y()+3*c.z()+1.5;}
 std::cout<<std::setprecision(17)<<"{\"results\":[";
 emit("box-sampling",n,[]{},[&]{double s=0;for(int i=0;i<n;++i){auto c=coords[i%coords.size()];float value;openvdb::tools::BoxSampler::sample(a,openvdb::Vec3d(c.x()+.25,c.y()+.25,c.z()+.25),value);s+=value;}return s;},[&](double s){check(s==expected);});
 std::cout<<',';
 emit("quadratic-sampling",n,[]{},[&]{double s=0;for(int i=0;i<n;++i){auto c=coords[i%coords.size()];float value;openvdb::tools::QuadraticSampler::sample(a,openvdb::Vec3d(c.x()+.25,c.y()+.25,c.z()+.25),value);s+=value;}return s;},[&](double s){check(std::abs(s-expected)<.01);});
 std::cout<<',';
 emit("probe-value",n,[]{},[&]{double s=0;for(int i=0;i<n;++i){float value;check(a.probeValue(coords[i%coords.size()],value));s+=value+1.5;}return s;},[&](double s){check(s==expected);});
 std::cout<<',';
 emit("accessor-create-read-destroy",n,[]{},[&]{double s=0;for(int i=0;i<n;++i){auto acc=grid->getConstAccessor();s+=acc.getValue(coords[i%coords.size()])+1.5;}return s;},[&](double s){check(s==expected);});
 std::cout<<',';
 emit("accessor-clear-read",n,[]{},[&]{double s=0;for(int i=0;i<n;++i){a.clear();s+=a.getValue(coords[i%coords.size()])+1.5;}return s;},[&](double s){check(s==expected);});
 std::cout<<',';
 openvdb::tools::Dense<float,openvdb::tools::LayoutXYZ> dense(openvdb::CoordBBox(openvdb::Coord(-16),openvdb::Coord(15)));
 emit("copy-to-dense",32768*256,[]{},[&]{for (int repeat=0;repeat<256;++repeat) openvdb::tools::copyToDense(grid->tree(),dense,true);double s=0;for(int i=0;i<32768;++i)s+=dense.data()[i];return s;},[](double s){check(s==32768*97.0);});
 std::cout<<',';
 openvdb::FloatGrid::Ptr filtered;
 emit("mean-filter",32768*8,[&]{filtered=grid->deepCopy();},[&]{openvdb::tools::Filter<openvdb::FloatGrid> f(*filtered);f.setGrainSize(0);f.mean(1,8);double s=0;for(auto it=filtered->cbeginValueOn();it;++it)s+=*it;return s;},[&](double s){check(std::isfinite(s)&&s>0&&filtered->activeVoxelCount()==32768);check(filtered->tree().getValue(openvdb::Coord(0))==100);});
 std::cout<<"]}\n";
 openvdb::uninitialize();return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}




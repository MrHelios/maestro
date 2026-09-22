#include "core/RowLayout.h"
#include "core/utf8.h"
#include "tests/helpers/perf_verbose.h"
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <numeric>
#include <iostream>
#include <chrono>

// Standalone: silencio por defecto, tablas solo con MAESTRO_PERF_VERBOSE=1.
struct PerfNullBuf : std::streambuf { int overflow(int c) override { return c; } };
int main(){
  PerfNullBuf perfNull;
  std::ostream perfLog(perf_verbose::enabled() ? std::cout.rdbuf() : &perfNull);
 auto load = [](){
   std::ifstream f("tests/temp/TextEditor.md", std::ios::binary);
   std::string c((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
   std::vector<std::string> l; size_t s=0;
   for(size_t i=0;i<=c.size();++i) if(i==c.size()||c[i]=='\n'){ l.emplace_back(c.substr(s,i-s)); s=i+1; }
   return l;
 };
 auto lines = load();
 std::vector<std::string> longest=lines;
 std::sort(longest.begin(), longest.end(), [](auto&a,auto&b){return a.size()>b.size();});
 std::vector<std::string> visLong; for(int i=0;i<40;++i) visLong.push_back(longest[i]);
 std::vector<std::string> visAvg; for(int i=0;i<40;++i) visAvg.push_back(lines[i* (lines.size()/40)]);

 std::string huge; std::string pat="int x(){ return 42; } // \xC3\xA9\xE2\x80\x94\t"; while(huge.size()<540*1024) huge+=pat; huge.resize(540*1024);

 auto bench = [](auto fn, int rep)->double{
   volatile size_t sink=0;
   auto t0=std::chrono::high_resolution_clock::now();
   for(int i=0;i<rep;++i) sink+=fn();
   auto t1=std::chrono::high_resolution_clock::now();
   return std::chrono::duration<double,std::milli>(t1-t0).count();
 };

 perfLog<<"=== TextEditor.md stats ===\n";
 perfLog<<"lines "<<lines.size()<<" total "<<std::accumulate(lines.begin(), lines.end(), 0ull, [](auto a, auto&s){return a+s.size();})<<" maxLine "<<longest[0].size()<<"\n";
 perfLog<<"visLong avg "<< (std::accumulate(visLong.begin(), visLong.end(), 0ull, [](auto a, auto&s){return a+s.size();})/40) << " max "<<visLong[0].size()<<"\n";
 perfLog<<"visAvg avg "<< (std::accumulate(visAvg.begin(), visAvg.end(), 0ull, [](auto a, auto&s){return a+s.size();})/40) << "\n";
 perfLog<<"huge "<<huge.size()<<" cols "<<utf8::columnOf(huge,huge.size())<<"\n\n";

  // per frame micro bench (single iteration)
 perfLog<<"--- Per-frame (40 lines) micro bench (single frame) ---\n";
 for(auto &caseName: std::vector<std::string>{"visLong","visAvg"}){
   auto &vis = (caseName=="visLong"?visLong:visAvg);
   auto fnBase = [&]()->size_t{ size_t s=0; for(auto &l:vis){ for(int i=0;i<8;++i){int b=(l.size()*i)/8; b=utf8::alignStart(l,b); s+=utf8::columnOf(l,b);} s+=utf8::range(l,0,80).size(); s+=utf8::expandTabs(utf8::range(l,0,80)).size();} return s;};
   auto fnFull = [&]()->size_t{ size_t s=0; for(auto &l:vis){ rowlayout::RowLayoutFull rl(l); for(int i=0;i<8;++i){int b=(l.size()*i)/8; b=utf8::alignStart(l,b); s+=rl.columnAt(b);} s+=rl.range(0,80).size(); s+=rl.expandVisible(0,80).size();} return s;};
   auto fnChk = [&]()->size_t{ size_t s=0; for(auto &l:vis){ rowlayout::RowLayoutCheckpoint rl(l); for(int i=0;i<8;++i){int b=(l.size()*i)/8; b=utf8::alignStart(l,b); s+=rl.columnAt(b);} s+=rl.range(0,80).size(); s+=rl.expandVisible(0,80).size();} return s;};
   double tB=bench([&](){return fnBase();},200);
   double tF=bench([&](){return fnFull();},200);
   double tC=bench([&](){return fnChk();},200);
   perfLog<<caseName<<" baseline "<<tB/200<<" ms/frame\n";
   perfLog<<caseName<<" Full     "<<tF/200<<" ms/frame x"<<tB/tF<<"\n";
   perfLog<<caseName<<" Chk      "<<tC/200<<" ms/frame x"<<tB/tC<<"\n";
 }

 perfLog<<"\n--- Huge 540KB single line, 40 col + 15 range per frame ---\n";
 auto fnBaseH = [&]()->size_t{ size_t s=0; for(int i=0;i<20;++i){int b=(i*27000)%huge.size(); b=utf8::alignStart(huge,b); s+=utf8::columnOf(huge,b);} for(int k=0;k<5;++k){s+=utf8::range(huge,0,80).size(); s+=utf8::range(huge,1000,1080).size();} return s;};
 auto fnFullH = [&]()->size_t{ rowlayout::RowLayoutFull rl(huge); size_t s=0; for(int i=0;i<20;++i){int b=(i*27000)%huge.size(); b=utf8::alignStart(huge,b); s+=rl.columnAt(b);} for(int k=0;k<5;++k){s+=rl.range(0,80).size(); s+=rl.range(1000,1080).size();} return s;};
 auto fnChkH = [&]()->size_t{ rowlayout::RowLayoutCheckpoint rl(huge); size_t s=0; for(int i=0;i<20;++i){int b=(i*27000)%huge.size(); b=utf8::alignStart(huge,b); s+=rl.columnAt(b);} for(int k=0;k<5;++k){s+=rl.range(0,80).size(); s+=rl.range(1000,1080).size();} return s;};
 double tbH=bench(fnBaseH,20);
 double tfH=bench(fnFullH,20);
 double tcH=bench(fnChkH,20);
 perfLog<<"baseline "<<tbH/20<<" ms/frame\n";
 perfLog<<"Full     "<<tfH/20<<" ms/frame x"<<tbH/tfH<<"\n";
 perfLog<<"Chk      "<<tcH/20<<" ms/frame x"<<tbH/tcH<<"\n";
 perfLog<<"Full mem "<<rowlayout::RowLayoutFull(huge).memoryBytes()/1024.0<<" KB\n";
 perfLog<<"Chk mem  "<<rowlayout::RowLayoutCheckpoint(huge).memoryBytes()/1024.0<<" KB chk="<<rowlayout::RowLayoutCheckpoint(huge).checkpointCount()<<"\n";

 return 0;
}

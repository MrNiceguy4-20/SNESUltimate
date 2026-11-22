#include "dsp1.hpp"
#include "cartridge.hpp"
#include <cstdint>
#include <iostream>
namespace {
int fails=0;
void ex(bool c,const char* m){if(!c){std::cerr<<"FAIL: "<<m<<'\n';++fails;}}
constexpr uint32_t kNtsc=21477272u, kPal=21281370u, kDsp=7600000u;
constexpr uint32_t data=0x600000u, status=0x604000u;
uint8_t rd(CartridgeDevice& d,uint32_t a){uint8_t v=0; d.read(a,v); return v;}
void wr(CartridgeDevice& d,uint32_t a,uint8_t v){d.write(a,v);}
uint64_t waitReady(CartridgeDevice& d){uint64_t n=0; while((rd(d,status)&0x80u)==0 && n<2000){d.clock(1);++n;} return n;}
uint64_t ceilMaster(uint64_t dspCycles,uint32_t master){return (dspCycles*master + kDsp-1)/kDsp;}
void put16(CartridgeDevice& d,uint16_t v){wr(d,data,uint8_t(v)); wr(d,data,uint8_t(v>>8));}
uint16_t get16(CartridgeDevice& d){uint16_t lo=rd(d,data),hi=rd(d,data);return uint16_t(lo|(hi<<8));}
}
int main(){
    auto d=createDSP1Device(DSP1Map::LoROMLarge,kNtsc); d->reset();
    ex((rd(*d,status)&0x80)!=0,"reset ready");
    wr(*d,data,0x00); ex((rd(*d,status)&0x80)==0,"command drops RQM");
    wr(*d,data,0xff);
    uint64_t c0=waitReady(*d); ex(c0>0,"command decode consumes time");
    wr(*d,data,0x00); ex((rd(*d,status)&0x80)!=0,"low input byte does not complete DR word");
    wr(*d,data,0x40); ex((rd(*d,status)&0x80)==0,"high input byte starts input phase"); waitReady(*d);
    put16(*d,0x4000); waitReady(*d);
    ex(get16(*d)==0x2000,"multiply result after timed inputs");
    ex((rd(*d,status)&0x80)==0,"completed output word starts output phase"); waitReady(*d);
    auto m=createDSP1Device(DSP1Map::LoROMLarge,kNtsc); m->reset(); uint64_t elapsed=0;
    wr(*m,data,0x00); elapsed+=waitReady(*m);
    put16(*m,0x4000); elapsed+=waitReady(*m);
    put16(*m,0x4000); elapsed+=waitReady(*m);
    ex(get16(*m)==0x2000,"timed multiply value"); elapsed+=waitReady(*m);
    ex(elapsed==ceilMaster(26,kNtsc),"NTSC multiply totals documented 26 DSP clocks");
    auto p=createDSP1Device(DSP1Map::LoROMLarge,kPal); p->reset(); elapsed=0;
    wr(*p,data,0x00); elapsed+=waitReady(*p); put16(*p,0x4000); elapsed+=waitReady(*p); put16(*p,0x4000); elapsed+=waitReady(*p); (void)get16(*p); elapsed+=waitReady(*p);
    ex(elapsed==ceilMaster(26,kPal),"PAL master clock converts same 26 DSP clocks");
    auto s=createDSP1Device(DSP1Map::LoROMLarge,kNtsc); s->reset(); wr(*s,data,0x00); s->clock(8);
    auto blob=s->saveState(); uint64_t tail1=waitReady(*s); ex(tail1>0,"checkpoint taken mid command wait");
    ex(s->loadState(blob),"reload timed DSP1 state"); uint64_t tail2=waitReady(*s); ex(tail1==tail2,"mid-wait save/load preserves phase");
    if(fails){std::cerr<<fails<<" DSP1 timing regressions failed\n";return 1;} std::cout<<"DSP1 timing accuracy regression passed\n";
}

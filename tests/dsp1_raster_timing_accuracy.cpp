#include "dsp1.hpp"
#include <cstdint>
#include <iostream>
namespace {
int fails = 0;
void ex(bool c, const char* m) { if (!c) { std::cerr << "FAIL: " << m << '\n'; ++fails; } }
constexpr uint32_t kNtsc = 21477272u, kDsp = 7600000u;
constexpr uint32_t data = 0x600000u, status = 0x604000u;
uint8_t rd(CartridgeDevice& d, uint32_t a) { uint8_t v=0; d.read(a,v); return v; }
void wr(CartridgeDevice& d, uint32_t a, uint8_t v) { d.write(a,v); }
uint64_t waitReady(CartridgeDevice& d, uint64_t limit=10000) {
    uint64_t n=0; while ((rd(d,status)&0x80u)==0 && n<limit) { d.clock(1); ++n; } return n;
}
uint64_t ceilMaster(uint64_t dspCycles) { return (dspCycles*kNtsc + kDsp-1)/kDsp; }
void put16(CartridgeDevice& d, uint16_t v) { wr(d,data,uint8_t(v)); wr(d,data,uint8_t(v>>8)); }
uint16_t get16(CartridgeDevice& d) { uint16_t lo=rd(d,data), hi=rd(d,data); return uint16_t(lo|(hi<<8)); }
void timedPut16(CartridgeDevice& d, uint16_t v) { put16(d,v); waitReady(d); }
uint16_t timedGet16(CartridgeDevice& d) { uint16_t v=get16(d); waitReady(d); return v; }
void setupProjection(CartridgeDevice& d) {
    wr(d,data,0x02); waitReady(d);
    for (uint16_t v : {uint16_t(100),uint16_t(200),uint16_t(1000),uint16_t(0),uint16_t(256),uint16_t(0),uint16_t(0x2000)}) timedPut16(d,v);
    for (int i=0;i<4;++i) (void)timedGet16(d);
}
}
int main() {
    auto r = createDSP1Device(DSP1Map::LoROMLarge, kNtsc); r->reset();
    uint64_t elapsed=0;
    wr(*r,data,0x0a); elapsed += waitReady(*r);
    put16(*r,0); elapsed += waitReady(*r);
    ex(elapsed == ceilMaster(217), "Raster first result waits documented 6+211 DSP clocks");
    auto inv = createDSP1Device(DSP1Map::LoROMLarge, kNtsc); inv->reset(); elapsed=0;
    wr(*inv,data,0x10); elapsed += waitReady(*inv);
    put16(*inv,0x4000); elapsed += waitReady(*inv);
    put16(*inv,0); elapsed += waitReady(*inv);
    (void)get16(*inv); elapsed += waitReady(*inv);
    (void)get16(*inv); elapsed += waitReady(*inv);
    ex(elapsed == ceilMaster(98), "Inverse staged timing totals documented 98 DSP clocks");
    auto s = createDSP1Device(DSP1Map::LoROMLarge, kNtsc); s->reset(); setupProjection(*s);
    wr(*s,data,0x0a); waitReady(*s); timedPut16(*s,0);
    for (int i=0;i<3;++i) (void)timedGet16(*s);
    (void)get16(*s);
    uint64_t seven = waitReady(*s);
    ex(seven > 0 && seven < 64, "Raster Dn exposes short command-interrupt wait");
    wr(*s,data,0x00);
    ex((rd(*s,status)&0x80u)==0, "new command accepted in Raster inter-line window");
    waitReady(*s); timedPut16(*s,0x4000); timedPut16(*s,0x4000);
    ex(get16(*s)==0x2000, "inter-line command cleanly terminates Raster stream");
    auto q = createDSP1Device(DSP1Map::LoROMLarge, kNtsc); q->reset(); setupProjection(*q);
    wr(*q,data,0x0a); waitReady(*q); timedPut16(*q,0);
    for (int i=0;i<3;++i) (void)timedGet16(*q);
    (void)get16(*q); waitReady(*q);
    q->clock(100);
    auto blob=q->saveState();
    q->clock(600);
    uint8_t after1=rd(*q,data);
    ex(q->loadState(blob), "reload Raster inter-line state");
    q->clock(600);
    uint8_t after2=rd(*q,data);
    ex(after1==after2 && after1!=0x80, "Raster An+1 deadline replays deterministically after checkpoint");
    if (fails) { std::cerr << fails << " DSP1 Raster timing regressions failed\n"; return 1; }
    std::cout << "DSP1 Raster timing accuracy regression passed\n";
}

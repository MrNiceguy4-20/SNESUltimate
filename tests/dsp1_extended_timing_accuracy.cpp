#include "dsp1.hpp"
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <vector>
namespace {
int fails = 0;
void expect(bool c, const char* m) { if (!c) { std::cerr << "FAIL: " << m << '\n'; ++fails; } }
constexpr uint32_t kNtsc = 21477272u;
constexpr uint32_t kDsp = 7600000u;
constexpr uint32_t data = 0x600000u, status = 0x604000u;
uint8_t rd(CartridgeDevice& d, uint32_t a) { uint8_t v=0; d.read(a,v); return v; }
void wr(CartridgeDevice& d, uint32_t a, uint8_t v) { d.write(a,v); }
uint64_t waitReady(CartridgeDevice& d, uint64_t limit=100000) {
    uint64_t n=0;
    while ((rd(d,status)&0x80u)==0 && n<limit) { d.clock(1); ++n; }
    return n;
}
uint64_t ceilMaster(uint64_t dspCycles) { return (dspCycles*kNtsc + kDsp-1)/kDsp; }
void put16(CartridgeDevice& d, uint16_t v) { wr(d,data,uint8_t(v)); wr(d,data,uint8_t(v>>8)); }
uint16_t get16(CartridgeDevice& d) { uint16_t lo=rd(d,data), hi=rd(d,data); return uint16_t(lo|(hi<<8)); }
uint64_t run(uint8_t command, std::initializer_list<uint16_t> inputs, unsigned outputs) {
    auto d = createDSP1Device(DSP1Map::LoROMLarge, kNtsc); d->reset();
    uint64_t elapsed=0;
    wr(*d,data,command); elapsed += waitReady(*d);
    for (uint16_t v: inputs) { put16(*d,v); elapsed += waitReady(*d); }
    for (unsigned i=0;i<outputs;++i) { (void)get16(*d); elapsed += waitReady(*d); }
    return elapsed;
}
}
int main() {
    expect(run(0x02,{100,200,1000,0,256,0,0x2000},4)==ceilMaster(892),
           "Parameter follows documented 892-cycle staged RQM route");
    expect(run(0x01,{0x4000,0x1000,0x2000,0x3000},0)==ceilMaster(164),
           "Attitude follows documented 164-cycle staged RQM route");
    expect(run(0x0d,{1,2,3},3)==ceilMaster(45),
           "Objective follows documented 45-cycle staged RQM route");
    expect(run(0x03,{1,2,3},3)==ceilMaster(44),
           "Subjective follows documented 44-cycle staged RQM route");
    expect(run(0x0b,{1,2,3},1)==ceilMaster(36),
           "Scalar follows documented 36-cycle staged RQM route");
    expect(run(0x0e,{0,0},2)==ceilMaster(228),
           "Target follows documented 228-cycle staged RQM route");
    auto q = createDSP1Device(DSP1Map::LoROMLarge, kNtsc); q->reset();
    wr(*q,data,0x02); waitReady(*q);
    for (uint16_t v: {uint16_t(100),uint16_t(200),uint16_t(1000),uint16_t(0),uint16_t(256),uint16_t(0)}) {
        put16(*q,v); waitReady(*q);
    }
    put16(*q,0x2000);
    q->clock(500);
    expect((rd(*q,status)&0x80u)==0,"Parameter remains busy partway through 839-cycle Azs phase");
    auto blob=q->saveState();
    uint64_t a=waitReady(*q);
    expect(q->loadState(blob),"reload Parameter long-input wait");
    uint64_t b=waitReady(*q);
    expect(a==b && a>0,"Parameter long-input RQM deadline replays deterministically");
    if (fails) { std::cerr << fails << " DSP1 extended timing regressions failed\n"; return 1; }
    std::cout << "DSP1 extended staged timing accuracy regression passed\n";
}

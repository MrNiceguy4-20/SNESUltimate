#include "bus.hpp"
#include "cartridge.hpp"
#include <cstdint>
#include <iostream>
#include <vector>
namespace {
int failures = 0;
void expect(bool cond, const char* msg) { if (!cond) { std::cerr << "FAIL: " << msg << '\n'; ++failures; } }
void w16(std::vector<uint8_t>& r, size_t o, uint16_t v) { r[o]=uint8_t(v); r[o+1]=uint8_t(v>>8); }
std::vector<uint8_t> srtcRom() {
    std::vector<uint8_t> r(1024*1024, 0);
    size_t h=0x7fc0; for(int i=0;i<21;i++) r[h+i]=' ';
    r[h+0x15]=0x35; r[h+0x16]=0x55; r[h+0x17]=0x0a; r[h+0x18]=0x03; r[h+0x19]=1;
    w16(r,h+0x1c,0xedcb); w16(r,h+0x1e,0x1234); w16(r,h+0x3c,0x8000); return r;
}
std::vector<uint8_t> spcRtcRom() {
    std::vector<uint8_t> r(6*1024*1024, 0);
    size_t h=0xffc0; for(int i=0;i<21;i++) r[h+i]=' ';
    r[h+0x15]=0x3a; r[h+0x16]=0xf9; r[h+0x17]=0x0d; r[h+0x18]=0x03; r[h+0x19]=1;
    w16(r,h+0x1c,0xedcb); w16(r,h+0x1e,0x1234); w16(r,h+0x3c,0x8000); return r;
}
void srtcWrite(Bus& b, const uint8_t (&v)[12]) {
    b.write(0x002801,0x0e); b.write(0x002801,0x00);
    for(uint8_t x:v) b.write(0x002801,x);
    b.write(0x002801,0x0d);
}
uint8_t srtcSecond(Bus& b) {
    b.write(0x002801,0x0d); expect((b.read(0x002800)&0x0f)==0x0f,"S-RTC read starts with sentinel");
    uint8_t lo=b.read(0x002800)&0x0f, hi=b.read(0x002800)&0x0f; return uint8_t(lo+hi*10);
}
void rtcWait(Cartridge& c){c.clockHardware(82);}
void spcWriteRtc(Cartridge& c, Bus& b, const uint8_t (&v)[16]) {
    b.write(0x004840,1); b.write(0x004841,3); rtcWait(c); b.write(0x004841,0); rtcWait(c);
    for(uint8_t x:v){b.write(0x004841,x);rtcWait(c);}
}
uint8_t spcRtcReg(Cartridge& c, Bus& b, uint8_t index) {
    b.write(0x004840,1); b.write(0x004841,12); rtcWait(c); b.write(0x004841,index); rtcWait(c); return b.read(0x004841)&0x0f;
}
uint8_t spcRtcSecond(Cartridge& c, Bus& b) {
    uint8_t lo=spcRtcReg(c,b,0), hi=spcRtcReg(c,b,1); return uint8_t(lo+hi*10);
}
uint8_t spcRtcDay(Cartridge& c, Bus& b) {
    uint8_t lo=spcRtcReg(c,b,6), hi=spcRtcReg(c,b,7); return uint8_t(lo+hi*10);
}
}
int main() {
    constexpr uint64_t NTSC_MASTER = 21477272ull;
    Cartridge srtc; srtc.setSRAMPersistence(false);
    expect(srtc.loadData(srtcRom(),"/tmp/phase32_srtc.sfc"),"S-RTC fixture loads");
    Bus sb; sb.attachCartridge(&srtc);
    const uint8_t sharp[12]={0,0,0,0,0,0,1,0,1,0,0,0};
    srtcWrite(sb,sharp);
    sb.write(0x002801,0x0d); (void)sb.read(0x002800); for(int i=0;i<12;++i)(void)sb.read(0x002800);
    expect((sb.read(0x002800)&0x0f)==3,"Sharp epoch 1000-01-01 is Wednesday");
    expect(srtcSecond(sb)==0,"S-RTC starts at programmed second");
    srtc.clockHardware(NTSC_MASTER-1);
    expect(srtcSecond(sb)==0,"S-RTC does not tick before one emulated second");
    srtc.clockHardware(1);
    expect(srtcSecond(sb)==1,"S-RTC ticks exactly at one emulated second");
    srtcWrite(sb,sharp); srtc.clockHardware(NTSC_MASTER-1); srtc.reset(); srtc.clockHardware(1);
    expect(srtcSecond(sb)==1,"S-RTC divider/time survive console reset");
    srtcWrite(sb,sharp); srtc.clockHardware(NTSC_MASTER/2);
    auto sstate=srtc.saveHardwareState(); auto stag=srtc.hardwareStateTag();
    srtc.clockHardware(NTSC_MASTER-NTSC_MASTER/2);
    expect(srtcSecond(sb)==1,"S-RTC reaches next second after checkpoint");
    expect(srtc.loadHardwareState(stag,sstate),"S-RTC in-flight divider state restores");
    srtc.clockHardware(NTSC_MASTER-NTSC_MASTER/2);
    expect(srtcSecond(sb)==1,"restored S-RTC divider replays deterministically");
    Cartridge spc; spc.setSRAMPersistence(false);
    expect(spc.loadData(spcRtcRom(),"/tmp/phase32_spc7110rtc.sfc"),"SPC7110 RTC fixture loads");
    Bus pb; pb.attachCartridge(&spc);
    const uint8_t epson[16]={0,0,0,0,0,0,1,0,1,0,6,2,4,0,0,0};
    spcWriteRtc(spc,pb,epson);
    spc.clockHardware(NTSC_MASTER-10000);
    expect(spcRtcSecond(spc,pb)==0,"RTC-4513 does not tick early");
    spc.clockHardware(10000);
    expect(spcRtcSecond(spc,pb)==1,"RTC-4513 ticks from emulated elapsed time");
    auto held=epson; (void)held;
    pb.write(0x004840,1); pb.write(0x004841,3); rtcWait(spc); pb.write(0x004841,13); rtcWait(spc); pb.write(0x004841,1); rtcWait(spc);
    spc.clockHardware(NTSC_MASTER);
    expect(spcRtcSecond(spc,pb)==1,"RTC-4513 hold blocks calendar tick");
    pb.write(0x004840,1); pb.write(0x004841,3); rtcWait(spc); pb.write(0x004841,13); rtcWait(spc); pb.write(0x004841,0); rtcWait(spc);
    expect(spcRtcSecond(spc,pb)==2,"RTC-4513 hold release commits one deferred second");
    spc.clockHardware(NTSC_MASTER);
    expect(spcRtcSecond(spc,pb)==3,"RTC-4513 resumes normal ticking after hold release");
    const uint8_t noCalendar[16]={9,5,9,5,3,2,1,0,1,0,6,2,4,0,0,0};
    spcWriteRtc(spc,pb,noCalendar); spc.clockHardware(NTSC_MASTER);
    expect(spcRtcSecond(spc,pb)==0&&spcRtcDay(spc,pb)==1,"RTC-4513 calendar-disabled midnight leaves date unchanged");
    const uint8_t calendarOn[16]={9,5,9,5,3,2,1,0,1,0,6,2,4,2,0,0};
    spcWriteRtc(spc,pb,calendarOn); spc.clockHardware(NTSC_MASTER);
    expect(spcRtcSecond(spc,pb)==0&&spcRtcDay(spc,pb)==2,"RTC-4513 calendar-enabled midnight advances date");
    if(failures){std::cerr<<failures<<" cartridge RTC clock regressions failed\n";return 1;}
    std::cout<<"phase32 cartridge RTC clock-domain accuracy ok\n";
    return 0;
}

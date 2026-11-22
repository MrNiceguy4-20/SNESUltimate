#include "bus.hpp"
#include "cartridge.hpp"
#include <cstdint>
#include <iostream>
#include <vector>
namespace {
int failures=0;
void expect(bool c,const char* m){if(!c){std::cerr<<"FAIL: "<<m<<'\n';++failures;}}
void w16(std::vector<uint8_t>& r,size_t o,uint16_t v){r[o]=uint8_t(v);r[o+1]=uint8_t(v>>8);}
std::vector<uint8_t> makeRom(){
    std::vector<uint8_t> r(6*1024*1024);
    for(size_t i=0;i<r.size();++i) r[i]=uint8_t((i*29+7)&255);
    const size_t h=0xffc0; for(int i=0;i<21;++i)r[h+i]=' ';
    r[h+0x15]=0x3a;r[h+0x16]=0xf5;r[h+0x17]=0x0d;r[h+0x18]=0x03;r[h+0x19]=1;
    w16(r,h+0x1c,0xedcb);w16(r,h+0x1e,0x1234);w16(r,h+0x3c,0x8000);
    r[0x100100]=0x42;r[0x100101]=0x99;r[0x100102]=0x77;
    return r;
}
}
int main(){
    Cartridge c;c.setSRAMPersistence(false);expect(c.loadData(makeRom(),"/tmp/spc7110-timing.sfc"),"load SPC7110 test ROM");
    Bus b;b.attachCartridge(&c);expect(c.hardwareNeedsClock(),"SPC7110 participates in master-clock scheduling");
    b.write(0x004811,0x00);b.write(0x004812,0x01);b.write(0x004813,0x00);
    expect(b.read(0x004810)==0x42,"4810 returns current prefetched data-ROM byte");
    expect(b.read(0x004811)==0x01,"4810 advances base pointer after read");
    b.write(0x004818,0x60);b.write(0x004814,1);b.write(0x004815,0);
    expect(b.read(0x00481a)==0,"481A side-effect read returns zero");
    expect(b.read(0x004811)==0x02,"481A adds adjust to base pointer");
    b.write(0x004820,2);b.write(0x004821,0);b.write(0x004824,3);b.write(0x004825,0);
    expect((b.read(0x00482f)&0x80)!=0,"482F busy asserted by multiply trigger");
    expect(b.read(0x004828)==0,"old ALU result remains visible while busy");
    c.clockHardware(29);expect((b.read(0x00482f)&0x80)!=0&&b.read(0x004828)==0,"multiply not early at 29 clocks");
    auto checkpoint=c.saveHardwareState();const auto tag=c.hardwareStateTag();
    c.clockHardware(1);expect((b.read(0x00482f)&0x80)==0&&b.read(0x004828)==6,"multiply retires at 30 clocks");
    expect(c.loadHardwareState(tag,checkpoint),"restore in-flight SPC7110 ALU state");
    c.clockHardware(1);expect((b.read(0x00482f)&0x80)==0&&b.read(0x004828)==6,"restored multiply retires identically");
    b.write(0x00482e,0);b.write(0x004820,0);b.write(0x004821,1);b.write(0x004822,0);b.write(0x004823,0);
    b.write(0x004826,3);b.write(0x004827,0);c.clockHardware(39);
    expect((b.read(0x00482f)&0x80)!=0,"division not early at 39 clocks");c.clockHardware(1);
    expect((b.read(0x00482f)&0x80)==0&&b.read(0x004828)==0x55&&b.read(0x00482c)==1,"division retires at 40 clocks");
    b.write(0x004830,0x80);b.write(0x006000,0xa6);expect(b.read(0x006000)==0xa6,"enabled SRAM visible");
    b.write(0x004830,0);expect(b.read(0x006000)==0,"disabled SRAM reads zero");
    if(failures){std::cerr<<failures<<" SPC7110 timing regressions failed\n";return 1;}
    std::cout<<"SPC7110 timing regression passed\n";return 0;
}

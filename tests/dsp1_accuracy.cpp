#include "bus.hpp"
#include "cartridge.hpp"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>
namespace { int fails=0; Cartridge* gc=nullptr; void ex(bool c,const char*m){if(!c){std::cerr<<"FAIL: "<<m<<'\n';++fails;}}
void w16(std::vector<uint8_t>&r,size_t o,uint16_t v){r[o]=static_cast<uint8_t>(v);r[o+1]=static_cast<uint8_t>(v>>8);}
std::vector<uint8_t> rom(){std::vector<uint8_t> r(2*1024*1024,0); size_t h=0x7fc0; for(int i=0;i<21;i++)r[h+i]=' '; r[h+0x15]=0x20;r[h+0x16]=0x03;r[h+0x17]=0x0b;r[h+0x19]=1;w16(r,h+0x1c,0xedcb);w16(r,h+0x1e,0x1234);w16(r,h+0x3c,0x8000);return r;}
void put(Bus&b,uint8_t v){if(gc) gc->clockHardware(5000); b.write(0x600000,v); if(gc) gc->clockHardware(5000);} void put16(Bus&b,uint16_t v){put(b,static_cast<uint8_t>(v));put(b,static_cast<uint8_t>(v>>8));} uint8_t get(Bus&b){if(gc) gc->clockHardware(5000); return b.read(0x600000);} int16_t get16(Bus&b){uint16_t l=get(b),h=get(b);return int16_t(l|(h<<8));}
}
int main(){Cartridge c;c.setSRAMPersistence(false);ex(c.loadData(rom(),"/tmp/dsp1.sfc"),"load");ex(c.hardwareType()==CartridgeHardware::DSP1,"detect DSP1");ex(c.hardwareImplemented(),"DSP1 implemented");Bus b;b.attachCartridge(&c);gc=&c;
put(b,0x00);put16(b,0x4000);put16(b,0x4000);ex(get16(b)==0x2000,"multiply Q15");ex(get(b)==0x80,"idle returns 80");
put(b,0x10); put16(b,3); put16(b,0); ex(get16(b)==21846,"inverse ROM/Newton coefficient for 3"); ex(get16(b)==14,"inverse exponent for 3");
put(b,0x10); put16(b,11); put16(b,0); ex(get16(b)==23832,"inverse ROM/Newton coefficient for 11"); ex(get16(b)==12,"inverse exponent for 11");
put(b,0x10); put16(b,0x8000); put16(b,0); ex(get16(b)==-16384,"inverse -8000 coefficient special case"); ex(get16(b)==1,"inverse -8000 exponent special case");
put(b,0x04);put16(b,0x4000);put16(b,0x4000);int16_t sn=get16(b),cs=get16(b);ex(sn>=0x3fff-2&&sn<=0x4001,"triangle sine quarter-turn");ex(cs>-4&&cs<4,"triangle cosine quarter-turn");
put(b,0x04);put16(b,0x0001);put16(b,0x7fff);ex(get16(b)==1,"triangle sine low-angle ROM interpolation");ex(get16(b)==0x7ffe,"triangle cosine low-angle ROM interpolation");
put(b,0x04);put16(b,0x8000);put16(b,0x7fff);ex(get16(b)==0,"triangle sine negative half-turn special case");ex(get16(b)==int16_t(0x8001),"triangle cosine negative half-turn special case");
put(b,0x08);put16(b,100);put16(b,200);put16(b,300);uint32_t rr=uint16_t(get16(b));rr|=uint32_t(uint16_t(get16(b)))<<16;ex(rr==2u*(10000u+40000u+90000u),"radius squared");
put(b,0x0c);put16(b,0x4000);put16(b,1000);put16(b,0);ex(get16(b)>=-2&&get16(b)<=-998,"rotate executes");
put(b,0x01);put16(b,0x7ffe);put16(b,0);put16(b,0);put16(b,0);ex(get(b)==0x80,"attitude has no output");put(b,0x0d);put16(b,1000);put16(b,0);put16(b,0);int16_t f=get16(b),l=get16(b),u=get16(b);ex(f>490&&f<510&&l==0&&u==0,"objective matrix A");
put(b,0x02); put16(b,100); put16(b,200); put16(b,1000); put16(b,0); put16(b,256); put16(b,0); put16(b,0x2000);
int16_t vof=get16(b),vva=get16(b),cx=get16(b),cy=get16(b);
ex(vof==0,"parameter VOF baseline"); ex(vva>=-257&&vva<=-254,"parameter VVA fixed-point cotangent");
ex(cx==100&&cy>=-801&&cy<=-798,"parameter shifts projection center along clipped horizon"); ex(get(b)==0x80,"parameter completes with ready value");
put(b,0x0a); put16(b,1); int16_t a0=get16(b),b0=get16(b),c0=get16(b),d0=get16(b); int16_t a1=get16(b),b1=get16(b),c1=get16(b),d1=get16(b);
ex(a0!=0&&d0!=0,"raster emits Mode7 matrix"); ex(a0!=a1||b0!=b1||c0!=c1||d0!=d1,"raster stream advances scanline without new command");
put(b,0x02); put16(b,0); put16(b,0); put16(b,1200); put16(b,0); put16(b,512); put16(b,0x1800); put16(b,0x5000);
int16_t clipVof=get16(b), clipVva=get16(b), clipCx=get16(b), clipCy=get16(b);
ex(clipVof!=0,"parameter zenith clipping produces VOF");
ex(clipVva<0,"parameter clipped VVA remains finite");
ex(clipCx!=0||clipCy!=0,"parameter clipping adjusts projection center");
put(b,0x0a); put16(b,clipVof); int16_t ca=get16(b),cb=get16(b),cc=get16(b),cd=get16(b);
ex(ca!=0&&cd!=0,"clipped raster keeps nonzero scale");
ex(ca!=cd||cb!=0||cc!=0,"clipped raster preserves view rotation");
put(b,0x06); put16(b,100); put16(b,200); put16(b,1000); int16_t ph=get16(b),pv=get16(b),pm=get16(b); (void)ph; (void)pv; ex(pm!=0,"project emits magnification"); ex(get(b)==0x80,"project completes");
put(b,0x0e); put16(b,0); put16(b,1); int16_t tx=get16(b),ty=get16(b); ex(tx!=0||ty!=0,"target emits ground coordinate"); ex(get(b)==0x80,"target completes");
put(b,0x14); put16(b,0); put16(b,0); put16(b,0); put16(b,0); put16(b,0); put16(b,321);
ex(get16(b)==0,"gyrate zero rotation preserves Z");
ex(get16(b)==0,"gyrate zero rotation preserves X");
ex(get16(b)==321,"gyrate local L rotation updates Y");
ex(get(b)==0x80,"gyrate completes without parser desync");
put(b,0x34); put16(b,100); put16(b,200); put16(b,300); put16(b,0); put16(b,0); put16(b,0);
ex(get16(b)==100&&get16(b)==200&&get16(b)==300,"gyrate alias 34 consumes all six words");
put(b,0x02); put16(b,0); put16(b,0); put16(b,1000); put16(b,0); put16(b,256); put16(b,0); put16(b,0x2000);
(void)get16(b); (void)get16(b); (void)get16(b); (void)get16(b);
put(b,0x0a); put16(b,0);
for(int i=0;i<8;++i) put(b,static_cast<uint8_t>(0x40+i));
put(b,0x00); put16(b,0x4000); put16(b,0x4000);
ex(get16(b)==0x2000,"raster pending-output writes do not desynchronize next command");
ex(get(b)==0x80,"raster discard returns command interface to ready state");
put(b,0x12); put16(b,100); put16(b,200); put16(b,1000); put16(b,0); put16(b,256); put16(b,0); put16(b,0x2000); ex(get16(b)==0,"parameter alias 12 accepted");
put(b,0x07); put16(b,0); ex(get16(b)==0,"RAM test alias 07 accepted");
put(b,0x27); put16(b,0); ex(get16(b)==0x0100,"memory-size alias 27 accepted");
{
    std::vector<uint8_t> smallRom(512 * 1024, 0);
    auto largeRom = rom();
    std::copy(largeRom.begin(), largeRom.begin() + smallRom.size(), smallRom.begin());
    Cartridge cs; cs.setSRAMPersistence(false);
    ex(cs.loadData(smallRom, "/tmp/dsp1-small.sfc"), "load DSP1 small map");
    ex(cs.hardwareType()==CartridgeHardware::DSP1, "detect DSP1 small map");
    Bus bs; bs.attachCartridge(&cs);
    auto sput=[&](uint8_t v){cs.clockHardware(5000);bs.write(0x308000,v);cs.clockHardware(5000);};
    auto sput16=[&](uint16_t v){sput(static_cast<uint8_t>(v));sput(static_cast<uint8_t>(v>>8));};
    auto sget=[&](){cs.clockHardware(5000);return bs.read(0x308000);};
    sput(0x00); sput16(0x4000); sput16(0x4000);
    cs.clockHardware(5000); ex(bs.read(0x30c000)==0x80, "DSP1 small-map status reports ready");
    ex(bs.read(0x30ffff)==0x80, "DSP1 small-map mirrored status reports ready");
    uint16_t product=uint16_t(sget()); product|=uint16_t(sget())<<8;
    ex(product==0x2000, "DSP1 status polling does not consume result bytes");
    cs.clockHardware(5000); bs.write(0x30c000,0x14); cs.clockHardware(5000);
    ex(bs.read(0x30c000)==0x80, "DSP1 status writes are ignored");
    sput(0x00); sput16(0x2000); sput16(0x4000);
    uint16_t product2=uint16_t(sget()); product2|=uint16_t(sget())<<8;
    ex(product2==0x1000, "DSP1 status write does not enter command parser");
}
put(b,0x00); put16(b,0x4000); put16(b,0x4000);
ex(b.read(0x604000)==0x80,"DSP large-map status reports ready");
ex(get16(b)==0x2000,"DSP large-map status polling preserves output");
ex(b.read(0x608000)!=0x80,"DSP large mapping does not own upper half");
if(fails){std::cerr<<fails<<" DSP1 regressions failed\n";return 1;}std::cout<<"DSP1 accuracy regression passed\n";}

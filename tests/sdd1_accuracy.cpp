#include "bus.hpp"
#include "cartridge.hpp"
#include "dma.hpp"
#include <cstdint>
#include <iostream>
#include <vector>
namespace {int f=0;void e(bool c,const char*m){if(!c){std::cerr<<"FAIL: "<<m<<'\n';++f;}}void w16(std::vector<uint8_t>&r,size_t o,uint16_t v){r[o]=v;r[o+1]=v>>8;}std::vector<uint8_t>rom(){std::vector<uint8_t>r(4*1024*1024);for(size_t i=0;i<r.size();++i)r[i]=uint8_t((i*37+11)&255);size_t h=0x7fc0;for(int i=0;i<21;i++)r[h+i]=' ';r[h+0x15]=0x32;r[h+0x16]=0x43;r[h+0x17]=0x0c;r[h+0x18]=0x03;r[h+0x19]=1;w16(r,h+0x1c,0xedcb);w16(r,h+0x1e,0x1234);w16(r,h+0x3c,0x8000);return r;} }
int main(){Cartridge c;c.setSRAMPersistence(false);auto r=rom();r[0]=0xc0;r[1]=0xaa;for(int i=2;i<128;i++)r[i]=uint8_t(i*13+7);r[0x100000]=0x5a; r[0x4802]=0x12; r[0x4803]=0x34; r[0x4808]=0x56; r[0x480f]=0x78; e(c.loadData(std::move(r),"/tmp/sdd1.sfc"),"load");e(c.hardwareType()==CartridgeHardware::SDD1,"detect");e(c.hardwareImplemented(),"implemented");Bus b;DMA d;b.attachCartridge(&c);b.attachDMA(&d);d.attachBus(&b);
e(b.read(0x004802)==0x12&&b.read(0x804803)==0x34,"register holes fall through to ROM");e(b.read(0x004808)==0x56&&b.read(0x00480f)==0x78,"upper register holes fall through to ROM");
b.write(0x004804,1);e(b.read(0xc00000)==0x5a,"C0-FF 1MiB bank remap");b.write(0x004804,0);
b.write(0x006000,0x91);e(b.read(0x806000)==0x91,"S-DD1 SRAM low-bank window");e(b.read(0x700000)==0x91,"S-DD1 SRAM $70 full-bank mirror");
b.write(0x002181,0);b.write(0x002182,0);b.write(0x002183,0);b.write(0x004300,0x08);b.write(0x004301,0x80);b.write(0x004302,0x00);b.write(0x004303,0x00);b.write(0x004304,0xc0);b.write(0x004305,16);b.write(0x004306,0);b.write(0x004800,1);b.write(0x004801,1);
c.clockHardware(160);
d.startDMA(1,0);while(d.dmaActive())d.stepDMA();
const auto&w=b.wram();
static const uint8_t expected[16]={0xa0,0x45,0x88,0xcb,0xde,0xaa,0x95,0x88,0xc6,0x65,0x44,0x3d,0xc3,0xc2,0x5a,0x10};
bool match=true;for(int i=0;i<16;i++)if(w[i]!=expected[i])match=false;e(match,"DMA receives S-DD1 decompressed stream");e(b.read(0x004801)==0,"decompression channel auto-clears after DMA");
if(f){std::cerr<<f<<" S-DD1 regressions failed\n";return 1;}std::cout<<"S-DD1 accuracy regression passed\n";}

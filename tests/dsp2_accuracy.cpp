#include "bus.hpp"
#include "cartridge.hpp"
#include <cstdint>
#include <iostream>
#include <vector>
namespace{int f=0;void e(bool c,const char*m){if(!c){std::cerr<<"FAIL: "<<m<<'\n';f++;}}void w16(std::vector<uint8_t>&r,size_t o,uint16_t v){r[o]=static_cast<uint8_t>(v);r[o+1]=static_cast<uint8_t>(v>>8);}std::vector<uint8_t> rom(){std::vector<uint8_t>r(2*1024*1024);size_t h=0x7fc0;for(int i=0;i<21;i++)r[h+i]=' ';r[h+0x15]=0x20;r[h+0x16]=0x05;r[h+0x17]=0x0b;r[h+0x19]=1;w16(r,h+0x1c,0xedcb);w16(r,h+0x1e,0x1234);w16(r,h+0x3c,0x8000);return r;}constexpr uint32_t port=0x208000;constexpr uint32_t mirror=0xa08000;void p(Bus&b,uint8_t v){b.write(port,v);}uint8_t g(Bus&b){return b.read(port);}}
int main(){Cartridge c;c.setSRAMPersistence(false);e(c.loadData(rom(),"/tmp/dsp2.sfc"),"load");e(c.hardwareType()==CartridgeHardware::DSP2,"detect");e(c.hardwareImplemented(),"implemented");Bus b;b.attachCartridge(&c);
p(b,0x09);p(b,0x34);p(b,0x12);p(b,0x02);p(b,0x00);uint32_t v=g(b);v|=uint32_t(g(b))<<8;v|=uint32_t(g(b))<<16;v|=uint32_t(g(b))<<24;e(v==0x2468,"multiply");
p(b,0x03);p(b,0x0f);p(b,0x05);p(b,2);p(b,0x12);p(b,0x34);p(b,0xf5);p(b,0x6f);e(g(b)==0x15&&g(b)==0x64,"transparent overlay");
p(b,0x06);p(b,3);p(b,0x12);p(b,0x34);p(b,0xab);e(g(b)==0xba&&g(b)==0x43&&g(b)==0x21,"reverse nibbles");
p(b,0x01);for(int i=0;i<32;i++)p(b,0);bool all0=true;for(int i=0;i<32;i++)all0&=g(b)==0;e(all0,"bitmap conversion zero tile");
const uint8_t tileIn[32]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31};
const uint8_t tileOut[32]={0x11,0x05,0x11,0x05,0x11,0x05,0x11,0x05,0xbb,0x05,0xbb,0x05,0xbb,0x05,0xbb,0x05,0x00,0x00,0x55,0x00,0x00,0x55,0x55,0x55,0x00,0x00,0x55,0x00,0x00,0x55,0x55,0x55};
p(b,0x01);for(uint8_t x:tileIn)p(b,x);for(uint8_t x:tileOut)e(g(b)==x,"bitmap conversion nonzero oracle");
p(b,0x05);p(b,0x00);e(g(b)==0x00&&g(b)==0x00,"Op05 size-zero DR hold");
p(b,0x09);p(b,0x02);p(b,0);p(b,0x03);p(b,0);e(g(b)==6,"size-zero quirk does not desync next command");
b.write(mirror,0x09);b.write(mirror,0x02);b.write(mirror,0);b.write(mirror,0x03);b.write(mirror,0);e(b.read(mirror)==6,"DSP2 data-port mirror");
if(f){std::cerr<<f<<" DSP2 regressions failed\n";return 1;}std::cout<<"DSP2 accuracy regression passed\n";}

#include "bus.hpp"
#include "cartridge.hpp"
#include <cstdint>
#include <iostream>
#include <vector>
namespace{int f=0;void e(bool c,const char*m){if(!c){std::cerr<<"FAIL: "<<m<<'\n';++f;}}void w16(std::vector<uint8_t>&r,size_t o,uint16_t v){r[o]=v;r[o+1]=v>>8;}std::vector<uint8_t>rom(){std::vector<uint8_t>r(1024*1024);size_t h=0x7fc0;for(int i=0;i<21;i++)r[h+i]=' ';r[h+0x15]=0x35;r[h+0x16]=0x55;r[h+0x17]=0x0a;r[h+0x18]=0x03;r[h+0x19]=1;w16(r,h+0x1c,0xedcb);w16(r,h+0x1e,0x1234);w16(r,h+0x3c,0x8000);return r;}}
int main(){Cartridge c;c.setSRAMPersistence(false);e(c.loadData(rom(),"/tmp/srtc.sfc"),"load");e(c.hardwareType()==CartridgeHardware::SRTC,"detect");e(c.hardwareImplemented(),"implemented");auto before=c.sramSize();Bus b;b.attachCartridge(&c);e(b.read(0x002800)==0x0f,"read sequence begins with sentinel");
b.write(0x002801,0x0e);b.write(0x002801,0);uint8_t vals[12]={5,4,3,2,1,0,1,0,1,6,2,0};for(auto v:vals)b.write(0x002801,v);b.write(0x002801,0x0d);e(b.read(0x002800)==0x0f,"read-mode sentinel after write");bool ok=true;for(auto v:vals){if((b.read(0x002800)&0x0f)!=v){ok=false;break;}}e(ok,"S-RTC date/time nibbles round-trip");uint8_t wd=b.read(0x002800);e(wd<=6,"weekday generated");e(b.read(0x002800)==0x0f,"read sequence terminates with sentinel");e(c.sramSize()==before,"RTC storage does not consume game SRAM");
if(f){std::cerr<<f<<" S-RTC regressions failed\n";return 1;}std::cout<<"S-RTC accuracy regression passed\n";}

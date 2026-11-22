#include "bus.hpp"
#include "cartridge.hpp"
#include <cstdint>
#include <iostream>
#include <vector>
namespace {int f=0;void e(bool c,const char*m){if(!c){std::cerr<<"FAIL: "<<m<<'\n';++f;}}void w16(std::vector<uint8_t>&r,size_t o,uint16_t v){r[o]=static_cast<uint8_t>(v);r[o+1]=static_cast<uint8_t>(v>>8);}std::vector<uint8_t> rom(bool rtc){std::vector<uint8_t>r(6*1024*1024);for(size_t i=0;i<r.size();i++)r[i]=uint8_t((i*17+3)&255);size_t h=0xffc0;for(int i=0;i<21;i++)r[h+i]=' ';r[h+0x15]=0x3a;r[h+0x16]=rtc?0xf9:0xf5;r[h+0x17]=0x0d;r[h+0x18]=0x03;r[h+0x19]=1;w16(r,h+0x1c,0xedcb);w16(r,h+0x1e,0x1234);w16(r,h+0x3c,0x8000);return r;}}
int main(){Cartridge c;c.setSRAMPersistence(false);auto r=rom(true);r[0x100000]=0x5a;r[0x200000]=0xa5;r[0x100100]=0x77;
r[0x100000]=0;r[0x100001]=0;r[0x100002]=1;r[0x100003]=0;r[0x100004]=1;r[0x100005]=0;r[0x100006]=1;r[0x100007]=0;r[0x100008]=2;r[0x100009]=0;r[0x10000a]=1;r[0x10000b]=0;e(c.loadData(std::move(r),"/tmp/spc7110.sfc"),"load");e(c.hardwareType()==CartridgeHardware::SPC7110RTC,"detect RTC variant");e(c.hardwareImplemented(),"implemented");Bus b;b.attachCartridge(&c);
e(b.read(0xd00100)==0x77,"D0 window defaults to data-ROM bank 0");b.write(0x004834,3);b.write(0x004831,1);e(b.read(0xd00000)==0xa5,"4831 remaps D0-DF window");b.write(0x004831,0);
b.write(0x004811,0x00);b.write(0x004812,0x01);b.write(0x004813,0x00);e(b.read(0x004810)==0x77,"data port reads data ROM and auto increments pointer");e(b.read(0x004811)==0x01,"data pointer auto increments");
b.write(0x004820,0x34);b.write(0x004821,0x12);b.write(0x004824,3);b.write(0x004825,0);
e((b.read(0x00482f)&0x80)!=0,"multiply busy flag asserts");
e(b.read(0x004828)==0,"multiply result is not published immediately");
c.clockHardware(29);e((b.read(0x00482f)&0x80)!=0&&b.read(0x004828)==0,"multiply remains in flight through clock 29");
auto pending=c.saveHardwareState();uint32_t tag=c.hardwareStateTag();
c.clockHardware(1);uint32_t prod=b.read(0x004828)|(uint32_t(b.read(0x004829))<<8)|(uint32_t(b.read(0x00482a))<<16)|(uint32_t(b.read(0x00482b))<<24);e(prod==0x369c&&((b.read(0x00482f)&0x80)==0),"multiply completes at clock 30");
e(c.loadHardwareState(tag,pending),"restore in-flight multiply");c.clockHardware(1);e(b.read(0x004828)==0x9c&&((b.read(0x00482f)&0x80)==0),"in-flight ALU state replays deterministically");
b.write(0x00482e,0);b.write(0x004820,0);b.write(0x004821,0);b.write(0x004822,1);b.write(0x004823,0);b.write(0x004826,3);b.write(0x004827,0);c.clockHardware(39);e((b.read(0x00482f)&0x80)!=0,"division remains busy through clock 39");c.clockHardware(1);e(b.read(0x004828)==0x55&&b.read(0x004829)==0x55&&b.read(0x00482c)==1&&((b.read(0x00482f)&0x80)==0),"division completes at clock 40");
b.write(0x004811,0x00);b.write(0x004812,0x01);b.write(0x004813,0x00);b.write(0x004818,0x60);b.write(0x004814,2);b.write(0x004815,0);e(b.read(0x00481a)==0,"481A seek port returns zero");e(b.read(0x004811)==0x02&&b.read(0x004812)==0x01,"481A applies 16-bit adjust to data pointer");
b.write(0x004808,0xa5);e(b.read(0x004808)==0&&b.read(0x580123)==0,"4808/58 mirror is read-zero write-discard");
b.write(0x006000,0x5a);e(b.read(0x006000)==0,"disabled SPC7110 SRAM reads zero");b.write(0x004830,0x80);b.write(0x006000,0x5a);e(b.read(0x006000)==0x5a,"enabled SPC7110 SRAM is readable/writable");b.write(0x004830,0);e(b.read(0x006000)==0,"disabling SPC7110 SRAM disconnects reads");
b.write(0x004801,0);b.write(0x004802,0);b.write(0x004803,0);b.write(0x004804,0);b.write(0x004805,0);b.write(0x004806,0);e((b.read(0x00480c)&0x80)==0,"decompression not complete immediately");c.clockHardware(20);e((b.read(0x00480c)&0x80)!=0,"decompression init completion flag");e(b.read(0x500123)==0x5e,"bank 50 mirrors decompression read port");e(b.read(0x004800)==0xae&&b.read(0x004800)==0xe0&&b.read(0x004800)==0x20,"SPC7110 mode0 arithmetic decoder vector");
b.write(0x004804,1);b.write(0x004806,0);c.clockHardware(20);e(b.read(0x004800)==0x28&&b.read(0x004800)==0xa2,"SPC7110 mode1 planar decoder vector");
b.write(0x004804,2);b.write(0x004806,0);c.clockHardware(20);e(b.read(0x004800)==0x4a&&b.read(0x004800)==0xf1,"SPC7110 mode2 planar decoder vector");
b.write(0x004840,1);e((b.read(0x004842)&0x80)!=0,"RTC enable status");b.write(0x004841,3);c.clockHardware(82);b.write(0x004841,13);c.clockHardware(82);b.write(0x004841,1);c.clockHardware(82);b.write(0x004840,1);b.write(0x004841,12);c.clockHardware(82);b.write(0x004841,13);c.clockHardware(82);e((b.read(0x004841)&0x0f)==1,"SPC7110 RTC register write/read");
if(f){std::cerr<<f<<" SPC7110 regressions failed\n";return 1;}std::cout<<"SPC7110 accuracy regression passed\n";}

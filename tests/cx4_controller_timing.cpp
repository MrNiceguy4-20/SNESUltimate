#include "cx4.hpp"
#include <cstdint>
#include <iostream>
#include <vector>
namespace {
int fails=0; void expect(bool c,const char* m){if(!c){std::cerr<<"FAIL: "<<m<<'\n';++fails;}}
uint8_t rr(CartridgeDevice& d,uint16_t a){uint8_t v=0xff;expect(d.read(a,v),"Cx4 read owned");return v;}
void ww(CartridgeDevice& d,uint16_t a,uint8_t v){expect(d.write(a,v),"Cx4 write owned");}
std::vector<uint8_t> rom(){std::vector<uint8_t> r(1024*1024);for(size_t i=0;i<r.size();++i)r[i]=uint8_t(i*37u+11u);return r;}
}
int main(){
 auto r=rom();
 {
  auto d=createCX4Device(r,21477272u); d->reset();
  ww(*d,0x007f4f,0x89); expect((rr(*d,0x7f5e)&0xc0)==0x40,"command sets RUNNING but not BUSY");
  d->clock(2); expect((rr(*d,0x7f5e)&0x40)==0,"command eventually halts");
  ww(*d,0x7f80,0x12);ww(*d,0x7f81,0x34);ww(*d,0x7f82,0x56);
  expect(rr(*d,0x7fc0)==0x12&&rr(*d,0x7fc1)==0x34&&rr(*d,0x7fc2)==0x56,"upper GPR window mirrors lower GPR window");
  ww(*d,0x7fc1,0xa5);expect(rr(*d,0x7f81)==0xa5,"GPR mirror writes alias same storage");
  ww(*d,0x7fb0,0xa5);ww(*d,0x7fff,0x5a);
  expect(rr(*d,0x7fb0)==0&&rr(*d,0x7fff)==0,"reserved Cx4 register windows read zero and ignore writes");
 }
 {
  auto d=createCX4Device(r,21477272u); d->reset();
  ww(*d,0x7f40,0x00);ww(*d,0x7f41,0x80);ww(*d,0x7f42,0x00);
  ww(*d,0x7f43,0x02);ww(*d,0x7f44,0x00);ww(*d,0x7f45,0x00);ww(*d,0x7f46,0x01);ww(*d,0x7f47,0x00);
  expect((rr(*d,0x7f5e)&0xc0)==0xc0,"DMA asserts RUNNING+BUSY");
  d->clock(5);expect(rr(*d,0x6100)==0x00,"first DMA byte unavailable before five Cx4 cycles elapse");
  d->clock(1);expect(rr(*d,0x6100)==r[0],"first DMA byte commits at first byte deadline");expect((rr(*d,0x7f5e)&0x80)!=0,"second DMA byte keeps BUSY asserted");
  d->clock(5);expect(rr(*d,0x6101)==r[1],"second DMA byte commits progressively");expect((rr(*d,0x7f5e)&0xc0)==0,"DMA completion clears RUNNING+BUSY without command IRQ");
 }
 {
  auto d=createCX4Device(r,21477272u); d->reset();
  ww(*d,0x7f50,0x00);expect(rr(*d,0x7f50)==0,"wait-state register masks/reads implemented bits");
  ww(*d,0x7f40,0x00);ww(*d,0x7f41,0x80);ww(*d,0x7f42,0x00);ww(*d,0x7f43,0x01);ww(*d,0x7f44,0);ww(*d,0x7f45,0x02);ww(*d,0x7f46,0x01);ww(*d,0x7f47,0);
  d->clock(2);expect(rr(*d,0x6102)==0,"two master clocks are only one Cx4 cycle from phase zero");
  d->clock(1);expect(rr(*d,0x6102)==r[0],"ROMWAIT=0 completes ROM->DRAM DMA after two Cx4 cycles");
 }
 {
  auto d=createCX4Device(r,21477272u); d->reset();ww(*d,0x7f50,0x00);
  ww(*d,0x7f48,0);expect((rr(*d,0x7f5e)&0x80)!=0,"cache preload asserts BUSY");
  d->clock(549);expect((rr(*d,0x7f5e)&0x80)!=0,"cache preload remains busy one Cx4 cycle short");
  auto st=d->saveState(); d->clock(1);expect((rr(*d,0x7f5e)&0x80)==0,"cache preload reaches completion on next phase-advancing master clock");
  expect(d->loadState(st),"in-flight cache state restores");d->clock(1);expect((rr(*d,0x7f5e)&0x80)==0,"restored cache preload reaches identical completion boundary");
 }
  {
    auto d=createCX4Device(r,21477272u); d->reset();
    uint8_t a=0,b=0,c=0; d->read(0x6c03,a); d->read(0x6c23,b); d->read(0x7c03,c);
    expect(a==b && a==c,"Cx4 garbage windows repeat the same 32-byte pattern across mirrors");
    d->write(0x6c03,uint8_t(a^0xff)); uint8_t after=0; d->read(0x6c03,after);
    expect(after==a,"Cx4 garbage-window writes are ignored rather than becoming RAM");
  }
if(fails){std::cerr<<fails<<" Cx4 controller regressions failed\n";return 1;}std::cout<<"phase34 Cx4 controller/bus timing accuracy ok\n";
}

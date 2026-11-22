#include "cartridge_device.hpp"
#include "spc7110.hpp"
#include <cstdint>
#include <iostream>
#include <vector>
namespace {
int f=0;
void e(bool c,const char*m){if(!c){std::cerr<<"FAIL: "<<m<<'\n';++f;}}
uint8_t rr(CartridgeDevice& d,uint32_t a){uint8_t v=0xff;e(d.readHost(a,0xff,v),"SPC7110 host-MMIO read handled");return v;}
}
int main(){
 constexpr uint32_t F=21477272u; std::vector<uint8_t> rom(6*1024*1024),ram(0x2000),rtc(20);bool d=false,rd=false;
 rtc[0]=7;rtc[1]=2;rtc[2]=4; auto x=createSPC7110Device(rom,ram,d,rtc,rd,true,F);
 x->write(0x004840,1);e(rr(*x,0x004842)==0x80,"enable asserts RTC ready");
 x->write(0x004841,3);e(rr(*x,0x004842)==0,"mode write starts eight-clock busy window");e(rr(*x,0x004842)==0,"status reads do not acknowledge busy");
 x->clock(81);e(rr(*x,0x004842)==0,"RTC stays busy through master clock 81");x->clock(1);e(rr(*x,0x004842)==0x80,"RTC becomes ready at eight internal clocks");
 x->write(0x004841,0);x->write(0x004841,9);e(rr(*x,0x004842)==0,"write while busy is ignored and does not bypass wait");x->clock(82);x->write(0x004841,9);x->clock(82);e(rr(*x,0x004841)==9,"write-mode data port echoes MDR while ready");
 x->write(0x004840,1);x->write(0x004841,12);x->clock(82);x->write(0x004841,0);x->clock(82);e(rr(*x,0x004841)==9,"read mode returns indexed RTC nibble");e(rr(*x,0x004842)==0,"successful RTC read starts busy window");e(rr(*x,0x004841)==0,"read while busy returns zero without advancing");x->clock(82);e(rr(*x,0x004841)==2,"next read advances exactly one register after wait");
 x->write(0x004840,1);x->write(0x004841,12);x->clock(41);auto st=x->saveState();x->clock(40);e(rr(*x,0x004842)==0,"81 clocks from mode write is still busy across split timing");x->clock(1);e(rr(*x,0x004842)==0x80,"82nd clock completes split busy window");e(x->loadState(st),"RTC interface in-flight state restores");x->clock(40);e(rr(*x,0x004842)==0,"restored interface phase preserves one-clock-short busy state");x->clock(1);e(rr(*x,0x004842)==0x80,"restored interface reaches same completion boundary");
 if(f){std::cerr<<f<<" RTC interface regressions failed\n";return 1;}std::cout<<"phase33 SPC7110 RTC interface timing accuracy ok\n";
}

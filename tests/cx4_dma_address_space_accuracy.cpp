#include "cx4.hpp"
#include <cstdint>
#include <iostream>
#include <vector>
namespace {
int fails=0;
void expect(bool c,const char* m){if(!c){std::cerr<<"FAIL: "<<m<<'\n';++fails;}}
uint8_t rr(CartridgeDevice& d,uint16_t a){uint8_t v=0xff;expect(d.read(a,v),"Cx4 read owned");return v;}
void ww(CartridgeDevice& d,uint16_t a,uint8_t v){expect(d.write(a,v),"Cx4 write owned");}
void w24(CartridgeDevice& d,uint16_t a,uint32_t v){ww(d,a,uint8_t(v));ww(d,uint16_t(a+1),uint8_t(v>>8));ww(d,uint16_t(a+2),uint8_t(v>>16));}
void beginDma(CartridgeDevice& d,uint32_t src,uint32_t dst,uint16_t len){
 w24(d,0x7f40,src);ww(d,0x7f43,uint8_t(len));ww(d,0x7f44,uint8_t(len>>8));w24(d,0x7f45,dst);
}
std::vector<uint8_t> makeRom(){std::vector<uint8_t> r(2*1024*1024);for(size_t i=0;i<r.size();++i)r[i]=uint8_t((i*29u+7u)&0xffu);return r;}
}
int main(){
 auto rom=makeRom();
 {
  auto d=createCX4Device(rom,21477272u);d->reset();ww(*d,0x6100,0xa5);ww(*d,0x6102,0x00);
  beginDma(*d,0x006100,0x006102,1);d->clock(2);expect(rr(*d,0x6102)==0x00,"DRAM->DRAM waits two HG51B cycles");
  d->clock(1);expect(rr(*d,0x6102)==0xa5,"DRAM->DRAM commits after one-cycle read plus one-cycle write");
 }
 {
  auto d=createCX4Device(rom,21477272u);d->reset();ww(*d,0x6103,0xcc);
  beginDma(*d,0x700000,0x006103,1);d->clock(5);expect(rr(*d,0x6103)==0xcc,"disconnected RAM source still pays configured RAM wait");
  d->clock(1);expect(rr(*d,0x6103)==0x00,"disconnected cartridge RAM reads as zero after RAM->DRAM transfer");
 }
 {
  auto d=createCX4Device(rom,21477272u);d->reset();ww(*d,0x6100,0xa5);
  beginDma(*d,0x006100,0x007f50,1);d->clock(3);expect(rr(*d,0x7f50)==0x25,"DRAM->IO DMA reaches the HG51B wait-state register");
  ww(*d,0x6104,0);beginDma(*d,0x007f50,0x006104,1);d->clock(3);expect(rr(*d,0x6104)==0x25,"IO->DRAM DMA reads dynamic HG51B register state");
 }
 {
  auto d=createCX4Device(rom,21477272u);d->reset();
  beginDma(*d,0xc00000,0x006105,1);d->clock(6);expect(rr(*d,0x6105)==rom[0],"C0:0000 DMA source uses HG51B Mapping-0 ROM mirror");
 }
 {
  auto d=createCX4Device(rom,21477272u);d->reset();
  beginDma(*d,0x008000,0x700000,1);d->clock(8);expect((rr(*d,0x7f5e)&0x80)!=0,"ROM->RAM remains busy one Cx4 cycle before allowed transfer completes");
  d->clock(1);expect((rr(*d,0x7f5e)&0x80)==0,"ROM->RAM same-bus different-chip DMA is allowed");
 }
 {
  auto d=createCX4Device(rom,21477272u);d->reset();
  beginDma(*d,0x000100,0x008000,1);d->clock(1000);expect((rr(*d,0x7f5e)&0xc0)==0xc0,"ROM destination locks HG51B DMA even with internal source");
  auto st=d->saveState();expect(!st.empty(),"locked DMA state serializes");d->reset();expect((rr(*d,0x7f5e)&0x80)==0,"reset clears DMA lock");
  expect(d->loadState(st),"locked DMA state restores");d->clock(1000);expect((rr(*d,0x7f5e)&0x80)!=0,"restored illegal DMA remains locked");
 }
 {
  auto d=createCX4Device(rom,21477272u);d->reset();
  beginDma(*d,0x004000,0x000100,1);d->clock(1000);expect((rr(*d,0x7f5e)&0x80)!=0,"unmapped DMA endpoint enters HG51B lock state");
 }
 {
  auto d=createCX4Device(rom,21477272u);d->reset();
  beginDma(*d,0x700000,0x710000,1);d->clock(1000);expect((rr(*d,0x7f5e)&0x80)!=0,"RAM->RAM DMA also enters HG51B lock state");
 }
 if(fails){std::cerr<<fails<<" Cx4 DMA address-space regressions failed\n";return 1;}
 std::cout<<"phase35 Cx4 HG51B DMA address-space accuracy ok\n";
}

#include "cx4.hpp"
#include <cstdint>
#include <iostream>
#include <vector>
static int fails=0;
#define CHECK(x,m) do{if(!(x)){std::cerr<<"FAIL: "<<m<<"\n";++fails;}}while(0)
static uint8_t rr(CartridgeDevice& d,uint16_t a){uint8_t v=0xff;CHECK(d.read(a,v),"Cx4 read owned");return v;}
static void ww(CartridgeDevice& d,uint16_t a,uint8_t v){CHECK(d.write(a,v),"Cx4 write owned");}
int main(){
 std::vector<uint8_t> rom(2*1024*1024,0xff), data(3072,0);
 const uint16_t code[]={0x642a,0xe060,0xfc00};
 for(unsigned i=0;i<3;++i){rom[i*2]=uint8_t(code[i]);rom[i*2+1]=uint8_t(code[i]>>8);}
 auto d=createCX4Device(rom,21477272u,data); d->reset();
 CHECK(d->supportLevel()==CartridgeSupportLevel::FirmwareLLE,"data-ROM-backed Cx4 advertises LLE path");
 ww(*d,0x7f49,0x00);ww(*d,0x7f4a,0x80);ww(*d,0x7f4b,0x00);
 ww(*d,0x7f4d,0x00);ww(*d,0x7f4e,0x00);
 ww(*d,0x7f48,0x00);
 CHECK((rr(*d,0x7f5e)&0xc0)==0xc0,"cache fill reports RUNNING+BUSY");
 ww(*d,0x7f4f,0x00);CHECK((rr(*d,0x7f5e)&0x80)!=0,"start ignored while cache BUSY");
 d->clock(2300);CHECK((rr(*d,0x7f5e)&0xc0)==0,"manual cache preload completes idle");
 ww(*d,0x7f4f,0x00);CHECK((rr(*d,0x7f5e)&0x40)!=0,"$7f4f starts HG51B at requested PC");
 auto cachedState=d->saveState();
 d->reset();CHECK(d->loadState(cachedState),"Cx4 LLE state restores cached HG51B program page");
 ww(*d,0x7f4f,0x00);
 d->clock(20);
 CHECK((rr(*d,0x7f5e)&0x40)==0,"HG51B HALT clears RUNNING");
 CHECK(rr(*d,0x7f80)==0x2a&&rr(*d,0x7f81)==0&&rr(*d,0x7f82)==0,"HG51B result reaches host GPR window");
 CHECK((rr(*d,0x7f5e)&0x02)!=0,"HG51B HALT raises completion IRQ pending");
 d->reset();ww(*d,0x7f49,0x00);ww(*d,0x7f4a,0x80);ww(*d,0x7f4b,0x00);ww(*d,0x7f4d,0);ww(*d,0x7f4e,0);
 ww(*d,0x7f4f,0);CHECK((rr(*d,0x7f5e)&0xc0)==0xc0,"uncached start enters cache-fill BUSY state");
 d->clock(2350);CHECK(rr(*d,0x7f80)==0x2a,"automatic cache fill continues into HG51B execution");
 CHECK((rr(*d,0x7f5e)&0x40)==0,"auto-filled HG51B program halts normally");
 std::fill(rom.begin(),rom.end(),0xff);
 const uint16_t far0[]={0x6701,0x0a00};
 const uint16_t far1[]={0x6455,0xe060,0xfc00};
 for(unsigned i=0;i<2;++i){rom[i*2]=uint8_t(far0[i]);rom[i*2+1]=uint8_t(far0[i]>>8);}
 for(unsigned i=0;i<3;++i){const size_t o=0x200+i*2;rom[o]=uint8_t(far1[i]);rom[o+1]=uint8_t(far1[i]>>8);}
 auto f=createCX4Device(rom,21477272u,data);f->reset();
 ww(*f,0x7f49,0x00);ww(*f,0x7f4a,0x80);ww(*f,0x7f4b,0x00);ww(*f,0x7f4d,0);ww(*f,0x7f4e,0);
 ww(*f,0x7f4f,0);f->clock(5000);
 CHECK(rr(*f,0x7f80)==0x55,"far JMP P refills/resolves target program page before execution");
 CHECK((rr(*f,0x7f5e)&0x40)==0,"far target HALT returns Cx4 idle");
 auto l=createCX4Device(rom,21477272u,data);l->reset();
 ww(*l,0x7f49,0x00);ww(*l,0x7f4a,0x80);ww(*l,0x7f4b,0x00);ww(*l,0x7f4d,0);ww(*l,0x7f4e,0);
 ww(*l,0x7f48,0);l->clock(2300);ww(*l,0x7f4c,3);ww(*l,0x7f4f,0);l->clock(30);
 CHECK(rr(*l,0x7f80)!=0x55,"locked cache pages prevent runtime replacement");
 std::fill(rom.begin(),rom.end(),0xff);rom[0x1000]=0xab;
 const uint16_t busRead[]={0x6260,0x602e,0x1c00,0x6003,0xe061,0xfc00};
 for(unsigned i=0;i<6;++i){rom[i*2]=uint8_t(busRead[i]);rom[i*2+1]=uint8_t(busRead[i]>>8);}
 auto br=createCX4Device(rom,21477272u,data);br->reset();
 ww(*br,0x7f80,0x00);ww(*br,0x7f81,0x90);ww(*br,0x7f82,0x00);
 ww(*br,0x7f49,0x00);ww(*br,0x7f4a,0x80);ww(*br,0x7f4b,0);ww(*br,0x7f4d,0);ww(*br,0x7f4e,0);ww(*br,0x7f4f,0);
 br->clock(5000);
 CHECK(rr(*br,0x7f83)==0xab,"external ROM read completes through MDR after WAIT");
 std::fill(rom.begin(),rom.end(),0xff);
 const uint16_t busRam[]={0x6260,0x65cc,0xe12f,0x1c00,0x602f,0x1c00,0x6003,0xe062,0xfc00};
 for(unsigned i=0;i<9;++i){rom[i*2]=uint8_t(busRam[i]);rom[i*2+1]=uint8_t(busRam[i]>>8);}
 auto bm=createCX4Device(rom,21477272u,data);bm->reset();
 ww(*bm,0x7f80,0x00);ww(*bm,0x7f81,0x00);ww(*bm,0x7f82,0x70);
 ww(*bm,0x7f49,0);ww(*bm,0x7f4a,0x80);ww(*bm,0x7f4b,0);ww(*bm,0x7f4d,0);ww(*bm,0x7f4e,0);ww(*bm,0x7f4f,0);
 bm->clock(5000);
 CHECK(rr(*bm,0x7f86)==0xcc,"external cartridge RAM write/read round-trips through $2f");
 if(fails){std::cerr<<fails<<" Cx4 LLE checks failed\n";return 1;}std::cout<<"Cx4 HG51B cache/start LLE integration checks passed\n";return 0;
}

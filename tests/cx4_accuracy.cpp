#include "bus.hpp"
#include "cartridge.hpp"
#include <cstdint>
#include <iostream>
#include <vector>
namespace {int f=0;void e(bool c,const char*m){if(!c){std::cerr<<"FAIL: "<<m<<'\n';++f;}}void w16(std::vector<uint8_t>&r,size_t o,uint16_t v){r[o]=static_cast<uint8_t>(v);r[o+1]=static_cast<uint8_t>(v>>8);}std::vector<uint8_t>rom(){std::vector<uint8_t>r(1024*1024);for(int i=0;i<64;i++)r[i]=uint8_t(0x80+i);r[0x100]=1;r[0x101]=0x20;r[0x102]=2;r[0x103]=3;r[0x104]=4;
r[0x200]=0x83;r[0x201]=0x00;r[0x202]=0x83;r[0x203]=0x10;r[0x204]=0x01;
r[0x300]=0;r[0x301]=0;r[0x302]=0;r[0x303]=0;r[0x304]=0;r[0x305]=0;
r[0x310]=0;r[0x311]=10;r[0x312]=0;r[0x313]=0;r[0x314]=0;r[0x315]=0;size_t h=0x7fc0;for(int i=0;i<21;i++)r[h+i]=' ';r[h+0x15]=0x20;r[h+0x16]=0xf3;r[h+0x17]=0x0a;r[h+0x19]=1;w16(r,h+0x1c,0xedcb);w16(r,h+0x1e,0x1234);w16(r,h+0x3c,0x8000);return r;}void wb(Bus&b,uint16_t a,uint8_t v){b.write(a,v);}void ww(Bus&b,uint16_t a,uint16_t v){wb(b,a,static_cast<uint8_t>(v));wb(b,a+1,static_cast<uint8_t>(v>>8));}uint16_t rw(Bus&b,uint16_t a){return uint16_t(b.read(a))|(uint16_t(b.read(a+1))<<8);}void w24(Bus&b,uint16_t a,uint32_t v){wb(b,a,static_cast<uint8_t>(v));wb(b,a+1,static_cast<uint8_t>(v>>8));wb(b,a+2,static_cast<uint8_t>(v>>16));}uint32_t r24(Bus&b,uint16_t a){return uint32_t(b.read(a))|(uint32_t(b.read(a+1))<<8)|(uint32_t(b.read(a+2))<<16);} }
int main(){Cartridge c;c.setSRAMPersistence(false);e(c.loadData(rom(),"/tmp/cx4.sfc"),"load");e(c.hardwareType()==CartridgeHardware::CX4,"detect");e(c.hardwareImplemented(),"implemented");Bus b;b.attachCartridge(&c); auto finish=[&](){c.clockHardware(1000);};
wb(b,0x7f4f,0x89);e((b.read(0x7f5e)&0x40)!=0,"Cx4 command sets busy");finish();e((b.read(0x7f5e)&0x40)==0,"Cx4 command completion clears busy");
wb(b,0x7f4f,0x89);wb(b,0x7f56,0);c.clockHardware(20);e((b.read(0x7f5e)&0x41)==0x41,"Cx4 timed suspend keeps busy and sets suspend flag");finish();e((b.read(0x7f5e)&0x41)==0,"Cx4 resumes after timed suspend and completes");
ww(b,0x7f80,3);ww(b,0x7f83,4);wb(b,0x7f4f,0x15);finish();e(rw(b,0x7f80)==5,"pythagorean command");
w24(b,0x7f80,0x000123);w24(b,0x7f83,0x000010);wb(b,0x7f4f,0x25);finish();e(r24(b,0x7f80)==0x001230,"24-bit multiply command");
wb(b,0x7f4f,0x89);finish();e(b.read(0x7f80)==0x36&&b.read(0x7f81)==0x43&&b.read(0x7f82)==0x05,"immediate ROM signature");
w24(b,0x7f40,0x008000);ww(b,0x7f43,4);ww(b,0x7f45,0x0100);wb(b,0x7f47,0);finish();e(b.read(0x6100)==0x80&&b.read(0x6103)==0x83,"ROM to Cx4 RAM transfer");
e((b.read(0x807f5e)&0x40)==0,"status clears busy after completion");
wb(b,0x7f51,0x00); wb(b,0x7f40,0xa5); wb(b,0x7f60,0x5a); wb(b,0x7f4f,0x89);
e((b.read(0x7f5e)&0xc0)==0x40,"running HLE command sets RUNNING without claiming BUSY bus state");
e(b.read(0x00ffc0)==0xa5&&b.read(0x00ffe0)==0x5a,"running Cx4 overlays control and vector windows onto $ffc0-$ffff");
finish();
e(b.read(0x00ffc0)!=0xa5,"halted Cx4 releases $ffc0-$ffff overlay"); e((b.read(0x7f5e)&0x02)!=0,"completion sets Cx4 IRQ-pending status"); e(c.hardwareIrqPending(),"enabled completion asserts cartridge IRQ");
w24(b,0x7f40,0x008000);ww(b,0x7f43,1);ww(b,0x7f45,0x0100);wb(b,0x7f47,0);
e((b.read(0x7f5e)&0xc0)==0xc0,"Cx4 DMA sets RUNNING and BUSY");
e((b.read(0x00ffe0))==0x5a,"bus-busy Cx4 overlays $7f60 onto CPU vector $ffe0");
finish();
wb(b,0x7f5e,0x00); e((b.read(0x7f5e)&0x02)==0,"$7f5e clears pending-status bit"); e(c.hardwareIrqPending(),"$7f5e does not deassert already asserted IRQ line");
wb(b,0x7f51,0x01); e(!c.hardwareIrqPending(),"$7f51.0 acknowledges and disables IRQ"); wb(b,0x7f51,0x00);
wb(b,0x6123,0xa6); e(b.read(0x7123)==0xa6,"Cx4 $7000-$7bff mirrors 3 KiB RAM");
wb(b,0x6620,1); ww(b,0x6621,0); ww(b,0x6623,0); wb(b,0x6626,0);
ww(b,0x6220,10); ww(b,0x6222,20); wb(b,0x6224,0x01); wb(b,0x6225,0x30); wb(b,0x6226,0x00);
w24(b,0x6227,0x008100); wb(b,0x7f4d,0x00); wb(b,0x7f4f,0x00);finish();
e(b.read(0x6000)==12&&b.read(0x6001)==23,"OAM builder position");
e(b.read(0x6002)==0x34&&b.read(0x6003)==0x01,"OAM builder tile/attr");
e((b.read(0x6200)&0x02)!=0,"OAM builder large-sprite bit");
for(int i=0;i<0x80;i++) wb(b,uint16_t(0x6b00+i),0xf0);
for(int i=0;i<16;i++) wb(b,uint16_t(0x6a00+i),0xff);
for(int i=0;i<16;i++) wb(b,uint16_t(0x6a10+i),0xff);
wb(b,0x7f83,0); wb(b,0x7f4d,0x0c); wb(b,0x7f4f,0x00);finish();
e((rw(b,0x6000)&0xc0c0)==0xc0c0,"bitplane wave first mask");
ww(b,0x7f80,2); wb(b,0x7f83,0); wb(b,0x7f86,0); wb(b,0x7f89,0); wb(b,0x7f8c,0x90);
ww(b,0x6001,0);ww(b,0x6005,0);ww(b,0x6009,0x95);ww(b,0x6011,10);ww(b,0x6015,0);ww(b,0x6019,0x95);
ww(b,0x6b00,1);wb(b,0x6b02,0);wb(b,0x6b03,1);wb(b,0x7f4d,0x05);wb(b,0x7f4f,0x00);finish();
e(rw(b,0x6001)==0x80&&rw(b,0x6005)==0x50,"transform-lines first vertex displacement");
e(rw(b,0x6011)==0x8a&&rw(b,0x6015)==0x50,"transform-lines second vertex displacement");
e(rw(b,0x6600)==11&&rw(b,0x6602)==0x0100&&rw(b,0x6605)==0,"transform-lines step metadata");
for(int i=0;i<0x900;i++) wb(b,uint16_t(0x6000+i),0);
w24(b,0x7f80,0x008200);wb(b,0x7f82,0);wb(b,0x6295,1);
wb(b,0x7f86,0);wb(b,0x7f87,0);wb(b,0x7f88,0);wb(b,0x7f90,0xff);wb(b,0x7f4d,0x08);wb(b,0x7f4f,0x00);finish();
e((b.read(0x67e0)&0x80)!=0,"wireframe renderer first pixel");
for(int i=0;i<32;i++) wb(b,uint16_t(0x6600+i),0x11);
ww(b,0x7f80,0); ww(b,0x7f83,4); ww(b,0x7f86,4); wb(b,0x7f89,8); wb(b,0x7f8c,8);
ww(b,0x7f8f,0x1000); ww(b,0x7f92,0x1000); wb(b,0x7f4d,0x03); wb(b,0x7f4f,0x00);finish();
for(int y=0;y<8;y++){e(b.read(uint16_t(0x6000+y*2))==0xff,"scale/rotate plane0 row");e(b.read(uint16_t(0x6001+y*2))==0x00,"scale/rotate plane1 row");}
for(int i=0;i<32;i++) wb(b,uint16_t(0x6600+i),0x22);
ww(b,0x7f80,4); ww(b,0x7f83,4); ww(b,0x7f86,0x0100); wb(b,0x7f89,8); wb(b,0x7f8c,8); ww(b,0x7f8f,0x0100);
wb(b,0x7f4d,0x0b); wb(b,0x7f4f,0x00);finish();
for(int y=0;y<8;y++){e(b.read(uint16_t(0x6000+y*2))==0x00,"disintegrate plane0 row");e(b.read(uint16_t(0x6001+y*2))==0xff,"disintegrate plane1 row");}
for(int i=0;i<0x900;i++) wb(b,uint16_t(0x6000+i),0);
w24(b,0x7f80,0x008200);wb(b,0x7f82,0);wb(b,0x6295,1);
wb(b,0x7f86,0);wb(b,0x7f87,0);wb(b,0x7f88,0);wb(b,0x7f90,0xff);wb(b,0x7f4d,0x08);wb(b,0x7f4f,0x01);finish();
e((b.read(0x67e0)&0x80)!=0,"top-level $01 wireframe renderer");
wb(b,0x7f4d,0x0e);wb(b,0x7f4f,0x3c);finish();e(b.read(0x7f80)==0x0f,"Cx4 mode-0e diagnostic command");
ww(b,0x7f80,0);ww(b,0x7f83,0x8000);wb(b,0x7f4f,0x13);finish();
e(r24(b,0x7f86)==0x7fff00&&r24(b,0x7f89)==0,"Cx4 $13 unsigned radius");
ww(b,0x7f80,4);ww(b,0x7f83,0x0100);wb(b,0x7f4f,0x13);finish();
e(r24(b,0x7f89)==3214,"Cx4 Q15 sine-table truncation");
ww(b,0x7f80,10);ww(b,0x7f83,20);ww(b,0x7f86,30);ww(b,0x7f89,20);ww(b,0x7f8c,0);ww(b,0x7f8f,0);ww(b,0x7f93,5);
wb(b,0x7f4d,0x02);wb(b,0x7f4f,0x22);finish();
e(b.read(0x6800)==20&&b.read(0x6900)==25,"Cx4 trapezoid first scanline");
e(b.read(0x68e0)==20&&b.read(0x69e0)==25,"Cx4 trapezoid sustained zero-angle edges");
if(f){std::cerr<<f<<" CX4 regressions failed\n";return 1;}std::cout<<"CX4 accuracy regression passed\n";}

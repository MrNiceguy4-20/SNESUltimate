#include "bus.hpp"
#include "cartridge.hpp"
#include <cstdint>
#include <iostream>
#include <vector>
namespace{int f=0;void e(bool c,const char*m){if(!c){std::cerr<<"FAIL: "<<m<<'\n';++f;}}void w16(std::vector<uint8_t>&r,size_t o,uint16_t v){r[o]=v;r[o+1]=v>>8;}std::vector<uint8_t>rom(){std::vector<uint8_t>r(1024*1024);size_t h=0x7fc0;for(int i=0;i<21;i++)r[h+i]=' ';r[h+0x15]=0x30;r[h+0x16]=0x03;r[h+0x17]=0x0a;r[h+0x19]=1;w16(r,h+0x1c,0xedcb);w16(r,h+0x1e,0x1234);w16(r,h+0x3c,0x8000);return r;}void p(Bus&b,uint8_t v){b.write(0x006000,v);}void p16(Bus&b,uint16_t v){p(b,uint8_t(v));p(b,uint8_t(v>>8));}uint8_t g(Bus&b){return b.read(0x006000);}uint16_t g16(Bus&b){uint16_t l=g(b);return l|(uint16_t(g(b))<<8);}}
int main(){Cartridge c;c.setSRAMPersistence(false);e(c.loadData(rom(),"/tmp/dsp4.sfc"),"load");e(c.hardwareType()==CartridgeHardware::DSP4,"detect");e(c.hardwareImplemented(),"implemented");Bus b;b.attachCartridge(&c);
p16(b,0x0000);p16(b,300);p16(b,200);uint32_t product=g16(b);product|=uint32_t(g16(b))<<16;e(product==60000,"16-bit multiply");
p16(b,0x0011);p16(b,0);p16(b,0);p16(b,0);p16(b,0x4000);e(g16(b)==0x5000,"horizontal nibble mapping");
p16(b,0x0005);e(g(b)==0xff,"OAM clear no payload");p16(b,0x000b);p16(b,10);p16(b,20);p16(b,0x1234);e(g16(b)==1,"OAM draw flag");e(g(b)==10&&g(b)==20,"OAM packed XY bytes");e(g16(b)==0x1234,"OAM packed attribute word");
p16(b,0x000b);p16(b,10);p16(b,0x00f0);p16(b,0x9999);e(g16(b)==0,"OAM clipped sprite stop flag");
p16(b,0x0006);bool any=false;for(int i=0;i<32;i++)any|=g16(b)!=0;e(!any,"OAM attribute transfer after in-range small sprite");
p16(b,0x000a);p16(b,0);p16(b,0x3210);p16(b,0);e(g16(b)==0x0060,"OP0A high-middle nibble");e(g16(b)==0x0090,"OP0A high nibble");e(g16(b)==0x0000,"OP0A low nibble");e(g16(b)==0x0030,"OP0A low-middle nibble");
p16(b,0x0001);
p16(b,0); p16(b,0);
p16(b,4); p16(b,0);
p16(b,0); p16(b,0);
p16(b,0); p16(b,0);
p16(b,0); p16(b,0x1200);
p16(b,0);
p16(b,0); p16(b,0);
p16(b,0); p16(b,0);
p16(b,0); p16(b,0);
p16(b,0); p16(b,0);
p16(b,0); p16(b,0); p16(b,0);
e(g16(b)==0 && g16(b)==0 && g16(b)==0 && g16(b)==0,"OP01 projected header");
e(g16(b)==4,"OP01 segment count");
for(int i=0;i<4;i++){e(g16(b)==uint16_t(0x1200-i*4),"OP01 HDMA pointer");(void)g16(b);(void)g16(b);}
p16(b,0x8000);
p16(b,0x0000);p16(b,7);p16(b,9);uint32_t p2=g16(b);p2|=uint32_t(g16(b))<<16;e(p2==63,"OP01 termination restores command parser");
p16(b,0x0007);
p16(b,0);p16(b,0);
p16(b,0);p16(b,0);p16(b,0);p16(b,0);
p16(b,0);p16(b,0);
p16(b,0);p16(b,0x2000);p16(b,0);
p16(b,0x4000);
p16(b,0);p16(b,0);p16(b,0);p16(b,0);p16(b,0);
e(g16(b)==0 && g16(b)==0 && g16(b)==0,"OP07 initial projection packet");
p16(b,0x8000);
p16(b,0x0011);p16(b,0);p16(b,0);p16(b,0);p16(b,0x4000);e(g16(b)==0x5000,"OP07 termination restores command parser");
p16(b,0x000d);
p16(b,0);p16(b,4);
p16(b,4);p16(b,0);p16(b,0);p16(b,0);
p16(b,0);p16(b,0);
p16(b,0);p16(b,0x2400);p16(b,0);
p16(b,0);p16(b,0);p16(b,0);p16(b,0);
p16(b,0);p16(b,0);
p16(b,0);p16(b,0);p16(b,0);p16(b,0);
e(g16(b)==0 && g16(b)==0 && g16(b)==4 && g16(b)==0,"OP0D projected header");
e(g16(b)==4,"OP0D segment count");
for(int i=0;i<4;i++){e(g16(b)==uint16_t(0x2400-i*4),"OP0D HDMA pointer");(void)g16(b);(void)g16(b);}
p16(b,0x8000);
p16(b,0x0000);p16(b,5);p16(b,6);uint32_t p3=g16(b);p3|=uint32_t(g16(b))<<16;e(p3==30,"OP0D termination restores command parser");
p16(b,0x0010);p16(b,0);
p16(b,0);p16(b,4);
p16(b,4);p16(b,0);p16(b,0);p16(b,0);
p16(b,0);p16(b,0);
p16(b,0);p16(b,0x3000);p16(b,0);
p16(b,0);p16(b,0);p16(b,0);p16(b,0);p16(b,0);
p16(b,0);
e(g16(b)==0&&g16(b)==0&&g16(b)==4,"OP10 projected header");
for(int i=0;i<4;i++){p16(b,0x4000);p16(b,0x7fff);e(g16(b)==0x3def,"OP10 dynamic lighting");}
for(int i=0;i<4;i++){e(g16(b)==uint16_t(0x3000-i*4),"OP10 HDMA pointer");(void)g16(b);(void)g16(b);}
p16(b,0x8000);
p16(b,0x0000);p16(b,4);p16(b,8);uint32_t p4=g16(b);p4|=uint32_t(g16(b))<<16;e(p4==32,"OP10 termination restores command parser");
p16(b,0x0008);
for(int i=0;i<4;i++)p16(b,127);
for(int i=0;i<4;i++)p16(b,uint16_t(int16_t(-128)));
for(int i=0;i<8;i++)p16(b,0);
p16(b,10);p16(b,20);p16(b,30);p16(b,40);
p16(b,0x4000);p16(b,0x4000);p16(b,0x5000);p16(b,0x5000);
for(int i=0;i<4;i++)p16(b,8);for(int i=0;i<4;i++)p16(b,0);
for(int i=0;i<4;i++)p16(b,0);
p16(b,0x4000);p16(b,10);p16(b,8);p16(b,30);p16(b,8);
for(int i=0;i<4;i++)p16(b,0);
e(g(b)==0 && g(b)==10,"OP08 initial clipped window bytes");
p16(b,0x8000);e(g16(b)==0,"OP08 termination word");
p16(b,0x0000);p16(b,3);p16(b,7);uint32_t p5=g16(b);p5|=uint32_t(g16(b))<<16;e(p5==21,"OP08 termination restores parser");
p16(b,0x0009);p16(b,128);p16(b,112);p16(b,0);p16(b,0);p16(b,255);p16(b,0);p16(b,223);p16(b,100);p16(b,0x8000);
p16(b,0x0000);p16(b,2);p16(b,11);uint32_t p6=g16(b);p6|=uint32_t(g16(b))<<16;e(p6==22,"OP09 termination restores parser");
p16(b,0x0009);
p16(b,128);p16(b,112);p16(b,0);p16(b,0);p16(b,255);p16(b,0);p16(b,223);
p16(b,100);p16(b,0x4000);
p16(b,0);p16(b,250);p16(b,0);p16(b,16);p16(b,0x1234);
p16(b,0x2000);
p16(b,0);p16(b,0);
e(g16(b)==0,"OP09 phase-1 clip suppresses terrain sprite");
p16(b,0x8000);
p16(b,0x000f);p16(b,0);p16(b,0);p16(b,4);p16(b,4);p16(b,0);p16(b,0);p16(b,0);p16(b,0);p16(b,0);p16(b,0);p16(b,0x3800);p16(b,0);p16(b,0);p16(b,0);p16(b,0);p16(b,0);p16(b,0);p16(b,0);p16(b,0);p16(b,0);p16(b,0);p16(b,0);p16(b,0);
e(g16(b)==0&&g16(b)==0&&g16(b)==4&&g16(b)==0&&g16(b)==4,"OP0F projected header");
for(int i=0;i<4;i++){p16(b,0x4000);p16(b,0x7fff);e(g16(b)==0x3def,"OP0F dynamic lighting");}
for(int i=0;i<4;i++){e(g16(b)==uint16_t(0x3800-i*4),"OP0F HDMA pointer");(void)g16(b);(void)g16(b);}p16(b,0x8000);
p16(b,0x0000);p16(b,6);p16(b,7);uint32_t p7=g16(b);p7|=uint32_t(g16(b))<<16;e(p7==42,"OP0F termination restores parser");
e(b.read(0x206000)==0xff,"DSP4 owns 00-1f:6000-7fff mirror");
if(f){std::cerr<<f<<" DSP4 regressions failed\n";return 1;}std::cout<<"DSP4 accuracy regression passed\n";}

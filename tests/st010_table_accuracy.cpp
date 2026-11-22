#include "bus.hpp"
#include "cartridge.hpp"
#include <cstdint>
#include <iostream>
#include <vector>
namespace {
int failures=0;
void expect(bool ok,const char* msg){if(!ok){std::cerr<<"FAIL: "<<msg<<'\n';++failures;}}
void w16(std::vector<uint8_t>&r,size_t o,uint16_t v){r[o]=uint8_t(v);r[o+1]=uint8_t(v>>8);}
std::vector<uint8_t> rom(){std::vector<uint8_t>r(1024*1024,0);const size_t h=0x7fc0;for(int i=0;i<21;i++)r[h+i]=' ';r[h+0x15]=0x30;r[h+0x16]=0xf6;r[h+0x17]=8;r[h+0x18]=3;r[h+0x19]=1;w16(r,h+0x1c,0xedcb);w16(r,h+0x1e,0x1234);w16(r,h+0x3c,0x8000);return r;}
void run(Bus& b,uint8_t op){b.write(0x680020,op);b.write(0x680021,0x80);}
}
int main(){
 Cartridge c;c.setSRAMPersistence(false);expect(c.loadData(rom(),"/tmp/st010_table.sfc"),"ST010 load");Bus b;b.attachCartridge(&c);b.write(0x600000,0);
 b.write16(0x680000,1000);b.write16(0x680002,0);b.write16(0x680004,0x00ff);run(b,0x08);
 expect(b.read16(0x680010)==999,"OP08 cosine uses quantized table at theta 00ff");
 expect(b.read16(0x680012)==0,"OP08 ignores low eight theta bits");
 b.write16(0x680000,1000);b.write16(0x680002,0);b.write16(0x680004,0x0100);run(b,0x08);
 expect(int16_t(b.read16(0x680012))==-24,"OP08 next table step uses exact 0x0324 sine entry");
 b.write16(0x680000,0x0000);run(b,0x07);
 expect(b.read16(0x6800f0)==0x037f,"OP07 first A coefficient uses scale 0x0380");
 expect(b.read16(0x68024e)==0x002a,"OP07 final A coefficient uses scale 0x002b");
 expect(b.read16(0x680250)==0,"OP07 sine stream is zero at theta 0");
 if(failures){std::cerr<<failures<<" ST010 table regressions failed\n";return 1;}
 std::cout<<"phase39 ST010 table accuracy ok\n";
}

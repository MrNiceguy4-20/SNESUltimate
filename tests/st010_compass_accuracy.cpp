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
void run(Bus& b){b.write(0x680020,0x01);b.write(0x680021,0x80);}
}
int main(){
 Cartridge c;c.setSRAMPersistence(false);expect(c.loadData(rom(),"/tmp/st010_compass.sfc"),"ST010 load");Bus b;b.attachCartridge(&c);b.write(0x600000,0);
 b.write16(0x680000,10);b.write16(0x680002,9);run(b);
 expect(b.read16(0x680010)==0xa000,"OP01 preserves exact lattice entry [10][9]=0x20");
 b.write16(0x680000,1000);b.write16(0x680002,900);run(b);
 expect(b.read16(0x680000)==31&&b.read16(0x680002)==28,"OP01 normalizes coordinates into 5-bit lattice domain");
 expect(b.read16(0x680010)==0xa200,"OP01 normalized vector uses exact [31][28] lattice value");
 b.write16(0x680000,0);b.write16(0x680002,uint16_t(int16_t(-9)));run(b);
 expect(b.read16(0x680004)==0x4000,"OP01 negative-Y axis applies quadrant post-fix");
 b.write16(0x680000,uint16_t(int16_t(-7)));b.write16(0x680002,9);run(b);
 expect(b.read16(0x680000)==9&&b.read16(0x680002)==7,"OP01 quadrant rotation precedes lattice lookup");
 expect(b.read16(0x680004)==0xc000,"OP01 exposes negative-X quadrant code");
 if(failures){std::cerr<<failures<<" ST010 compass regressions failed\n";return 1;}
 std::cout<<"phase40 ST010 compass accuracy ok\n";
}

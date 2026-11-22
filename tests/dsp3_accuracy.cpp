#include "bus.hpp"
#include "cartridge.hpp"
#include <cstdint>
#include <iostream>
#include <vector>
namespace{int f=0;void e(bool c,const char*m){if(!c){std::cerr<<"FAIL: "<<m<<'\n';++f;}}void w16(std::vector<uint8_t>&r,size_t o,uint16_t v){r[o]=v;r[o+1]=v>>8;}std::vector<uint8_t>rom(){std::vector<uint8_t>r(1024*1024);size_t h=0x7fc0;for(int i=0;i<21;i++)r[h+i]=' ';r[h+0x15]=0x30;r[h+0x16]=0x05;r[h+0x17]=0x0a;r[h+0x19]=1;r[h+0x1a]=0xb2;w16(r,h+0x1c,0xedcb);w16(r,h+0x1e,0x1234);w16(r,h+0x3c,0x8000);return r;}void p(Bus&b,uint8_t v){b.write(0x208000,v);}void p16(Bus&b,uint16_t v){p(b,uint8_t(v));p(b,uint8_t(v>>8));}uint8_t g(Bus&b){return b.read(0x208000);}uint16_t g16(Bus&b){uint16_t l=g(b);return l|(uint16_t(g(b))<<8);}}
int main(){Cartridge c;c.setSRAMPersistence(false);e(c.loadData(rom(),"/tmp/dsp3.sfc"),"load");e(c.hardwareType()==CartridgeHardware::DSP3,"detect");e(c.hardwareImplemented(),"implemented");Bus b;b.attachCartridge(&c);
p(b,0x06);p16(b,0x1020);e(g(b)==0x80,"window command has no result");
p(b,0x03);p16(b,0x0305);e(g16(b)==0x65,"coordinate to linear offset");
p(b,0x07);p16(b,0x0002);p16(b,0x0305);uint16_t xy=g16(b),ofs=g16(b);e((xy&0xff)==6,"hex neighbor x");e(ofs<0x200,"hex neighbor offset");
p(b,0x0c);p16(b,0);e(g16(b)==0,"hardware probe");
p(b,0x10);p16(b,0x1234);e(g(b)==0x80,"OP10 absorbs data without output");
p16(b,0x5678);e(g(b)==0x80,"OP10 remains in streaming data mode");
p16(b,0xffff);e(g(b)==0x80,"OP10 terminator resets interface");
p(b,0x03);p16(b,0x0101);e(g16(b)==0x21,"OP10 termination restores parser");
p(b,0x1c);p16(b,0x1234);p16(b,0x5678);e(g16(b)==0&&g16(b)==0,"OP1C two-stage firmware handshake");
p(b,0x3e);p16(b,0x0305);e(g(b)==0x80,"OP3E origin packet has no immediate result");
p(b,0x1e);p16(b,0x0101);
std::vector<uint16_t> ring;for(int i=0;i<6;i++){uint16_t cxy=g16(b);ring.push_back(cxy);p16(b,0);p16(b,1);}e(g16(b)==0xffff,"OP1E terrain/cost exchange terminator");
const uint16_t expectedRing[6]={0x0205,0x0306,0x0406,0x0405,0x0404,0x0304};
bool ringOrder=true;for(int i=0;i<6;i++)ringOrder&=ring[i]==expectedRing[i];e(ringOrder,"OP1E firmware hex-ring traversal order");
p16(b,0x0101);bool weights=true;for(int i=0;i<6;i++){uint16_t cxy=g16(b),wt=g16(b);weights&=(cxy==ring[i]&&wt==1);}e(weights,"OP1E weighted radius output");e(g16(b)==0xffff,"OP1E weight exchange terminator");
p(b,0x18);p16(b,1);p16(b,0x0000);p16(b,0x0000);p16(b,0x0000);p16(b,0x0000);bool z=true;for(int i=0;i<4;i++)z&=g16(b)==0;e(z,"bitmap transpose zero block");
p(b,0x03);p16(b,0x0305);e(b.read(0x20c000)==0x80,"DSP3 status register reports ready");e(g16(b)==0x65,"DSP3 status poll preserves data output");
p(b,0x38);p16(b,1);p16(b,1);p16(b,0x0840);p16(b,0x0000);e(g16(b)==0x0042,"OP38 Huffman literal decode");
p(b,0x03);p16(b,0x0101);e(g16(b)==0x21,"OP38 completion restores parser");
e(b.read(0x206000)!=0x80,"DSP3 does not own 6000 area");
if(f){std::cerr<<f<<" DSP3 regressions failed\n";return 1;}std::cout<<"DSP3 accuracy regression passed\n";}

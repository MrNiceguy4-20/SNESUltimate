#include "hg51b.hpp"
#include <cstdint>
#include <iostream>
#include <vector>
static int fail=0;
#define CHECK(x,m) do{if(!(x)){std::cerr<<"FAIL: "<<m<<"\n";++fail;}}while(0)
int main(){
 HG51B h;
 h.setProgramWord(0,0,0x642a);
 h.setProgramWord(0,1,0xe060);
 h.setProgramWord(0,2,0xfc00);
 CHECK(h.stepInstruction()==1,"LD base cycle");
 CHECK(h.state().a==0x2a,"LD immediate");
 h.stepInstruction();CHECK(h.state().gpr[0]==0x2a,"ST GPR");
 h.stepInstruction();CHECK(h.state().halt,"HALT");
 h.reset();
 h.state().a=0x7fffff;h.executeOpcode(0x8401);
 CHECK(h.state().a==0x800000,"24-bit ADD result");
 CHECK(h.state().n&&!h.state().z&&!h.state().c&&h.state().v,"ADD flags");
 h.state().a=0;h.executeOpcode(0x9401);
 CHECK(h.state().a==0xffffff,"24-bit SUB wraps");
 CHECK(h.state().n&&!h.state().z&&!h.state().c,"SUB flags");
 std::vector<uint8_t> dr(3072);dr[3*7+0]=0x56;dr[3*7+1]=0x34;dr[3*7+2]=0x12;h.setDataRom(dr);
 h.executeOpcode(uint16_t(0x7400|7));CHECK(h.state().rom==0x123456,"RDROM packed 24-bit word");
 h.executeOpcode(0x6008);CHECK(h.state().a==0x123456,"ROM register readable");
 h.state().a=0xc12;h.state().ram=0x00ab00;h.executeOpcode(0xe900);
 CHECK(h.dataRam()[0x812]==0xab,"WRRAM address alias");
 h.state().ram=0;h.executeOpcode(0x6900);CHECK(h.state().ram==0x00ab00,"RDRAM byte lane");
 CHECK(h.readRegister(0x56)==0x800000,"constant register");
 h.writeRegister(0x60,0xabcdef);CHECK(h.readRegister(0x70)==0xabcdef,"GPR mirror");
 h.reset();h.state().z=true;h.state().pc=0x10;
 CHECK(h.executeOpcode(0x0c44)==3&&h.state().pc==0x44,"conditional JMP");
 h.state().pc=0x20;CHECK(h.executeOpcode(0x2840)==3&&h.state().pc==0x40,"JSR taken");
 CHECK(h.executeOpcode(0x3c00)==3&&h.state().pc==0x20,"RTS restores PC");
 h.state().a=0x123456;h.writeRegister(0x60,25);h.executeOpcode(0xd860);
 CHECK(h.state().a==0x123456,"SHL >24 treated as zero");
 if(fail){std::cerr<<fail<<" HG51B checks failed\n";return 1;}
 std::cout<<"HG51B instruction-core accuracy checks passed\n";return 0;
}

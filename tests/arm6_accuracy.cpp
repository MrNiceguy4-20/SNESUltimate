#include "arm6.hpp"
#include <array>
#include <cstdint>
#include <iostream>
#include <vector>
namespace {
int failures=0;
void expect(bool c,const char* m){if(!c){std::cerr<<"FAIL: "<<m<<'\n';++failures;}}
void put32(std::array<uint8_t,512>& m,uint32_t a,uint32_t v){for(unsigned i=0;i<4;i++)m[(a+i)&511]=uint8_t(v>>(i*8));}
}
int main(){
 std::array<uint8_t,512> mem{};
 ARM6Core cpu;
 cpu.attach(
  [&](uint32_t a){a&=~3u;return uint32_t(mem[a&511])|(uint32_t(mem[(a+1)&511])<<8)|(uint32_t(mem[(a+2)&511])<<16)|(uint32_t(mem[(a+3)&511])<<24);},
  [&](uint32_t a){return mem[a&511];},
  [&](uint32_t a,uint32_t v){a&=~3u;put32(mem,a,v);},
  [&](uint32_t a,uint8_t v){mem[a&511]=v;});
 cpu.reset(); put32(mem,0,0xE5910000u);
 put32(mem,0x100,0x44332211u); cpu.registers()[1]=0x101; cpu.step();
 expect(cpu.registers()[0]==0x11443322u,"unaligned LDR rotates aligned bus word by address low bits");
 cpu.reset(); put32(mem,0,0xE5810000u);
 cpu.registers()[0]=0xA1B2C3D4u; cpu.registers()[1]=0x103; cpu.step();
 expect(mem[0x100]==0xD4&&mem[0x101]==0xC3&&mem[0x102]==0xB2&&mem[0x103]==0xA1,
        "unaligned STR writes the aligned word address");
 cpu.reset(); cpu.registers()[13]=0x11111111u; cpu.registers()[8]=0x88880000u;
 cpu.setCpsr((cpu.cpsr()&~0x1fu)|0x12u);
 cpu.registers()[13]=0x22222222u;
 cpu.setCpsr((cpu.cpsr()&~0x1fu)|0x13u);
 expect(cpu.registers()[13]==0x11111111u,"SVC stack pointer survives IRQ bank switch");
 cpu.registers()[13]=0x33333333u;
 cpu.setCpsr((cpu.cpsr()&~0x1fu)|0x12u);
 expect(cpu.registers()[13]==0x22222222u,"IRQ stack pointer has its own register bank");
 cpu.setCpsr((cpu.cpsr()&~0x1fu)|0x11u);
 cpu.registers()[8]=0xF1F1F1F1u;
 cpu.setCpsr((cpu.cpsr()&~0x1fu)|0x13u);
 expect(cpu.registers()[8]==0x88880000u,"leaving FIQ restores shared r8-r12 bank");
 expect(cpu.registers()[13]==0x33333333u,"SVC stack pointer survives a round trip through FIQ");
 cpu.setCpsr((cpu.cpsr()&~0x1fu)|0x11u);
 expect(cpu.registers()[8]==0xF1F1F1F1u,"FIQ r8-r12 bank persists across mode changes");
 cpu.reset(); put32(mem,0,0xEC000000u); cpu.step();
 expect((cpu.cpsr()&0x1f)==0x1bu,"undefined instruction enters ARM undefined mode");
 expect(cpu.pc()==0x04u,"undefined instruction vectors to 0x04");
 expect(cpu.registers()[14]==0x04u,"undefined exception stores architectural return address in UND LR");
 cpu.reset(); put32(mem,0,0xE0000291u);
 cpu.registers()[1]=7; cpu.registers()[2]=1;
 expect(cpu.step()==2,"MUL with a sign-extended two-bit multiplier takes 1S+1I");
 cpu.reset(); put32(mem,0,0xE0000291u); cpu.registers()[1]=7; cpu.registers()[2]=0x40000000u;
 expect(cpu.step()==17,"MUL worst-case multiplier takes 1S+16I");
 cpu.reset(); put32(mem,0,0xE1A00211u);
 cpu.registers()[1]=1; cpu.registers()[2]=3;
 expect(cpu.step()==2&&cpu.registers()[0]==8,"register-specified data-processing shift adds one internal cycle");
 cpu.reset(); put32(mem,0,0xE1A0F000u); cpu.registers()[0]=0x40;
 expect(cpu.step()==3&&cpu.pc()==0x40,"data-processing write to PC pays pipeline-refill cycles");
 cpu.reset(); put32(mem,0,0xE5810000u); cpu.registers()[1]=0x100;
 expect(cpu.step()==2,"STR timing is two external cycles");
 cpu.reset(); put32(mem,0,0xE5910000u); cpu.registers()[1]=0x100;
 expect(cpu.step()==3,"LDR timing includes the load-use internal cycle");
 cpu.reset(); put32(mem,0,0xE1A0021Fu);
 cpu.registers()[2]=0; cpu.step();
 expect(cpu.registers()[0]==12,"register-shifted R15 operand observes PC+12");
 cpu.reset();
 cpu.registers()[13]=0xAAAA1111u;
 cpu.setCpsr((cpu.cpsr()&~0x1fu)|0x10u);
 cpu.registers()[13]=0xBBBB2222u;
 cpu.setCpsr((cpu.cpsr()&~0x1fu)|0x13u);
 cpu.registers()[0]=0x100; put32(mem,0,0xE8C02000u);
 cpu.step();
 expect(uint32_t(mem[0x100])|(uint32_t(mem[0x101])<<8)|(uint32_t(mem[0x102])<<16)|(uint32_t(mem[0x103])<<24)==0xBBBB2222u,
        "STM^ in privileged mode stores the User-bank SP");
 expect(cpu.registers()[13]==0xAAAA1111u,"STM^ leaves the active exception SP untouched");
 put32(mem,0x104,0xCCCC3333u); cpu.setPc(0); cpu.registers()[0]=0x104; put32(mem,0,0xE8D02000u);
 cpu.step();
 expect(cpu.registers()[13]==0xAAAA1111u,"LDM^ writes the User bank rather than active SVC SP");
 cpu.setCpsr((cpu.cpsr()&~0x1fu)|0x10u);
 expect(cpu.registers()[13]==0xCCCC3333u,"LDM^ updates the User-bank SP");
 cpu.reset(); put32(mem,0,0xE0B10002u);
 cpu.registers()[1]=0x7fffffffu; cpu.registers()[2]=0x7fffffffu; cpu.setCpsr(cpu.cpsr()|0x20000000u); cpu.step();
 expect(cpu.registers()[0]==0xffffffffu && (cpu.cpsr()&0x10000000u),"ADCS carry-in preserves correct signed-overflow detection");
 cpu.reset(); put32(mem,0,0xE0D10002u);
 cpu.registers()[1]=0x80000000u; cpu.registers()[2]=0x7fffffffu; cpu.setCpsr(cpu.cpsr()&~0x20000000u); cpu.step();
 expect(cpu.registers()[0]==0u && (cpu.cpsr()&0x10000000u),"SBCS borrow-in preserves correct signed-overflow detection");
 cpu.reset(); put32(mem,0,0xE8A10006u);
 cpu.registers()[1]=0x100; cpu.registers()[2]=0x22222222u; cpu.step();
 expect(uint32_t(mem[0x100])|(uint32_t(mem[0x101])<<8)|(uint32_t(mem[0x102])<<16)|(uint32_t(mem[0x103])<<24)==0x100u,
        "STM base first in list stores the pre-writeback base");
 expect(cpu.registers()[1]==0x108u,"STM writeback advances base by the full transfer size");
 cpu.reset(); put32(mem,0,0xE8A20006u);
 cpu.registers()[1]=0x11111111u; cpu.registers()[2]=0x100; cpu.step();
 expect(uint32_t(mem[0x104])|(uint32_t(mem[0x105])<<8)|(uint32_t(mem[0x106])<<16)|(uint32_t(mem[0x107])<<24)==0x108u,
        "STM base later in list stores the post-writeback base");
 cpu.reset(); put32(mem,0,0xE8B10006u);
 put32(mem,0x100,0xCAFEBABEu); put32(mem,0x104,0x12345678u); cpu.registers()[1]=0x100; cpu.step();
 expect(cpu.registers()[1]==0xCAFEBABEu,"LDM base in list leaves loaded value rather than writeback value");
 expect(cpu.registers()[2]==0x12345678u,"LDM still loads subsequent registers normally");
 cpu.reset(); put32(mem,0,0xE8AF0001u);
 cpu.registers()[0]=0x55AA55AAu; cpu.registers()[15]=0xDEADBEEFu; cpu.step();
 expect(cpu.registers()[15]==0xDEADBEEFu,"STM writeback never mutates hidden R15 backing storage");
 cpu.setCpsr((cpu.cpsr()&~0x1fu)|0x1bu);
 cpu.registers()[13]=0x13572468u; auto state=cpu.saveState();
 cpu.setCpsr((cpu.cpsr()&~0x1fu)|0x13u); cpu.registers()[13]=0;
 expect(cpu.loadState(state),"ARMv3 banked register state reloads");
 expect((cpu.cpsr()&0x1f)==0x1bu&&cpu.registers()[13]==0x13572468u,"ARMv3 save state restores active exception bank");
 if(failures){std::cerr<<failures<<" ARM6 accuracy regression(s) failed\n";return 1;}
 std::cout<<"ARM6 accuracy regression passed\n";return 0;
}

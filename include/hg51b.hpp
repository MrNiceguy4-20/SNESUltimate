#pragma once
#include <array>
#include <cstdint>
#include <vector>
class HG51B {
public:
 struct State {
  uint32_t a=0, mdr=0, mar=0, rom=0, ram=0;
  uint64_t mul=0;
  uint16_t dpr=0, p=0, pb=0;
  uint8_t pc=0;
  std::array<uint32_t,16> gpr{};
  std::array<uint16_t,8> stack{};
  bool n=false,z=false,c=false,v=false,irq=false,halt=false;
  bool busEnable=false,busReading=false,busWriting=false;
  uint8_t busPending=0,waitRom=3,waitRam=3;
  uint32_t busAddress=0;
 };
 HG51B();
 void reset();
 void setDataRom(const std::vector<uint8_t>& bytes);
 void setProgramWord(uint8_t page,uint8_t pc,uint16_t opcode);
 uint16_t programWord(uint8_t page,uint8_t pc) const;
 const std::array<std::array<uint16_t,256>,2>& programCache() const{return program_;}
 std::array<std::array<uint16_t,256>,2>& programCache(){return program_;}
 void setProgramPage(uint8_t page){page_=page&1;}
 uint8_t programPage() const{return page_;}
 State& state(){return s_;}
 const State& state() const{return s_;}
 std::array<uint8_t,0x0c00>& dataRam(){return data_ram_;}
 const std::array<uint8_t,0x0c00>& dataRam() const{return data_ram_;}
 uint32_t stepInstruction();
 uint32_t executeOpcode(uint16_t opcode);
 uint32_t readRegister(uint8_t reg);
 void writeRegister(uint8_t reg,uint32_t value);
private:
 static uint32_t u24(uint64_t v){return uint32_t(v)&0x00ffffffu;}
 static int32_t s24(uint32_t v){v&=0xffffffu;return (v&0x800000u)?int32_t(v|0xff000000u):int32_t(v);}
 static unsigned shiftAmount(unsigned code){static constexpr unsigned t[4]={0,1,8,16};return t[code&3];}
 void setNZ(uint32_t x);
 uint32_t add(uint32_t x,uint32_t y);
 uint32_t sub(uint32_t x,uint32_t y);
 uint32_t logic(uint32_t x);
 uint32_t shl(uint32_t x,unsigned n);
 uint32_t shr(uint32_t x,unsigned n);
 uint32_t asr(uint32_t x,unsigned n);
 uint32_t ror(uint32_t x,unsigned n);
 void advance();
 void push();
 void pull();
 uint16_t ramAddress(uint32_t x) const;
 uint32_t operandReg(uint8_t reg,unsigned shift);
 uint32_t operandImm(uint8_t imm,unsigned shift) const;
 State s_{};
 uint8_t page_=0;
 std::array<std::array<uint16_t,256>,2> program_{};
 std::array<uint32_t,1024> data_rom_{};
 std::array<uint8_t,0x0c00> data_ram_{};
};

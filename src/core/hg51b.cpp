#include "hg51b.hpp"
#include <algorithm>
HG51B::HG51B(){reset();}
void HG51B::reset(){s_={};page_=0;program_={};data_ram_.fill(0);}
void HG51B::setDataRom(const std::vector<uint8_t>& bytes){
 data_rom_.fill(0);
 for(size_t i=0;i<1024 && i*3+2<bytes.size();++i)
  data_rom_[i]=uint32_t(bytes[i*3])|(uint32_t(bytes[i*3+1])<<8)|(uint32_t(bytes[i*3+2])<<16);
}
void HG51B::setProgramWord(uint8_t page,uint8_t pc,uint16_t op){program_[page&1][pc]=op;}
uint16_t HG51B::programWord(uint8_t page,uint8_t pc)const{return program_[page&1][pc];}
void HG51B::setNZ(uint32_t x){x=u24(x);s_.n=(x&0x800000u)!=0;s_.z=x==0;}
uint32_t HG51B::add(uint32_t x,uint32_t y){
 const uint64_t z=uint64_t(u24(x))+u24(y);const uint32_t r=u24(z);
 setNZ(r);s_.c=z>0xffffffu;s_.v=((~(x^y)&(x^r)&0x800000u)!=0);return r;
}
uint32_t HG51B::sub(uint32_t x,uint32_t y){
 const uint32_t xx=u24(x),yy=u24(y),r=u24(uint64_t(xx)-yy);setNZ(r);s_.c=xx>=yy;
 s_.v=((~(xx^yy)&(xx^r)&0x800000u)!=0);return r;
}
uint32_t HG51B::logic(uint32_t x){x=u24(x);setNZ(x);return x;}
uint32_t HG51B::shl(uint32_t x,unsigned n){if(n>24)n=0;return logic(u24(uint64_t(u24(x))<<n));}
uint32_t HG51B::shr(uint32_t x,unsigned n){if(n>24)n=0;return logic(u24(x)>>n);}
uint32_t HG51B::asr(uint32_t x,unsigned n){if(n>24)n=0;return logic(uint32_t(s24(x)>>n));}
uint32_t HG51B::ror(uint32_t x,unsigned n){if(n>24)n=0;n%=24;const uint32_t v=u24(x);return logic(n?((v>>n)|(v<<(24-n))):v);}
void HG51B::advance(){
 if(++s_.pc==0){if(page_){s_.halt=true;}else{page_=1;s_.pb=s_.p;}}
}
void HG51B::push(){for(size_t i=7;i>0;--i)s_.stack[i]=s_.stack[i-1];s_.stack[0]=uint16_t((s_.pb<<8)|s_.pc);}
void HG51B::pull(){const uint16_t v=s_.stack[0];for(size_t i=0;i<7;++i)s_.stack[i]=s_.stack[i+1];s_.stack[7]=0;s_.pb=uint16_t(v>>8);s_.pc=uint8_t(v);}
uint16_t HG51B::ramAddress(uint32_t x)const{uint16_t a=uint16_t(x&0xfff);if(a>=0xc00)a-=0x400;return a;}
uint32_t HG51B::operandReg(uint8_t reg,unsigned shift){return u24(uint64_t(readRegister(reg))<<shift);}
uint32_t HG51B::operandImm(uint8_t imm,unsigned shift)const{return u24(uint64_t(imm)<<shift);}
uint32_t HG51B::readRegister(uint8_t reg){
 reg&=0x7f;
 switch(reg){
  case 0x01:return uint32_t((s_.mul>>24)&0xffffffu);case 0x02:return uint32_t(s_.mul&0xffffffu);
  case 0x03:return s_.mdr;case 0x08:return s_.rom;case 0x0c:return s_.ram;case 0x13:return s_.mar;
  case 0x1c:return s_.dpr;case 0x20:return s_.pc;case 0x28:return s_.p;
  case 0x2e:s_.busEnable=true;s_.busReading=true;s_.busWriting=false;s_.busPending=s_.waitRom&7;s_.busAddress=s_.mar;return 0;
  case 0x2f:s_.busEnable=true;s_.busReading=true;s_.busWriting=false;s_.busPending=s_.waitRam&7;s_.busAddress=s_.mar;return 0;
  case 0x50:return 0;case 0x51:return 0xffffff;case 0x52:return 0x00ff00;case 0x53:return 0xff0000;
  case 0x54:return 0x00ffff;case 0x55:return 0xffff00;case 0x56:return 0x800000;case 0x57:return 0x7fffff;
  case 0x58:return 0x008000;case 0x59:return 0x007fff;case 0x5a:return 0xff7fff;case 0x5b:return 0xffff7f;
  case 0x5c:return 0x010000;case 0x5d:return 0xfeffff;case 0x5e:return 0x000100;case 0x5f:return 0x00feff;
  default:if(reg>=0x60)return s_.gpr[reg&0x0f];return 0;
 }
}
void HG51B::writeRegister(uint8_t reg,uint32_t value){
 reg&=0x7f;value=u24(value);
 switch(reg){case 0x01:s_.mul=(s_.mul&0xffffffull)|(uint64_t(value)<<24);break;case 0x02:s_.mul=(s_.mul&0xffffff000000ull)|value;break;case 0x03:s_.mdr=value;break;case 0x08:s_.rom=value;break;case 0x0c:s_.ram=value;break;case 0x13:s_.mar=value;break;
  case 0x1c:s_.dpr=uint16_t(value&0xfff);break;case 0x20:s_.pc=uint8_t(value);break;case 0x28:s_.p=uint16_t(value&0x7fff);break;
  case 0x2e:s_.busEnable=true;s_.busReading=false;s_.busWriting=true;s_.busPending=s_.waitRom&7;s_.busAddress=s_.mar;break;
  case 0x2f:s_.busEnable=true;s_.busReading=false;s_.busWriting=true;s_.busPending=s_.waitRam&7;s_.busAddress=s_.mar;break;
  default:if(reg>=0x60)s_.gpr[reg&0x0f]=value;break;}
}
uint32_t HG51B::stepInstruction(){if(s_.halt)return 0;const uint16_t op=program_[page_][s_.pc];advance();return executeOpcode(op);}
uint32_t HG51B::executeOpcode(uint16_t op){
 uint32_t cycles=1;const uint8_t top=uint8_t(op>>8),lo=uint8_t(op),reg=uint8_t(op&0x7f),sc=unsigned((op>>8)&3),sh=shiftAmount(sc);
 auto jump=[&](bool take){if(take){if(op&0x0200)s_.pb=s_.p;s_.pc=lo;cycles+=2;}};
 if((op&0xfc00)==0x0800){jump(true);return cycles;}
 if((op&0xfc00)==0x0c00){jump(s_.z);return cycles;}
 if((op&0xfc00)==0x1000){jump(s_.c);return cycles;}
 if((op&0xfc00)==0x1400){jump(s_.n);return cycles;}
 if((op&0xfc00)==0x1800){jump(s_.v);return cycles;}
 if((op&0xfc00)==0x1c00){if(s_.busEnable)cycles+=s_.busPending;return cycles;}
 if((op&0xfc00)==0x2000)return cycles;
 if((op&0xff00)==0x2400){if(s_.v==bool(op&1)){advance();++cycles;}return cycles;}
 if((op&0xff00)==0x2500){if(s_.c==bool(op&1)){advance();++cycles;}return cycles;}
 if((op&0xff00)==0x2600){if(s_.z==bool(op&1)){advance();++cycles;}return cycles;}
 if((op&0xff00)==0x2700){if(s_.n==bool(op&1)){advance();++cycles;}return cycles;}
 if((op&0xfc00)>=0x2800&&(op&0xfc00)<=0x3800){bool take=true;switch(op&0xfc00){case 0x2c00:take=s_.z;break;case 0x3000:take=s_.c;break;case 0x3400:take=s_.n;break;case 0x3800:take=s_.v;break;default:break;}if(take){push();if(op&0x0200)s_.pb=s_.p;s_.pc=lo;cycles+=2;}return cycles;}
 if((op&0xfc00)==0x3c00){pull();cycles+=2;return cycles;}
 if((op&0xfc00)==0x4000){s_.mar=u24(s_.mar+1);return cycles;}
 if((op&0xfc00)==0x4800){sub(readRegister(reg),u24(uint64_t(s_.a)<<sh));return cycles;}
 if((op&0xfc00)==0x4c00){sub(lo,u24(uint64_t(s_.a)<<sh));return cycles;}
 if((op&0xfc00)==0x5000){sub(u24(uint64_t(s_.a)<<sh),readRegister(reg));return cycles;}
 if((op&0xfc00)==0x5400){sub(u24(uint64_t(s_.a)<<sh),lo);return cycles;}
 if((op&0xff00)==0x5900){s_.a=logic(uint32_t(int32_t(int8_t(s_.a))));return cycles;}
 if((op&0xff00)==0x5a00){s_.a=logic(uint32_t(int32_t(int16_t(s_.a))));return cycles;}
 if((op&0xff00)==0x6000){s_.a=readRegister(reg);return cycles;}
 if((op&0xff00)==0x6100){s_.mdr=readRegister(reg);return cycles;}
 if((op&0xff00)==0x6200){s_.mar=readRegister(reg);return cycles;}
 if((op&0xff00)==0x6300){s_.p=uint16_t(s_.gpr[op&0xf]&0x7fff);return cycles;}
 if(top==0x64){s_.a=lo;return cycles;}if(top==0x65){s_.mdr=lo;return cycles;}if(top==0x66){s_.mar=lo;return cycles;}if(top==0x67){s_.p=lo;return cycles;}
 if(top>=0x68&&top<=0x6a){const unsigned b=top-0x68;const uint16_t a=ramAddress(s_.a);s_.ram=(s_.ram&~(0xffu<<(b*8)))|(uint32_t(data_ram_[a])<<(b*8));return cycles;}
 if(top>=0x6c&&top<=0x6e){const unsigned b=top-0x6c;const uint16_t a=ramAddress(uint32_t(s_.dpr)+lo);s_.ram=(s_.ram&~(0xffu<<(b*8)))|(uint32_t(data_ram_[a])<<(b*8));return cycles;}
 if((op&0xfc00)==0x7000){s_.rom=data_rom_[s_.a&0x3ff];return cycles;}if((op&0xfc00)==0x7400){s_.rom=data_rom_[op&0x3ff];return cycles;}
 if(top==0x7c){s_.p=uint16_t((s_.p&0x7f00)|lo);return cycles;}if((op&0xff00)==0x7d00){s_.p=uint16_t((s_.p&0xff)|((op&0x7f)<<8));return cycles;}
 auto alu=[&](uint16_t baseReg,uint16_t baseImm,auto fn){if((op&0xfc00)==baseReg){s_.a=fn(u24(uint64_t(s_.a)<<sh),readRegister(reg));return true;}if((op&0xfc00)==baseImm){s_.a=fn(u24(uint64_t(s_.a)<<sh),lo);return true;}return false;};
 if(alu(0x8000,0x8400,[&](uint32_t x,uint32_t y){return add(x,y);}))return cycles;
 if(alu(0x8800,0x8c00,[&](uint32_t x,uint32_t y){return sub(y,x);}))return cycles;
 if(alu(0x9000,0x9400,[&](uint32_t x,uint32_t y){return sub(x,y);}))return cycles;
 if((op&0xfc00)==0x9800){s_.mul=uint64_t(int64_t(s24(s_.a))*int64_t(s24(readRegister(reg))))&0xffffffffffffull;return cycles;}
 if((op&0xfc00)==0x9c00){s_.mul=uint64_t(int64_t(s24(s_.a))*int64_t(lo))&0xffffffffffffull;return cycles;}
 if(alu(0xa000,0xa400,[&](uint32_t x,uint32_t y){return logic(~x^y);}))return cycles;
 if(alu(0xa800,0xac00,[&](uint32_t x,uint32_t y){return logic(x^y);}))return cycles;
 if(alu(0xb000,0xb400,[&](uint32_t x,uint32_t y){return logic(x&y);}))return cycles;
 if(alu(0xb800,0xbc00,[&](uint32_t x,uint32_t y){return logic(x|y);}))return cycles;
 if((op&0xfc00)==0xc000){s_.a=shr(s_.a,readRegister(reg)&31);return cycles;}if((op&0xfc00)==0xc400){s_.a=shr(s_.a,op&31);return cycles;}
 if((op&0xfc00)==0xc800){s_.a=asr(s_.a,readRegister(reg)&31);return cycles;}if((op&0xfc00)==0xcc00){s_.a=asr(s_.a,op&31);return cycles;}
 if((op&0xfc00)==0xd000){s_.a=ror(s_.a,readRegister(reg)&31);return cycles;}if((op&0xfc00)==0xd400){s_.a=ror(s_.a,op&31);return cycles;}
 if((op&0xfc00)==0xd800){s_.a=shl(s_.a,readRegister(reg)&31);return cycles;}if((op&0xfc00)==0xdc00){s_.a=shl(s_.a,op&31);return cycles;}
 if((op&0xff00)==0xe000){writeRegister(reg,s_.a);return cycles;}if((op&0xff00)==0xe100){writeRegister(reg,s_.mdr);return cycles;}
 if(top>=0xe8&&top<=0xea){const unsigned b=top-0xe8;data_ram_[ramAddress(s_.a)]=uint8_t(s_.ram>>(b*8));return cycles;}
 if(top>=0xec&&top<=0xee){const unsigned b=top-0xec;data_ram_[ramAddress(uint32_t(s_.dpr)+lo)]=uint8_t(s_.ram>>(b*8));return cycles;}
 if((op&0xfc00)==0xf000){std::swap(s_.a,s_.gpr[op&15]);s_.a=u24(s_.a);s_.gpr[op&15]=u24(s_.gpr[op&15]);return cycles;}
 if((op&0xfc00)==0xf800){s_.a=0;s_.p=0;s_.ram=0;s_.dpr=0;return cycles;}
 if((op&0xfc00)==0xfc00){s_.halt=true;return cycles;}
 return cycles;
}

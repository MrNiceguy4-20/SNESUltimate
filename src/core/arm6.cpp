#include "arm6.hpp"
#include "state_io.hpp"
#include <cstdint>
#include <limits>
namespace {
static uint32_t ror32(uint32_t v, unsigned n) { n &= 31; return n ? ((v >> n) | (v << (32 - n))) : v; }
static unsigned popcount16(uint16_t v){ unsigned n=0; while(v){v=uint16_t(v&(v-1));++n;} return n; }
static bool addOverflow(uint32_t a,uint32_t b,uint32_t r){return ((~(a^b)&(a^r))>>31)!=0;}
static bool subOverflow(uint32_t a,uint32_t b,uint32_t r){return (((a^b)&(a^r))>>31)!=0;}
static bool adcOverflow(uint32_t a,uint32_t b,bool carry){
 const int64_t sum=int64_t(int32_t(a))+int64_t(int32_t(b))+(carry?1:0);
 return sum>std::numeric_limits<int32_t>::max()||sum<std::numeric_limits<int32_t>::min();
}
static bool sbcOverflow(uint32_t a,uint32_t b,bool carry){
 const int64_t diff=int64_t(int32_t(a))-int64_t(int32_t(b))-(carry?0:1);
 return diff>std::numeric_limits<int32_t>::max()||diff<std::numeric_limits<int32_t>::min();
}
static unsigned multiplyIterations(uint32_t value){
 const int64_t signedValue=static_cast<int32_t>(value);
 for(unsigned m=1;m<16;++m){
   const unsigned bits=2u*m;
   const int64_t lo=-(int64_t(1)<<(bits-1));
   const int64_t hi=(int64_t(1)<<(bits-1))-1;
   if(signedValue>=lo&&signedValue<=hi)return m;
 }
 return 16;
}
}
void ARM6Core::attach(Read32 r32, Read8 r8, Write32 w32, Write8 w8){read32_=std::move(r32);read8_=std::move(r8);write32_=std::move(w32);write8_=std::move(w8);}
void ARM6Core::reset(){
 r_.fill(0); user_r8_12_.fill(0); fiq_r8_14_.fill(0);
 user_r13_=user_r14_=0; fiq_spsr_=0;
 irq_r13_=irq_r14_=irq_spsr_=0; svc_r13_=svc_r14_=svc_spsr_=0;
 abt_r13_=abt_r14_=abt_spsr_=0; und_r13_=und_r14_=und_spsr_=0;
 pc_=0;cpsr_=0xD3;current_instruction_=0;
}
uint32_t ARM6Core::reg(unsigned n)const{return n==15?pc_+8:r_[n&15];}
void ARM6Core::setNZ(uint32_t v){cpsr_=(cpsr_&~0xC0000000u)|(v&0x80000000u)|(v==0?0x40000000u:0);}
void ARM6Core::setNZCV(uint32_t v,bool c,bool ov){setNZ(v);cpsr_=(cpsr_&~0x30000000u)|(c?0x20000000u:0)|(ov?0x10000000u:0);}
bool ARM6Core::condition(uint32_t op)const{const bool n=cpsr_>>31,z=(cpsr_>>30)&1,c=(cpsr_>>29)&1,v=(cpsr_>>28)&1;switch(op>>28){case 0:return z;case 1:return !z;case 2:return c;case 3:return !c;case 4:return n;case 5:return !n;case 6:return v;case 7:return !v;case 8:return c&&!z;case 9:return !c||z;case 10:return n==v;case 11:return n!=v;case 12:return !z&&(n==v);case 13:return z||(n!=v);case 14:return true;default:return false;}}
uint32_t ARM6Core::shift(uint32_t v,unsigned type,unsigned amount,bool byReg,bool& carry)const{
 const bool oldC=(cpsr_>>29)&1;
 if(byReg) amount&=0xff;
 if(amount==0){if(byReg){carry=oldC;return v;}switch(type){case 0:carry=oldC;return v;case 1:carry=(v>>31)&1;return 0;case 2:carry=(v>>31)&1;return (v&0x80000000u)?0xffffffffu:0;default:carry=v&1;return (oldC?0x80000000u:0)|(v>>1);}}
 switch(type){case 0:if(amount<32){carry=(v>>(32-amount))&1;return v<<amount;}carry=amount==32?(v&1):0;return 0;
 case 1:if(amount<32){carry=(v>>(amount-1))&1;return v>>amount;}carry=amount==32?((v>>31)&1):0;return 0;
 case 2:if(amount<32){carry=(v>>(amount-1))&1;return uint32_t(int32_t(v)>>amount);}carry=(v>>31)&1;return (v&0x80000000u)?0xffffffffu:0;
 default:amount&=31;if(amount==0){carry=(v>>31)&1;return v;}carry=(v>>(amount-1))&1;return ror32(v,amount);}
}
uint32_t ARM6Core::operand2(uint32_t op,bool& carry)const{
 if(op&(1u<<25)){unsigned rot=((op>>8)&15)*2;uint32_t imm=op&255;uint32_t out=ror32(imm,rot);carry=rot?((out>>31)&1):((cpsr_>>29)&1);return out;}
 const unsigned rm=op&15,type=(op>>5)&3;bool byReg=op&(1u<<4);unsigned amount=byReg?(reg((op>>8)&15)&0xff):((op>>7)&31);const uint32_t source=(byReg&&rm==15)?pc_+12:reg(rm);return shift(source,type,amount,byReg,carry);
}
bool ARM6Core::modeHasSpsr(uint8_t mode) const { return mode==0x11||mode==0x12||mode==0x13||mode==0x17||mode==0x1b; }
bool ARM6Core::privileged() const { const uint8_t m=uint8_t(cpsr_&0x1f); return m!=0x10; }
uint32_t& ARM6Core::activeSpsr(){
 switch(cpsr_&0x1f){case 0x11:return fiq_spsr_;case 0x12:return irq_spsr_;case 0x17:return abt_spsr_;case 0x1b:return und_spsr_;default:return svc_spsr_;}
}
const uint32_t& ARM6Core::activeSpsr()const{return const_cast<ARM6Core*>(this)->activeSpsr();}
void ARM6Core::switchMode(uint8_t nm){
 const uint8_t om=uint8_t(cpsr_&0x1f); nm&=0x1f; if(om==nm)return;
 const bool oldFiq=om==0x11,newFiq=nm==0x11;
 if(oldFiq){
   for(unsigned i=0;i<7;++i)fiq_r8_14_[i]=r_[8+i];
 }else{
   for(unsigned i=0;i<5;++i)user_r8_12_[i]=r_[8+i];
   switch(om){case 0x12:irq_r13_=r_[13];irq_r14_=r_[14];break;case 0x13:svc_r13_=r_[13];svc_r14_=r_[14];break;case 0x17:abt_r13_=r_[13];abt_r14_=r_[14];break;case 0x1b:und_r13_=r_[13];und_r14_=r_[14];break;default:user_r13_=r_[13];user_r14_=r_[14];break;}
 }
 if(newFiq){
   for(unsigned i=0;i<7;++i)r_[8+i]=fiq_r8_14_[i];
 }else{
   for(unsigned i=0;i<5;++i)r_[8+i]=user_r8_12_[i];
   switch(nm){case 0x12:r_[13]=irq_r13_;r_[14]=irq_r14_;break;case 0x13:r_[13]=svc_r13_;r_[14]=svc_r14_;break;case 0x17:r_[13]=abt_r13_;r_[14]=abt_r14_;break;case 0x1b:r_[13]=und_r13_;r_[14]=und_r14_;break;default:r_[13]=user_r13_;r_[14]=user_r14_;break;}
 }
 cpsr_=(cpsr_&~0x1fu)|nm;
}
void ARM6Core::setCpsr(uint32_t v){ const uint8_t nm=uint8_t(v&0x1f); switchMode(nm); cpsr_=v; }
uint32_t ARM6Core::loadWord(uint32_t a)const{const uint32_t w=read32_(a&~3u);return ror32(w,(a&3u)*8u);}
void ARM6Core::storeWord(uint32_t a,uint32_t v)const{write32_(a&~3u,v);}
uint32_t ARM6Core::userReg(unsigned n)const{
 n&=15;
 if(n<8)return r_[n];
 if(n<13){
  if((cpsr_&0x1f)==0x11)return user_r8_12_[n-8];
  return r_[n];
 }
 if(n==13){const uint8_t m=uint8_t(cpsr_&0x1f);return (m==0x10||m==0x1f)?r_[13]:user_r13_;}
 if(n==14){const uint8_t m=uint8_t(cpsr_&0x1f);return (m==0x10||m==0x1f)?r_[14]:user_r14_;}
 return pc_+12;
}
void ARM6Core::setUserReg(unsigned n,uint32_t v){
 n&=15;
 if(n<8){r_[n]=v;return;}
 if(n<13){if((cpsr_&0x1f)==0x11)user_r8_12_[n-8]=v;else r_[n]=v;return;}
 if(n==13){const uint8_t m=uint8_t(cpsr_&0x1f);if(m==0x10||m==0x1f)r_[13]=v;else user_r13_=v;return;}
 if(n==14){const uint8_t m=uint8_t(cpsr_&0x1f);if(m==0x10||m==0x1f)r_[14]=v;else user_r14_=v;}
}
void ARM6Core::exception(uint32_t vector,uint32_t returnPc,uint8_t mode){const uint32_t old=cpsr_;switchMode(mode);activeSpsr()=old;r_[14]=returnPc;cpsr_=(cpsr_&0xF0000000u)|0x80u|(old&0x40u)|(mode==0x11?0x40u:0u)|mode;pc_=vector;}
unsigned ARM6Core::step(){
 if(!read32_) return 1;
 current_instruction_=read32_(pc_);
 const uint32_t op=current_instruction_;
 if(!condition(op)){pc_+=4;return 1;}
 uint32_t next=pc_+4;
 if((op&0x0E000000u)==0x0A000000u){int32_t off=int32_t(op&0x00ffffffu);if(off&0x00800000)off|=~0x00ffffff;off<<=2;if(op&(1u<<24))r_[14]=pc_+4;next=uint32_t(int32_t(pc_+8)+off);pc_=next&~3u;return 3;}
 if((op&0x0F000000u)==0x0F000000u){exception(0x08,pc_+4,0x13);return 3;}
 if((op&0x0FC000F0u)==0x00000090u){unsigned rd=(op>>16)&15,rn=(op>>12)&15,rs=(op>>8)&15,rm=op&15;const uint32_t multiplier=reg(rs);uint32_t result=reg(rm)*multiplier;if(op&(1u<<21))result+=reg(rn);if(rd!=15)r_[rd]=result;if(op&(1u<<20))setNZ(result);pc_=next;return 1u+multiplyIterations(multiplier);}
 if((op&0x0FB00FF0u)==0x01000090u){unsigned rn=(op>>16)&15,rd=(op>>12)&15,rm=op&15;uint32_t a=reg(rn);if(op&(1u<<22)){uint8_t old=read8_(a);write8_(a,uint8_t(reg(rm)));if(rd!=15)r_[rd]=old;}else{uint32_t old=loadWord(a);storeWord(a,reg(rm));if(rd!=15)r_[rd]=old;}pc_=next;return 4;}
 if((op&0x0FBF0FFFu)==0x010F0000u){unsigned rd=(op>>12)&15;uint32_t v=(op&(1u<<22))?(modeHasSpsr(uint8_t(cpsr_&0x1f))?activeSpsr():cpsr_):cpsr_;if(rd==15)next=v&~3u;else r_[rd]=v;pc_=next;return 1;}
 if((op & 0x0DB0F000u) == 0x0120F000u){bool sc=false;uint32_t val=operand2(op,sc);uint32_t mask=0;if(op&(1u<<16))mask|=0x000000ffu;if(op&(1u<<19))mask|=0xff000000u;const bool toSpsr=op&(1u<<22);if(toSpsr){if(modeHasSpsr(uint8_t(cpsr_&0x1f))){uint32_t& psr=activeSpsr();psr=(psr&~mask)|(val&mask);}}else{if(!privileged())mask&=0xff000000u;uint32_t nv=(cpsr_&~mask)|(val&mask);setCpsr(nv);}pc_=next;return 1;}
 if((op&0x0E000000u)==0x08000000u){bool p=op&(1u<<24),u=op&(1u<<23),sbit=op&(1u<<22),w=op&(1u<<21),load=op&(1u<<20);unsigned rn=(op>>16)&15;uint16_t list=op&0xffff;unsigned count=popcount16(list);uint32_t base=reg(rn);uint32_t addr;
   if(list==0){list=0x8000;count=16;}
   const bool pcInList=(list&0x8000u)!=0;
   const bool userBank=sbit&&privileged()&&(!load||!pcInList);
   const uint32_t writeback=u?base+4u*count:base-4u*count;
   const bool writebackEnabled=w&&rn!=15;
   if(load&&writebackEnabled) r_[rn]=writeback;
   unsigned firstReg=16; for(unsigned i=0;i<16;++i)if(list&(1u<<i)){firstReg=i;break;}
   if(u) addr=base+(p?4:0); else addr=base-(p?4u*count:4u*(count-1));
   for(unsigned i=0;i<16;i++)if(list&(1u<<i)){
     if(load){uint32_t v=loadWord(addr);if(i==15)next=v&~3u;else if(userBank)setUserReg(i,v);else r_[i]=v;}
     else {
       uint32_t v=i==15?pc_+12:(userBank?userReg(i):reg(i));
       if(writebackEnabled&&i==rn&&rn!=firstReg) v=writeback;
       storeWord(addr,v);
     }
     addr+=4;
   }
   if(!load&&writebackEnabled) r_[rn]=writeback;
   if(load&&pcInList&&sbit&&modeHasSpsr(uint8_t(cpsr_&0x1f)))setCpsr(activeSpsr());
   pc_=next;{const unsigned transfers=popcount16(list);return load ? (transfers+2u+(pcInList?2u:0u)) : (transfers+1u);}}
 if((op&0x0C000000u)==0x04000000u){bool i=op&(1u<<25),p=op&(1u<<24),u=op&(1u<<23),byte=op&(1u<<22),w=op&(1u<<21),load=op&(1u<<20);unsigned rn=(op>>16)&15,rd=(op>>12)&15;uint32_t off;if(i){bool c=false;off=shift(reg(op&15),(op>>5)&3,(op>>7)&31,false,c);}else off=op&0xfff;uint32_t base=reg(rn),eff=p?(u?base+off:base-off):base;
   if(load){uint32_t v=byte?read8_(eff):loadWord(eff);if(!p||w)r_[rn]=u?base+off:base-off;if(rd==15)next=v&~3u;else r_[rd]=v;}else{uint32_t v=rd==15?pc_+12:reg(rd);if(byte)write8_(eff,uint8_t(v));else storeWord(eff,v);if(!p||w)r_[rn]=u?base+off:base-off;}pc_=next;return load ? (3u+(rd==15?2u:0u)) : 2u;}
 if((op&0x0C000000u)==0){const bool registerShift=(op&(1u<<25))==0&&(op&(1u<<4));bool shCarry=false;uint32_t rhs=operand2(op,shCarry),lhs=reg((op>>16)&15)+((registerShift&&((op>>16)&15)==15)?4u:0u),res=0;unsigned code=(op>>21)&15,rd=(op>>12)&15;bool s=op&(1u<<20),write=true,c=false,v=false;uint64_t wide;
   switch(code){case 0:res=lhs&rhs;break;case 1:res=lhs^rhs;break;case 2:res=lhs-rhs;c=lhs>=rhs;v=subOverflow(lhs,rhs,res);break;case 3:res=rhs-lhs;c=rhs>=lhs;v=subOverflow(rhs,lhs,res);break;case 4:wide=uint64_t(lhs)+rhs;res=uint32_t(wide);c=wide>>32;v=addOverflow(lhs,rhs,res);break;case 5:{const bool cin=((cpsr_>>29)&1)!=0;wide=uint64_t(lhs)+rhs+(cin?1u:0u);res=uint32_t(wide);c=wide>>32;v=adcOverflow(lhs,rhs,cin);break;}case 6:{const bool cin=((cpsr_>>29)&1)!=0;uint64_t sub=uint64_t(rhs)+(cin?0:1);res=lhs-uint32_t(sub);c=uint64_t(lhs)>=sub;v=sbcOverflow(lhs,rhs,cin);break;}case 7:{const bool cin=((cpsr_>>29)&1)!=0;uint64_t sub=uint64_t(lhs)+(cin?0:1);res=rhs-uint32_t(sub);c=uint64_t(rhs)>=sub;v=sbcOverflow(rhs,lhs,cin);break;}case 8:res=lhs&rhs;write=false;break;case 9:res=lhs^rhs;write=false;break;case 10:res=lhs-rhs;c=lhs>=rhs;v=subOverflow(lhs,rhs,res);write=false;break;case 11:wide=uint64_t(lhs)+rhs;res=uint32_t(wide);c=wide>>32;v=addOverflow(lhs,rhs,res);write=false;break;case 12:res=lhs|rhs;break;case 13:res=rhs;break;case 14:res=lhs&~rhs;break;default:res=~rhs;break;}
   if(s){if(code<=1||code==8||code==9||code>=12){setNZ(res);cpsr_=(cpsr_&~0x20000000u)|(shCarry?0x20000000u:0);}else setNZCV(res,c,v);}
   if(write){if(rd==15){next=res&~3u;if(s&&modeHasSpsr(uint8_t(cpsr_&0x1f)))setCpsr(activeSpsr());}else r_[rd]=res;}pc_=next;return 1u+(registerShift?1u:0u)+((write&&rd==15)?2u:0u);}
 exception(0x04,pc_+4,0x1b);return 3;
}
std::vector<uint8_t> ARM6Core::saveState()const{stateio::Writer w;uint32_t v=2;w.pod(v);w.array(r_);w.pod(pc_);w.pod(cpsr_);w.pod(current_instruction_);w.array(user_r8_12_);w.pod(user_r13_);w.pod(user_r14_);w.array(fiq_r8_14_);w.pod(fiq_spsr_);w.pod(irq_r13_);w.pod(irq_r14_);w.pod(irq_spsr_);w.pod(svc_r13_);w.pod(svc_r14_);w.pod(svc_spsr_);w.pod(abt_r13_);w.pod(abt_r14_);w.pod(abt_spsr_);w.pod(und_r13_);w.pod(und_r14_);w.pod(und_spsr_);return w.take();}
bool ARM6Core::loadState(const std::vector<uint8_t>& b){stateio::Reader r(b);uint32_t v=0;if(!r.pod(v)||v!=2)return false;return r.array(r_)&&r.pod(pc_)&&r.pod(cpsr_)&&r.pod(current_instruction_)&&r.array(user_r8_12_)&&r.pod(user_r13_)&&r.pod(user_r14_)&&r.array(fiq_r8_14_)&&r.pod(fiq_spsr_)&&r.pod(irq_r13_)&&r.pod(irq_r14_)&&r.pod(irq_spsr_)&&r.pod(svc_r13_)&&r.pod(svc_r14_)&&r.pod(svc_spsr_)&&r.pod(abt_r13_)&&r.pod(abt_r14_)&&r.pod(abt_spsr_)&&r.pod(und_r13_)&&r.pod(und_r14_)&&r.pod(und_spsr_)&&r.done();}

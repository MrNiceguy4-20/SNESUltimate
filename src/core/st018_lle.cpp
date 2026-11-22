#include "seta.hpp"
#include "arm6.hpp"
#include "state_io.hpp"
#include <array>
#include <cstdint>
#include <utility>
#include <vector>
namespace {
class ST018FirmwareDevice final : public CartridgeDevice {
public:
 ST018FirmwareDevice(std::vector<uint8_t>& ram,bool& dirty,ST018Firmware fw,uint32_t masterHz)
  : saveRam_(ram),dirty_(dirty),program_(std::move(fw.program)),data_(std::move(fw.data)),masterHz_(masterHz){
   cpu_.attach([this](uint32_t a){return read32(a);},[this](uint32_t a){return read8(a);},[this](uint32_t a,uint32_t v){write32(a,v);},[this](uint32_t a,uint8_t v){write8(a,v);});reset();
 }
 bool implemented()const override{return true;}
 CartridgeSupportLevel supportLevel()const override{return CartridgeSupportLevel::FirmwareLLE;}
 bool needsClock()const override{return true;}
 uint32_t stateTag()const override{return 0x384C5453u;}
 void reset()override{
  if(saveRam_.size()<0x2000) saveRam_.resize(0x2000,0);
  programRam_.fill(0);
  cpu_.reset();
  cputoarmReady_=armtocpuReady_=signal_=false; resetLine_=false; bridgeReady_=false;
  timer_=timerLatch_=0; clockFrac_=0; resetDelay_=65536; instructionCyclesRemaining_=0;
 }
 void clock(uint64_t masterClocks)override{
  const uint64_t scaled=(masterClocks%masterHz_)*21477272ull+clockFrac_;
  uint64_t cycles=(masterClocks/masterHz_)*21477272ull+scaled/masterHz_;clockFrac_=scaled%masterHz_;
  while(cycles--){
   if(timer_&&--timer_==0){}
   if(resetLine_)continue;
   if(!bridgeReady_){if(resetDelay_){--resetDelay_;continue;}bridgeReady_=true;}
   if(instructionCyclesRemaining_ != 0){ --instructionCyclesRemaining_; continue; }
   const unsigned cost = cpu_.step();
   instructionCyclesRemaining_ = cost > 0 ? cost - 1 : 0;
  }
 }
 bool read(uint32_t address,uint8_t& v)override{
  const uint8_t bank=uint8_t(address>>16);uint16_t a=uint16_t(address);
  if(!((bank<=0x3f)||(bank>=0x80&&bank<=0xbf))||a<0x3800||a>0x38ff)return false;
  a=uint16_t(0x3800|(a&6));
  if(a==0x3800){v=armtocpuReady_?armtocpuData_:0;if(armtocpuReady_)armtocpuReady_=false;return true;}
  if(a==0x3802){signal_=false;v=0;return true;}
  if(a==0x3804){v=status();return true;}
  v=0;return true;
 }
 bool write(uint32_t address,uint8_t v)override{
  const uint8_t bank=uint8_t(address>>16);uint16_t a=uint16_t(address);
  if(!((bank<=0x3f)||(bank>=0x80&&bank<=0xbf))||a<0x3800||a>0x38ff)return false;
  a=uint16_t(0x3800|(a&6));
  if(a==0x3802){cputoarmReady_=true;cputoarmData_=v;return true;}
  if(a==0x3804){const bool newReset=(v&1)!=0;if(!resetLine_&&newReset){cpu_.reset();bridgeReady_=false;resetDelay_=65536;instructionCyclesRemaining_=0;cputoarmReady_=armtocpuReady_=signal_=false;timer_=timerLatch_=0;}resetLine_=newReset;return true;}
  return true;
 }
 std::vector<uint8_t> saveState()const override{
  stateio::Writer w;uint32_t v=3;w.pod(v);w.vector(saveRam_);w.array(programRam_);w.pod(cputoarmReady_);w.pod(cputoarmData_);w.pod(armtocpuReady_);w.pod(armtocpuData_);w.pod(signal_);w.pod(resetLine_);w.pod(bridgeReady_);w.pod(timer_);w.pod(timerLatch_);w.pod(clockFrac_);w.pod(resetDelay_);w.pod(instructionCyclesRemaining_);w.vector(cpu_.saveState());return w.take();
 }
 bool loadState(const std::vector<uint8_t>& b)override{
  stateio::Reader r(b);uint32_t v=0;std::vector<uint8_t> sr,cs;if(!r.pod(v)||v!=3||!r.vector(sr)||!r.array(programRam_)||!r.pod(cputoarmReady_)||!r.pod(cputoarmData_)||!r.pod(armtocpuReady_)||!r.pod(armtocpuData_)||!r.pod(signal_)||!r.pod(resetLine_)||!r.pod(bridgeReady_)||!r.pod(timer_)||!r.pod(timerLatch_)||!r.pod(clockFrac_)||!r.pod(resetDelay_)||!r.pod(instructionCyclesRemaining_)||!r.vector(cs)||!r.done()||sr.size()<0x2000)return false;saveRam_=std::move(sr);dirty_=true;return cpu_.loadState(cs);
 }
private:
 uint8_t status()const{return uint8_t((armtocpuReady_?1:0)|(signal_?4:0)|(cputoarmReady_?8:0)|(bridgeReady_?0x80:0));}
 uint8_t read8(uint32_t a){
  switch(a&0xe0000000u){
   case 0x00000000u:return program_[a&0x1ffff];
   case 0x60000000u:return uint8_t(0x40404001u>>((a&3)*8));
   case 0xa0000000u:return data_[a&0x7fff];
   case 0xe0000000u:return programRam_[a&0x3fff];
   default:break;
  }
  if((a&0xe0000000u)==0x40000000u){switch(a&0x3f){case 0x10:if(cputoarmReady_){cputoarmReady_=false;return cputoarmData_;}return 0;case 0x20:return status();default:return 0;}}
  if((a&0xe0000000u)==0x20000000u||(a&0xe0000000u)==0x80000000u||(a&0xe0000000u)==0xc0000000u)return uint8_t(cpu_.currentInstruction() >> ((a & 3) * 8));
  return 0;
 }
 uint32_t read32(uint32_t a){
  if((a&0xe0000000u)==0x60000000u)return 0x40404001u;
  uint32_t b=a&~3u;return uint32_t(read8(b))|(uint32_t(read8(b+1))<<8)|(uint32_t(read8(b+2))<<16)|(uint32_t(read8(b+3))<<24);
 }
 void write8(uint32_t a,uint8_t v){
  if((a&0xe0000000u)==0xe0000000u){programRam_[a&0x3fff]=v;return;}
  if((a&0xe0000000u)!=0x40000000u)return;
  switch(a&0x3f){case 0x00:armtocpuReady_=true;armtocpuData_=v;break;case 0x10:signal_=true;break;case 0x20:timerLatch_=(timerLatch_&0xffffff00u)|v;break;case 0x24:timerLatch_=(timerLatch_&0xffff00ffu)|(uint32_t(v)<<8);break;case 0x28:timerLatch_=(timerLatch_&0xff00ffffu)|(uint32_t(v)<<16);break;case 0x2c:timer_=timerLatch_;break;default:break;}
 }
 void write32(uint32_t a,uint32_t v){if((a&0xe0000000u)==0xe0000000u){uint32_t b=a&~3u;for(unsigned i=0;i<4;i++)programRam_[(b+i)&0x3fff]=uint8_t(v>>(8*i));return;}write8(a,uint8_t(v));}
 std::vector<uint8_t>& saveRam_;bool& dirty_;std::vector<uint8_t> program_,data_;std::array<uint8_t,0x4000> programRam_{};ARM6Core cpu_;
 uint32_t masterHz_=21477272;uint64_t clockFrac_=0;bool cputoarmReady_=false,armtocpuReady_=false,signal_=false,resetLine_=false,bridgeReady_=false;uint8_t cputoarmData_=0,armtocpuData_=0;uint32_t timer_=0,timerLatch_=0,resetDelay_=65536,instructionCyclesRemaining_=0;
};
}
std::unique_ptr<CartridgeDevice> createST018FirmwareDevice(std::vector<uint8_t>& r,bool& d,ST018Firmware f,uint32_t hz){if(!f.valid())return {};return std::make_unique<ST018FirmwareDevice>(r,d,std::move(f),hz);}

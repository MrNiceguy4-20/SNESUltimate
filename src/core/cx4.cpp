#include "cx4.hpp"
#include "state_io.hpp"
#include "hg51b.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <memory>
#include <sstream>
#include <vector>
namespace {
constexpr double PI=3.14159265358979323846;
static int16_t cx4SinQ15(uint16_t angle){
 const double v=std::sin(double(angle&0x1ff)*(2.0*PI/512.0))*32768.0;
 int32_t q=static_cast<int32_t>(v);
 if(q>32767) q=32767;
 if(q<-32768) q=-32768;
 return static_cast<int16_t>(q);
}
static int16_t cx4CosQ15(uint16_t angle){return cx4SinQ15(uint16_t(angle+128));}
class CX4Device final : public CartridgeDevice {
public:
 explicit CX4Device(const std::vector<uint8_t>& rom,uint32_t masterClockHz,std::vector<uint8_t> dataRom)
 :rom_(rom),data_rom_(std::move(dataRom)),master_clock_hz_(masterClockHz){
  lleEnabled_=data_rom_.size()==3072; if(lleEnabled_) hg_.setDataRom(data_rom_); reset();
 }
 void reset() override {
  ram_.fill(0); running_=false; dmaActive_=false; cacheActive_=false; dmaLocked_=false;
  irqPending_=false; irqAsserted_=false; irqEnabled_=true;
  suspended_=false; suspendCycles_=0; pendingCommand_=0; workCycles_=0; frac_=0;
  dmaSource_=dmaTarget_=0; dmaLength_=dmaOffset_=0; dmaByteCycles_=0;
  cachePage_=0; cacheBase_=0; cachePB_=0; cacheLocks_=0; cacheCycles_=0;
  waitRom_=3; waitRam_=3; romConfig_=1;
  cacheTags_.fill(0xffffu); initGarbagePattern(); cacheLoadPB_=0; cacheLoadBase_=0; cacheStartPending_=false; pendingStartPc_=0; pendingStartPB_=0; lleCyclesRemaining_=0;
  hg_.reset(); if(lleEnabled_) hg_.setDataRom(data_rom_); syncHostToCore();
 }
 bool implemented() const override { return true; }
 CartridgeSupportLevel supportLevel() const override { return lleEnabled_ ? CartridgeSupportLevel::FirmwareLLE : CartridgeSupportLevel::PartialHLE; }
 bool needsClock() const override { return true; }
 bool hostIrqPending() const override { return irqAsserted_; }
 uint32_t stateTag() const override{return 0x20345843u;}
 std::vector<uint8_t> saveState() const override{
  stateio::Writer w;uint32_t v=8;w.pod(v);w.array(ram_);w.array(cartRam_);w.array(garbagePattern_);w.pod(running_);w.pod(dmaActive_);w.pod(cacheActive_);w.pod(dmaLocked_);
  w.pod(irqPending_);w.pod(irqAsserted_);w.pod(irqEnabled_);w.pod(suspended_);w.pod(suspendCycles_);
  w.pod(pendingCommand_);w.pod(workCycles_);w.pod(frac_);w.pod(dmaSource_);w.pod(dmaTarget_);w.pod(dmaLength_);w.pod(dmaOffset_);w.pod(dmaByteCycles_);
  w.pod(cachePage_);w.pod(cacheBase_);w.pod(cachePB_);w.pod(cacheLocks_);w.pod(cacheCycles_);w.pod(waitRom_);w.pod(waitRam_);w.pod(romConfig_);
  w.pod(lleEnabled_);w.array(cacheTags_);w.pod(cacheLoadPB_);w.pod(cacheLoadBase_);w.pod(cacheStartPending_);w.pod(pendingStartPc_);w.pod(lleCyclesRemaining_);w.pod(pendingStartPB_);
  w.pod(hg_.state());w.array(hg_.programCache()[0]);w.array(hg_.programCache()[1]);w.array(hg_.dataRam());return w.take();}
 bool loadState(const std::vector<uint8_t>&b)override{
  stateio::Reader r(b);uint32_t v=0;if(!r.pod(v))return false;
  if(v==1){reset();return r.array(ram_)&&r.done();}
  if(v==2)return false;
  if(v==3){bool oldBusy=false;uint8_t oldKind=0;if(!r.array(ram_)||!r.pod(oldBusy)||!r.pod(irqPending_)||!r.pod(irqAsserted_)||!r.pod(irqEnabled_)||!r.pod(suspended_)||!r.pod(suspendCycles_)||!r.pod(oldKind)||!r.pod(pendingCommand_)||!r.pod(workCycles_)||!r.pod(frac_)||!r.done())return false;running_=oldBusy&&oldKind==2;dmaActive_=oldBusy&&oldKind==1;cacheActive_=false;dmaSource_=r24(0x1f40);dmaTarget_=r16(0x1f45);dmaLength_=r16(0x1f43);dmaOffset_=0;dmaByteCycles_=dmaActive_?uint32_t(2+waitRom_):0;if(lleEnabled_)syncHostToCore();return true;}
  if(v==4){dmaLocked_=false;const bool ok=r.array(ram_)&&r.pod(running_)&&r.pod(dmaActive_)&&r.pod(cacheActive_)&&r.pod(irqPending_)&&r.pod(irqAsserted_)&&r.pod(irqEnabled_)&&r.pod(suspended_)&&r.pod(suspendCycles_)&&r.pod(pendingCommand_)&&r.pod(workCycles_)&&r.pod(frac_)&&r.pod(dmaSource_)&&r.pod(dmaTarget_)&&r.pod(dmaLength_)&&r.pod(dmaOffset_)&&r.pod(dmaByteCycles_)&&r.pod(cachePage_)&&r.pod(cacheBase_)&&r.pod(cachePB_)&&r.pod(cacheLocks_)&&r.pod(cacheCycles_)&&r.pod(waitRom_)&&r.pod(waitRam_)&&r.pod(romConfig_)&&r.done();if(ok&&lleEnabled_)syncHostToCore();return ok;}
  if(v==5){const bool ok=r.array(ram_)&&r.pod(running_)&&r.pod(dmaActive_)&&r.pod(cacheActive_)&&r.pod(dmaLocked_)&&r.pod(irqPending_)&&r.pod(irqAsserted_)&&r.pod(irqEnabled_)&&r.pod(suspended_)&&r.pod(suspendCycles_)&&r.pod(pendingCommand_)&&r.pod(workCycles_)&&r.pod(frac_)&&r.pod(dmaSource_)&&r.pod(dmaTarget_)&&r.pod(dmaLength_)&&r.pod(dmaOffset_)&&r.pod(dmaByteCycles_)&&r.pod(cachePage_)&&r.pod(cacheBase_)&&r.pod(cachePB_)&&r.pod(cacheLocks_)&&r.pod(cacheCycles_)&&r.pod(waitRom_)&&r.pod(waitRam_)&&r.pod(romConfig_)&&r.done();if(ok&&lleEnabled_)syncHostToCore();return ok;}
  if(v!=6&&v!=7&&v!=8)return false;
  if(v==6){
   struct LegacyHG51BState {uint32_t a=0,mdr=0,mar=0,rom=0,ram=0;uint64_t mul=0;uint16_t dpr=0,p=0,pb=0;uint8_t pc=0;std::array<uint32_t,16> gpr{};std::array<uint16_t,8> stack{};bool n=false,z=false,c=false,v=false,irq=false,halt=false;} hs{};
   bool stateLLE=false;
   if(!r.array(ram_)||!r.pod(running_)||!r.pod(dmaActive_)||!r.pod(cacheActive_)||!r.pod(dmaLocked_)||!r.pod(irqPending_)||!r.pod(irqAsserted_)||!r.pod(irqEnabled_)||!r.pod(suspended_)||!r.pod(suspendCycles_)||!r.pod(pendingCommand_)||!r.pod(workCycles_)||!r.pod(frac_)||!r.pod(dmaSource_)||!r.pod(dmaTarget_)||!r.pod(dmaLength_)||!r.pod(dmaOffset_)||!r.pod(dmaByteCycles_)||!r.pod(cachePage_)||!r.pod(cacheBase_)||!r.pod(cachePB_)||!r.pod(cacheLocks_)||!r.pod(cacheCycles_)||!r.pod(waitRom_)||!r.pod(waitRam_)||!r.pod(romConfig_)||!r.pod(stateLLE)||!r.array(cacheTags_)||!r.pod(cacheLoadPB_)||!r.pod(cacheLoadBase_)||!r.pod(cacheStartPending_)||!r.pod(pendingStartPc_)||!r.pod(lleCyclesRemaining_)||!r.pod(hs)||!r.array(hg_.programCache()[0])||!r.array(hg_.programCache()[1])||!r.array(hg_.dataRam())||!r.done())return false;
   if(stateLLE!=lleEnabled_)return false;
   auto& h=hg_.state();h.a=hs.a;h.mdr=hs.mdr;h.mar=hs.mar;h.rom=hs.rom;h.ram=hs.ram;h.mul=hs.mul;h.dpr=hs.dpr;h.p=hs.p;h.pb=hs.pb;h.pc=hs.pc;h.gpr=hs.gpr;h.stack=hs.stack;h.n=hs.n;h.z=hs.z;h.c=hs.c;h.v=hs.v;h.irq=hs.irq;h.halt=hs.halt;h.waitRom=waitRom_;h.waitRam=waitRam_;pendingStartPB_=cachePB_;return true;
  }
  bool stateLLE=false;HG51B::State hs{};
  if(!r.array(ram_)||!r.array(cartRam_))return false;
  if(v>=8){if(!r.array(garbagePattern_))return false;}else initGarbagePattern();
  if(!r.pod(running_)||!r.pod(dmaActive_)||!r.pod(cacheActive_)||!r.pod(dmaLocked_)||!r.pod(irqPending_)||!r.pod(irqAsserted_)||!r.pod(irqEnabled_)||!r.pod(suspended_)||!r.pod(suspendCycles_)||!r.pod(pendingCommand_)||!r.pod(workCycles_)||!r.pod(frac_)||!r.pod(dmaSource_)||!r.pod(dmaTarget_)||!r.pod(dmaLength_)||!r.pod(dmaOffset_)||!r.pod(dmaByteCycles_)||!r.pod(cachePage_)||!r.pod(cacheBase_)||!r.pod(cachePB_)||!r.pod(cacheLocks_)||!r.pod(cacheCycles_)||!r.pod(waitRom_)||!r.pod(waitRam_)||!r.pod(romConfig_)||!r.pod(stateLLE)||!r.array(cacheTags_)||!r.pod(cacheLoadPB_)||!r.pod(cacheLoadBase_)||!r.pod(cacheStartPending_)||!r.pod(pendingStartPc_)||!r.pod(lleCyclesRemaining_)||!r.pod(pendingStartPB_)||!r.pod(hs)||!r.array(hg_.programCache()[0])||!r.array(hg_.programCache()[1])||!r.array(hg_.dataRam())||!r.done())return false;
  if(stateLLE!=lleEnabled_)return false;
  hg_.state()=hs;return true;
 }
 bool read(uint32_t a,uint8_t& v) override {
  if(!owns(a)) return false;
  const uint16_t x=static_cast<uint16_t>(a);
  if(isExecutionOverlay(a)){v=ram_[0x1f40u+(x-0xffc0u)];return true;}
  if(isStatusMirror(x)){
   v=status();
   return true;
  }
  if(x>=0x7000&&x<=0x7bff){const size_t o=x-0x7000;if(lleEnabled_&&running_)v=hg_.dataRam()[o];else v=ram_[o];return true;}
  if(isGarbageWindow(x)){v=garbagePattern_[x&31u];return true;}
  if(x==0x7f48){v=cachePage_;return true;}
  if(x==0x7f49){v=uint8_t(cacheBase_);return true;} if(x==0x7f4a){v=uint8_t(cacheBase_>>8);return true;} if(x==0x7f4b){v=uint8_t(cacheBase_>>16);return true;}
  if(x==0x7f4c){v=uint8_t(cacheLocks_&3);return true;} if(x==0x7f4d){v=uint8_t(cachePB_);return true;} if(x==0x7f4e){v=uint8_t(cachePB_>>8);return true;}
  if(x==0x7f50){v=uint8_t((waitRam_&7)|((waitRom_&7)<<4));return true;} if(x==0x7f51){v=uint8_t(irqEnabled_?0:1);return true;} if(x==0x7f52){v=romConfig_;return true;}
  if(isZeroWindow(x)){v=0;return true;}
  const uint16_t y=gprMirror(x); if(y!=x){const size_t o=y-0x7f80; if(lleEnabled_&&running_){const uint32_t g=hg_.state().gpr[o/3];v=uint8_t(g>>(8*(o%3)));}else v=ram_[y-0x6000];return true;}
  v=ram_[x-0x6000]; return true;
 }
 bool write(uint32_t a,uint8_t v) override {
  if(!owns(a))return false;
  const uint16_t x=static_cast<uint16_t>(a);
  if(isExecutionOverlay(a)){ram_[0x1f40u+(x-0xffc0u)]=v;return true;}
  if(x>=0x7000&&x<=0x7bff){const size_t o=x-0x7000;ram_[o]=v;if(lleEnabled_&&running_)hg_.dataRam()[o]=v;return true;}
  if(isGarbageWindow(x))return true;
  if(isZeroWindow(x))return true;
  const uint16_t y=gprMirror(x); if(y!=x){ram_[y-0x6000]=v;if(lleEnabled_&&running_){const size_t o=y-0x7f80;auto& g=hg_.state().gpr[o/3];const unsigned sh=8u*unsigned(o%3);g=(g&~(0xffu<<sh))|(uint32_t(v)<<sh);g&=0x00ffffffu;}return true;}
  ram_[x-0x6000]=v;
  if(x==0x7f40){dmaSource_=(dmaSource_&0xffff00u)|v;return true;} if(x==0x7f41){dmaSource_=(dmaSource_&0xff00ffu)|(uint32_t(v)<<8);return true;} if(x==0x7f42){dmaSource_=(dmaSource_&0x00ffffu)|(uint32_t(v)<<16);return true;}
  if(x==0x7f43){dmaLength_=uint16_t((dmaLength_&0xff00u)|v);return true;} if(x==0x7f44){dmaLength_=uint16_t((dmaLength_&0x00ffu)|(uint16_t(v)<<8));return true;}
  if(x==0x7f45){dmaTarget_=(dmaTarget_&0xffff00u)|v;return true;} if(x==0x7f46){dmaTarget_=(dmaTarget_&0xff00ffu)|(uint32_t(v)<<8);return true;}
  if(x==0x7f47){dmaTarget_=(dmaTarget_&0x00ffffu)|(uint32_t(v)<<16);if(!runningStatus())beginDMA();return true;}
  if(x==0x7f48){cachePage_=v&1;if(!runningStatus())beginCache();return true;}
  if(x==0x7f49){cacheBase_=(cacheBase_&0xffff00u)|v;return true;} if(x==0x7f4a){cacheBase_=(cacheBase_&0xff00ffu)|(uint32_t(v)<<8);return true;} if(x==0x7f4b){cacheBase_=(cacheBase_&0x00ffffu)|(uint32_t(v)<<16);return true;}
  if(x==0x7f4c){cacheLocks_=v&3;return true;} if(x==0x7f4d){cachePB_=uint16_t((cachePB_&0xff00u)|v);return true;} if(x==0x7f4e){cachePB_=uint16_t((cachePB_&0x00ffu)|(uint16_t(v)<<8));return true;}
  if(x==0x7f50){waitRam_=v&7;waitRom_=(v>>4)&7;return true;}
  if(x==0x7f51){if(v&1){irqPending_=false;irqAsserted_=false;irqEnabled_=false;}else irqEnabled_=true;return true;}
  if(x==0x7f52){romConfig_=v&1;return true;}
  if(x==0x7f53){dmaLocked_=false;running_=false;workCycles_=0;return true;}
  if(x==0x7f55){suspended_=true;suspendCycles_=0;return true;}
  if(x>=0x7f56&&x<=0x7f5c){suspended_=true;suspendCycles_=uint32_t(x-0x7f55)*32u;return true;}
  if(x==0x7f5d){suspended_=false;suspendCycles_=0;return true;}
  if(x==0x7f5e){irqPending_=false;return true;}
  if(x==0x7f4f){
   if(!runningStatus())beginCommand(v);
  }
  return true;
 }
 void clock(uint64_t masterClocks) override {
  const uint64_t scaled=(masterClocks%master_clock_hz_)*20000000ull+frac_;
  uint64_t cycles=(masterClocks/master_clock_hz_)*20000000ull+scaled/master_clock_hz_;frac_=scaled%master_clock_hz_;
  if(suspended_){if(suspendCycles_==0)return;if(cycles<suspendCycles_){suspendCycles_-=uint32_t(cycles);return;}cycles-=suspendCycles_;suspendCycles_=0;suspended_=false;}
  while(cycles){
   if(cacheActive_){const uint64_t take=std::min<uint64_t>(cycles,cacheCycles_);cacheCycles_-=uint32_t(take);cycles-=take;if(cacheCycles_==0){cacheActive_=false;completeCache();}continue;}
   if(dmaActive_){
    if(dmaLocked_){break;}
    if(dmaOffset_>=dmaLength_){dmaActive_=false;continue;}
    const uint32_t source=(dmaSource_+dmaOffset_)&0xffffffu,target=(dmaTarget_+dmaOffset_)&0xffffffu;
    const DmaRegion sourceRegion=dmaRegion(source),targetRegion=dmaRegion(target);
    if(sourceRegion==DmaRegion::Unmapped||targetRegion==DmaRegion::Unmapped||targetRegion==DmaRegion::ROM||
       (sourceRegion==DmaRegion::RAM&&targetRegion==DmaRegion::RAM)){dmaLocked_=true;dmaByteCycles_=0;break;}
    if(dmaByteCycles_==0)dmaByteCycles_=dmaCyclesPerByte(source,target);
    const uint64_t take=std::min<uint64_t>(cycles,dmaByteCycles_);dmaByteCycles_-=uint32_t(take);cycles-=take;
    if(dmaByteCycles_==0){dmaWrite(target,dmaRead(source));++dmaOffset_;if(dmaOffset_>=dmaLength_)dmaActive_=false;}
    continue;
   }
   if(running_&&lleEnabled_){
    if(lleCyclesRemaining_){const uint64_t take=std::min<uint64_t>(cycles,lleCyclesRemaining_);advanceLLEBus(uint32_t(take));lleCyclesRemaining_-=uint32_t(take);cycles-=take;if(lleCyclesRemaining_==0&&hg_.state().halt)finishLLE();continue;}
    hg_.state().waitRom=waitRom_;hg_.state().waitRam=waitRam_;advanceLLEBus(1); const uint8_t previousPhysical=hg_.programPage(); const uint32_t used=std::max<uint32_t>(1,hg_.stepInstruction());
    if(!hg_.state().halt && !resolveProgramPage(previousPhysical)){--cycles;continue;}
    if(used>1){lleCyclesRemaining_=used-1;} --cycles;
    if(hg_.state().halt&&lleCyclesRemaining_==0)finishLLE();
    continue;
   }
   if(running_&&workCycles_){const uint64_t take=std::min<uint64_t>(cycles,workCycles_);workCycles_-=uint32_t(take);cycles-=take;if(workCycles_==0){execute(pendingCommand_);running_=false;irqPending_=true;if(irqEnabled_)irqAsserted_=true;}continue;}
   break;
  }
 }
private:
 bool runningStatus() const{return running_||cacheActive_||dmaActive_;}
 bool busyStatus() const{return cacheActive_||dmaActive_||(lleEnabled_&&hg_.state().busEnable);}
 uint8_t status() const{return uint8_t((suspended_?0x01:0)|(irqPending_?0x02:0)|(runningStatus()?0x40:0)|(busyStatus()?0x80:0));}
 bool owns(uint32_t a) const {uint8_t b=static_cast<uint8_t>(a>>16);uint16_t x=static_cast<uint16_t>(a);const bool hostBank=(b<=0x3f||(b>=0x80&&b<=0xbf));return hostBank&&((x>=0x6000&&x<=0x7fff)||(runningStatus()&&x>=0xffc0));}
 bool isExecutionOverlay(uint32_t a) const {const uint8_t b=uint8_t(a>>16);return runningStatus()&&(b<=0x3f||(b>=0x80&&b<=0xbf))&&uint16_t(a)>=0xffc0;}
 static bool isGarbageWindow(uint16_t x){return (x>=0x6c00&&x<=0x6fff)||(x>=0x7c00&&x<=0x7f3f);}
 static bool isZeroWindow(uint16_t x){return (x>=0x7fb0&&x<=0x7fbf)||(x>=0x7ff0&&x<=0x7fff);}
 void initGarbagePattern(){
  uint32_t l=0x51b169u;for(auto& b:garbagePattern_){l^=l<<13;l^=l>>17;l^=l<<5;b=uint8_t(l);}
 }
 static bool isStatusMirror(uint16_t x){switch(x){case 0x7f53:case 0x7f54:case 0x7f55:case 0x7f56:case 0x7f57:case 0x7f59:case 0x7f5b:case 0x7f5c:case 0x7f5d:case 0x7f5e:case 0x7f5f:return true;default:return false;}}
 static uint16_t gprMirror(uint16_t x){if(x>=0x7fc0&&x<=0x7fef)return uint16_t(0x7f80+(x-0x7fc0));return x;}
 void beginCommand(uint8_t cmd){
  irqPending_=false;
  if(!lleEnabled_){pendingCommand_=cmd;workCycles_=1;running_=true;return;}
  pendingStartPc_=cmd; pendingStartPB_=cachePB_; cacheStartPending_=false;
  int page=findCachePage(cachePB_);
  if(page<0){
   const int preferred=(cacheLocks_&1)?1:0;
   const int alternate=preferred^1;
   if((cacheLocks_&(1u<<preferred)) && (cacheLocks_&(1u<<alternate))){running_=false;return;}
   cachePage_=uint8_t((cacheLocks_&(1u<<preferred))?alternate:preferred);
   cacheStartPending_=true; beginCacheFor(cachePB_); return;
  }
  startLLE(uint8_t(page),cmd,cachePB_);
 }
 void beginCache(){beginCacheFor(cachePB_);}
 void beginCacheFor(uint16_t logicalPB){
  cacheActive_=true; cacheCycles_=512u*uint32_t(1u+waitRom_);
  cacheLoadPB_=logicalPB; cacheLoadBase_=cacheBase_;
 }
 int findCachePage(uint16_t tag)const{for(int i=0;i<2;++i)if(cacheTags_[i]==tag)return i;return -1;}
 void completeCache(){
  const uint32_t base=(cacheLoadBase_ + uint32_t(cacheLoadPB_)*512u)&0xffffffu;
  for(unsigned i=0;i<256;++i){
   const uint8_t lo=romAt((base+i*2u)&0xffffffu), hi=romAt((base+i*2u+1u)&0xffffffu);
   hg_.setProgramWord(cachePage_,uint8_t(i),uint16_t(lo|(uint16_t(hi)<<8)));
  }
  cacheTags_[cachePage_]=cacheLoadPB_;
  if(cacheStartPending_){cacheStartPending_=false;startLLE(cachePage_,pendingStartPc_,pendingStartPB_);}
 }
 void startLLE(uint8_t physicalPage,uint8_t pc,uint16_t logicalPB){
  syncHostToCore(); hg_.setProgramPage(physicalPage); hg_.state().pb=logicalPB; hg_.state().pc=pc; hg_.state().halt=false;
  running_=true; workCycles_=0; lleCyclesRemaining_=0;
 }
 void advanceLLEBus(uint32_t clocks){
  auto& h=hg_.state();if(!h.busEnable)return;
  if(h.busPending>clocks){h.busPending=uint8_t(h.busPending-clocks);return;}
  h.busPending=0;const uint32_t a=h.busAddress&0xffffffu;
  if(h.busReading)h.mdr=dmaRead(a);
  if(h.busWriting)dmaWrite(a,uint8_t(h.mdr));
  h.busEnable=false;h.busReading=false;h.busWriting=false;
 }
 bool resolveProgramPage(uint8_t previousPhysical){
  const uint16_t logicalPB=hg_.state().pb;
  const uint8_t physical=hg_.programPage();
  if(cacheTags_[physical]==logicalPB)return true;
  const int resident=findCachePage(logicalPB);
  if(resident>=0){hg_.setProgramPage(uint8_t(resident));return true;}
  int target=(physical!=previousPhysical)?int(physical):(int(physical)^1);
  if(cacheLocks_&(1u<<target))target=int(physical)^1;
  if(cacheLocks_&(1u<<target)){hg_.state().halt=true;return true;}
  pendingStartPc_=hg_.state().pc;pendingStartPB_=logicalPB;cachePage_=uint8_t(target);
  cacheStartPending_=true;beginCacheFor(logicalPB);return false;
 }
 void finishLLE(){
  syncCoreToHost(); running_=false; irqPending_=true; if(irqEnabled_)irqAsserted_=true;
 }
 void syncHostToCore(){
  std::copy_n(ram_.begin(),0x0c00,hg_.dataRam().begin());
  for(unsigned r=0;r<16;++r){const size_t o=0x1f80u+r*3u;hg_.state().gpr[r]=uint32_t(ram_[o]|(uint32_t(ram_[o+1])<<8)|(uint32_t(ram_[o+2])<<16));}
 }
 void syncCoreToHost(){
  std::copy(hg_.dataRam().begin(),hg_.dataRam().end(),ram_.begin());
  for(unsigned r=0;r<16;++r){const size_t o=0x1f80u+r*3u;const uint32_t v=hg_.state().gpr[r];ram_[o]=uint8_t(v);ram_[o+1]=uint8_t(v>>8);ram_[o+2]=uint8_t(v>>16);}
 }
 void beginDMA(){dmaActive_=dmaLength_!=0;dmaOffset_=0;dmaByteCycles_=0;}
 enum class DmaRegion:uint8_t{ROM,RAM,DRAM,IO,Unmapped};
 bool isRomAddress(uint32_t address)const{return ((address&0x408000u)==0x008000u)||((address&0xc00000u)==0xc00000u);}
 bool isRamAddress(uint32_t address)const{return (address&0xf88000u)==0x700000u;}
 bool dramAddress(uint32_t address,uint16_t& out)const{
  if(address<0x0c00u){out=uint16_t(address);return true;}
  if((address&0x40e000u)!=0x006000u||(address&0x0c00u)==0x0c00u)return false;
  out=uint16_t(address&0x0fffu);
  return out<0x0c00u;
 }
 bool ioAddress(uint32_t address,uint16_t& out)const{if((address&0x40ec00u)!=0x006c00u)return false;out=uint16_t(0x7c00u|(address&0x03ffu));return true;}
 DmaRegion dmaRegion(uint32_t address)const{uint16_t off=0;if(isRomAddress(address))return DmaRegion::ROM;if(isRamAddress(address))return DmaRegion::RAM;if(dramAddress(address,off))return DmaRegion::DRAM;if(ioAddress(address,off))return DmaRegion::IO;return DmaRegion::Unmapped;}
 size_t ramLinear(uint32_t address)const{return size_t(((address>>16)&7u)*0x8000u+(address&0x7fffu))%cartRam_.size();}
 size_t romLinear(uint32_t address)const{uint32_t linear=((address&0x3f0000u)>>1)|(address&0x7fffu);return rom_.empty()?0:size_t(linear&0x1fffffu)%rom_.size();}
 uint8_t dmaRead(uint32_t address){
  address&=0xffffffu;if(isRomAddress(address))return rom_.empty()?0xff:rom_[romLinear(address)];if(isRamAddress(address))return cartRam_[ramLinear(address)];
  uint16_t off=0;if(dramAddress(address,off)){if(lleEnabled_&&running_&&off<0x0c00)return hg_.dataRam()[off];return ram_[off];}if(ioAddress(address,off)){uint8_t v=0;read(off,v);return v;}return 0x00;
 }
 void dmaWrite(uint32_t address,uint8_t value){
  address&=0xffffffu;if(isRomAddress(address))return;if(isRamAddress(address)){cartRam_[ramLinear(address)]=value;return;}uint16_t off=0;if(dramAddress(address,off)){ram_[off]=value;if(lleEnabled_&&running_&&off<0x0c00)hg_.dataRam()[off]=value;return;}if(ioAddress(address,off)){write(off,value);return;}
 }
 uint32_t dmaWait(uint32_t address)const{const DmaRegion region=dmaRegion(address);if(region==DmaRegion::ROM)return uint32_t(1u+waitRom_);if(region==DmaRegion::RAM)return uint32_t(1u+waitRam_);return 1u;}
 uint32_t dmaCyclesPerByte(uint32_t source,uint32_t target)const{return dmaWait(source)+dmaWait(target);}
 uint16_t r16(size_t o)const{return static_cast<uint16_t>(ram_[o]|(ram_[o+1]<<8));}
 int16_t s16(size_t o)const{return static_cast<int16_t>(r16(o));}
 uint32_t r24(size_t o)const{return static_cast<uint32_t>(ram_[o]|(ram_[o+1]<<8)|(ram_[o+2]<<16));}
 int32_t s24(size_t o)const{uint32_t v=r24(o); if(v&0x800000)v|=0xff000000;return static_cast<int32_t>(v);}
 void w16(size_t o,uint16_t v){ram_[o]=static_cast<uint8_t>(v&0xff);ram_[o+1]=static_cast<uint8_t>(v>>8);}
 void w24(size_t o,int64_t v){uint32_t x=static_cast<uint32_t>(v)&0xffffff;ram_[o]=static_cast<uint8_t>(x&0xff);ram_[o+1]=static_cast<uint8_t>((x>>8)&0xff);ram_[o+2]=static_cast<uint8_t>((x>>16)&0xff);}
 uint8_t romAt(uint32_t snes)const{if(rom_.empty())return 0xff;snes&=0xffffffu;if(isRomAddress(snes))return rom_[romLinear(snes)];return 0x00;}
 void loadMemory(){uint32_t src=r24(0x1f40),len=r16(0x1f43),dst=r16(0x1f45);for(uint32_t i=0;i<len;i++)if((dst+i)<ram_.size())ram_[dst+i]=romAt((src+i)&0xffffff);}
 static int32_t sar(int64_t v,unsigned n){return static_cast<int32_t>(v>>n);}
 void execute(uint8_t cmd){
  if(ram_[0x1f4d]==0x0e && cmd<0x40 && (cmd&3)==0){ram_[0x1f80]=uint8_t(cmd>>2);return;}
  const uint8_t mode=ram_[0x1f4d];
  if(mode==0x0e && cmd<0x40 && (cmd&3)==0){ram_[0x1f80]=cmd>>2;return;}
  switch(cmd){
   case 0x05:{int32_t out=0x10000;uint16_t d=r16(0x1f83);if(d)out=sar((static_cast<int64_t>(0x10000)/d)*r16(0x1f81),8);w16(0x1f80,static_cast<uint16_t>(out));break;}
   case 0x0d:{double x=s16(0x1f80),y=s16(0x1f83),target=s16(0x1f86);double d=std::sqrt(x*x+y*y);if(d>0){w16(0x1f89,static_cast<int16_t>(x*(target/d)*0.98));w16(0x1f8c,static_cast<int16_t>(y*(target/d)*0.99));}break;}
   case 0x10: case 0x13:{
    const uint16_t ang=r16(0x1f80)&0x1ff;
    const int32_t rad=(cmd==0x10)?int32_t(s16(0x1f83)):int32_t(r16(0x1f83));
    int32_t x,y;
    if(cmd==0x10){
      x=sar(int64_t(rad)*cx4CosQ15(ang)*2,16);
      y=sar(int64_t(rad)*cx4SinQ15(ang)*2,16);
      y-=sar(y,6);
    }else{
      x=sar(int64_t(rad)*cx4CosQ15(ang)*2,8);
      y=sar(int64_t(rad)*cx4SinQ15(ang)*2,8);
    }
    w24(0x1f86,x);w24(0x1f89,y);break;
   }
   case 0x15:{double x=s16(0x1f80),y=s16(0x1f83);w16(0x1f80,static_cast<uint16_t>(static_cast<int16_t>(std::sqrt(x*x+y*y))));break;}
   case 0x1f:{int x=s16(0x1f80),y=s16(0x1f83);int a;if(x==0)a=y>0?0x80:0x180;else{a=static_cast<int>(std::atan(static_cast<double>(y)/x)/(2*PI)*512);if(x<0)a+=0x100;a&=0x1ff;}w16(0x1f86,static_cast<uint16_t>(a));break;}
   case 0x00: processSprites(); break;
   case 0x01:{std::fill(ram_.begin()+0x300,ram_.begin()+0xc00,0);drawWireFrame();break;}
   case 0x22:{trapezoid();break;}
   case 0x25:{int64_t p=static_cast<int64_t>(s24(0x1f80))*s24(0x1f83);w24(0x1f80,p);break;}
   case 0x2d: transform(); break;
   case 0x40:{uint32_t sum=0;for(size_t i=0;i<0x800;i++)sum+=ram_[i];w16(0x1f80,static_cast<uint16_t>(sum));break;}
   case 0x54:{int64_t x=s24(0x1f80);uint64_t q=static_cast<uint64_t>(x*x);w24(0x1f83,q);w24(0x1f86,q>>24);break;}
   case 0x5c:{static constexpr uint8_t pat[48]={0,0,0,255,255,255,0,255,0,0,0,255,255,255,0,0,255,255,0,0,128,255,255,127,0,128,0,255,127,0,255,127,255,127,255,255,0,0,1,255,255,254,0,1,0,255,254,0};std::memcpy(ram_.data(),pat,48);break;}
   case 0x89: ram_[0x1f80]=0x36;ram_[0x1f81]=0x43;ram_[0x1f82]=0x05;break;
   default: break;
  }
 }
 void processSprites(){
  switch(ram_[0x1f4d]){
   case 0x00: buildOam(); break;
   case 0x03: scaleRotate(0); break;
   case 0x05: transformLines(); break;
   case 0x07: scaleRotate(64); break;
   case 0x08: drawWireFrame(); break;
   case 0x0b: disintegrate(); break;
   case 0x0c: bitPlaneWave(); break;
   default: break;
  }
 }
 void buildOam(){
  size_t oam=static_cast<size_t>(ram_[0x626])<<2;
  if(oam>=0x200) return;
  for(int i=0x1fd;i>static_cast<int>(oam);i-=4) ram_[static_cast<size_t>(i)]=0xe0;
  size_t hi=0x200+(ram_[0x626]>>2);
  const int16_t globalX=static_cast<int16_t>(r16(0x621));
  const int16_t globalY=static_cast<int16_t>(r16(0x623));
  int remaining=128-ram_[0x626];
  unsigned shift=(ram_[0x626]&3u)*2u;
  size_t src=0x220;
  for(unsigned obj=0;obj<ram_[0x620] && remaining>0 && src+15<ram_.size();++obj,src+=16){
   const int16_t baseX=static_cast<int16_t>(r16(src))-globalX;
   const int16_t baseY=static_cast<int16_t>(r16(src+2))-globalY;
   const uint8_t name=ram_[src+5];
   const uint8_t attr=uint8_t(ram_[src+4]|ram_[src+6]);
   uint32_t list=r24(src+7);
   uint8_t count=romAt(list);
   if(count){
    ++list;
    for(unsigned n=0;n<count && remaining>0;++n,list+=4){
     const uint8_t flags=romAt(list);
     int16_t x=static_cast<int8_t>(romAt(list+1));
     int16_t y=static_cast<int8_t>(romAt(list+2));
     if(attr&0x40) x=static_cast<int16_t>(-x-((flags&0x20)?16:8));
     if(attr&0x80) y=static_cast<int16_t>(-y-((flags&0x20)?16:8));
     x=static_cast<int16_t>(x+baseX); y=static_cast<int16_t>(y+baseY);
     if(x < -16 || x > 272 || y < -16 || y > 224) continue;
     if(oam+3>=0x200 || hi>=0x220) break;
     ram_[oam]=uint8_t(x); ram_[oam+1]=uint8_t(y); ram_[oam+2]=uint8_t(name+romAt(list+3)); ram_[oam+3]=uint8_t(attr^(flags&0xc0));
     ram_[hi]=uint8_t(ram_[hi]&~(3u<<shift));
     if(x&0x100) ram_[hi]|=uint8_t(1u<<shift);
     if(flags&0x20) ram_[hi]|=uint8_t(2u<<shift);
     oam+=4; --remaining; shift=(shift+2)&6; if(!shift) ++hi;
    }
   } else if(remaining>0 && oam+3<0x200 && hi<0x220){
    ram_[oam]=uint8_t(baseX); ram_[oam+1]=uint8_t(baseY); ram_[oam+2]=name; ram_[oam+3]=attr;
    ram_[hi]=uint8_t(ram_[hi]&~(3u<<shift)); ram_[hi]|=static_cast<uint8_t>(static_cast<uint8_t>((baseX&0x100)?3u:2u)<<shift);
    oam+=4; --remaining; shift=(shift+2)&6; if(!shift) ++hi;
   }
  }
 }
 void bitPlaneWave(){
  static constexpr uint16_t bmp[40]={
   0x0000,0x0002,0x0004,0x0006,0x0008,0x000a,0x000c,0x000e,
   0x0200,0x0202,0x0204,0x0206,0x0208,0x020a,0x020c,0x020e,
   0x0400,0x0402,0x0404,0x0406,0x0408,0x040a,0x040c,0x040e,
   0x0600,0x0602,0x0604,0x0606,0x0608,0x060a,0x060c,0x060e,
   0x0800,0x0802,0x0804,0x0806,0x0808,0x080a,0x080c,0x080e};
  size_t dst=0; unsigned wave=ram_[0x1f83]&0x7f; uint16_t mask1=0xc0c0,mask2=0x3f3f;
  auto pass=[&](size_t pattern){
   do{
    int height=-static_cast<int8_t>(ram_[0xb00+wave])-16;
    for(unsigned i=0;i<40;++i){
     const size_t at=dst+bmp[i]; if(at+1>=ram_.size()) continue;
     uint16_t tmp=uint16_t(ram_[at]|(uint16_t(ram_[at+1])<<8))&mask2;
     if(height>=0){ uint16_t src=0xff00; if(height<8){size_t po=pattern+size_t(height)*2;src=uint16_t(ram_[po]|(uint16_t(ram_[po+1])<<8));} tmp|=uint16_t(mask1&src); }
     ram_[at]=uint8_t(tmp);ram_[at+1]=uint8_t(tmp>>8);++height;
    }
    wave=(wave+1)&0x7f; mask1=uint16_t((mask1>>2)|(mask1<<6));mask2=uint16_t((mask2>>2)|(mask2<<6));
   }while(mask1!=0xc0c0);
  };
  for(unsigned j=0;j<16;++j){pass(0xa00);dst+=16;pass(0xa10);dst+=16;}
 }
 void trapezoid(){
  const uint16_t a1=r16(0x1f8c)&0x1ff,a2=r16(0x1f8f)&0x1ff;
  const int32_t c1=cx4CosQ15(a1),c2=cx4CosQ15(a2);
  const int32_t s1=cx4SinQ15(a1),s2=cx4SinQ15(a2);
  const int32_t tan1=c1?static_cast<int32_t>((static_cast<int64_t>(s1)<<16)/c1):int32_t(0x80000000u);
  const int32_t tan2=c2?static_cast<int32_t>((static_cast<int64_t>(s2)<<16)/c2):int32_t(0x80000000u);
  int16_t y=int16_t(r16(0x1f83)-r16(0x1f89));
  const int16_t xOrigin=s16(0x1f80),xCenter=s16(0x1f86),rightOffset=s16(0x1f93);
  for(int j=0;j<225;++j,++y){
   int32_t left=1,right=0;
   if(y>=0){
    left=sar(static_cast<int64_t>(tan1)*y,16)-xOrigin+xCenter;
    right=sar(static_cast<int64_t>(tan2)*y,16)-xOrigin+xCenter+rightOffset;
    if(left<0&&right<0){left=1;right=0;}
    else{if(left<0)left=0;if(right<0)right=0;if(left>255&&right>255){left=255;right=254;}else{if(left>255)left=255;if(right>255)right=255;}}
   }
   ram_[0x800+j]=uint8_t(left);ram_[0x900+j]=uint8_t(right);
  }
 }
 static void calcLine(int16_t x1,int16_t y1,int16_t x2,int16_t y2,int16_t& dx,int16_t& dy,int16_t& dist){
  int vx=int(x2)-x1, vy=int(y2)-y1;
  if(std::abs(vx)>std::abs(vy)){dist=static_cast<int16_t>(std::abs(vx)+1);dy=static_cast<int16_t>(256.0*vy/std::abs(vx));dx=static_cast<int16_t>(vx<0?-256:256);}
  else if(vy){dist=static_cast<int16_t>(std::abs(vy)+1);dx=static_cast<int16_t>(256.0*vx/std::abs(vy));dy=static_cast<int16_t>(vy<0?-256:256);}
  else {dist=0;dx=dy=0;}
 }
 static void wireTransform(int16_t& x,int16_t& y,int16_t z,int rx,int ry,int rz,int scale,bool perspective){
  double xx=x,yy=y,zz=z-(perspective?0x95:0);
  double a=-rx*2.0*PI/128.0; double y2=yy*std::cos(a)-zz*std::sin(a), z2=yy*std::sin(a)+zz*std::cos(a);
  a=-ry*2.0*PI/128.0; double x2=xx*std::cos(a)+z2*std::sin(a); zz=-xx*std::sin(a)+z2*std::cos(a);
  a=-rz*2.0*PI/128.0; xx=x2*std::cos(a)-y2*std::sin(a); yy=x2*std::sin(a)+y2*std::cos(a);
  if(perspective){double den=0x90*(zz+0x95);if(std::abs(den)<1e-9){x=y=0;return;}x=static_cast<int16_t>(xx*scale/den*0x95);y=static_cast<int16_t>(yy*scale/den*0x95);}
  else{x=static_cast<int16_t>(xx*scale/256.0);y=static_cast<int16_t>(yy*scale/256.0);}
 }
 void transformLines(){
  const int rx=ram_[0x1f83],ry=ram_[0x1f86],rz=ram_[0x1f89],scale=ram_[0x1f8c];
  size_t ptr=0; const unsigned vc=r16(0x1f80);
  for(unsigned i=0;i<vc && ptr+10<ram_.size();++i,ptr+=0x10){int16_t x=s16(ptr+1),y=s16(ptr+5),z=s16(ptr+9);wireTransform(x,y,z,rx,ry,rz,scale,true);w16(ptr+1,uint16_t(int(x)+0x80));w16(ptr+5,uint16_t(int(y)+0x50));}
  w16(0x600,23);w16(0x602,0x60);w16(0x605,0x40);w16(0x608,23);w16(0x60a,0x60);w16(0x60d,0x40);
  const unsigned ec=r16(0xb00); size_t ep=0xb02,out=0x600;
  for(unsigned i=0;i<ec && ep+1<ram_.size() && out+7<ram_.size();++i,ep+=2,out+=8){unsigned a=ram_[ep],b=ram_[ep+1];size_t pa=size_t(a)<<4,pb=size_t(b)<<4;if(pa+6>=ram_.size()||pb+6>=ram_.size())break;int16_t dx,dy,d;calcLine(s16(pa+1),s16(pa+5),s16(pb+1),s16(pb+5),dx,dy,d);w16(out,uint16_t(d?d:1));w16(out+2,uint16_t(dx));w16(out+5,uint16_t(dy));}
 }
 void drawLine(int16_t x1,int16_t y1,int16_t z1,int16_t x2,int16_t y2,int16_t z2,uint8_t color){
  int16_t ax=x1,ay=y1,bx=x2,by=y2;wireTransform(ax,ay,z1,ram_[0x1f86],ram_[0x1f87],ram_[0x1f88],ram_[0x1f90],false);wireTransform(bx,by,z2,ram_[0x1f86],ram_[0x1f87],ram_[0x1f88],ram_[0x1f90],false);
  int32_t X=(int(ax)+48)<<8,Y=(int(ay)+48)<<8;int16_t dx,dy,d;calcLine(int16_t(X>>8),int16_t(Y>>8),int16_t(int(bx)+48),int16_t(int(by)+48),dx,dy,d);
  for(int i=d?d:1;i>0;--i){if(X>0xff&&Y>0xff&&X<0x6000&&Y<0x6000){int px=X>>8,py=Y>>8;size_t at=size_t((py>>3)*0xc0+(px>>3)*0x10+(py&7)*2)+0x300;uint8_t bit=uint8_t(0x80u>>(px&7));if(at+1<ram_.size()){ram_[at]&=uint8_t(~bit);ram_[at+1]&=uint8_t(~bit);if(color&1)ram_[at]|=bit;if(color&2)ram_[at+1]|=bit;}}X+=dx;Y+=dy;}
 }
 void drawWireFrame(){
  uint32_t line=r24(0x1f80), prevPoint2=0; const uint8_t bank=ram_[0x1f82];
  for(unsigned i=0;i<ram_[0x295];++i,line+=5){uint8_t a0=romAt(line),a1=romAt(line+1),b0=romAt(line+2),b1=romAt(line+3),col=romAt(line+4);uint32_t p1=(a0==0xff&&a1==0xff&&prevPoint2)?prevPoint2:(uint32_t(bank)<<16)|(uint32_t(a0)<<8)|a1;uint32_t p2=(uint32_t(bank)<<16)|(uint32_t(b0)<<8)|b1;auto be16=[&](uint32_t p){return int16_t((uint16_t(romAt(p))<<8)|romAt(p+1));};drawLine(be16(p1),be16(p1+2),be16(p1+4),be16(p2),be16(p2+2),be16(p2+4),col);prevPoint2=p2;}
 }
 static int16_t mulQ15(int16_t trig,int32_t scale){
  return static_cast<int16_t>((int32_t(trig)*scale)>>15);
 }
 void scaleRotate(int rowPadding){
  int32_t xs=r16(0x1f8f), ys=r16(0x1f92);
  if(xs&0x8000) xs=0x7fff;
  if(ys&0x8000) ys=0x7fff;
  const uint16_t ang=r16(0x1f80)&0x1ff;
  int16_t A=0,B=0,C=0,D=0;
  if(ang==0){A=static_cast<int16_t>(xs);D=static_cast<int16_t>(ys);}
  else if(ang==128){B=static_cast<int16_t>(-ys);C=static_cast<int16_t>(xs);}
  else if(ang==256){A=static_cast<int16_t>(-xs);D=static_cast<int16_t>(-ys);}
  else if(ang==384){B=static_cast<int16_t>(ys);C=static_cast<int16_t>(-xs);}
  else {
   A=mulQ15(cx4CosQ15(ang),xs); B=static_cast<int16_t>(-mulQ15(cx4SinQ15(ang),ys));
   C=mulQ15(cx4SinQ15(ang),xs); D=mulQ15(cx4CosQ15(ang),ys);
  }
  const uint32_t width=ram_[0x1f89]&~7u, height=ram_[0x1f8c]&~7u;
  if(width==0||height==0) return;
  const size_t clearBytes=std::min<size_t>(ram_.size(),(width+static_cast<uint32_t>(rowPadding)/4u)*height/2u);
  std::fill(ram_.begin(),ram_.begin()+clearBytes,0);
  const int32_t cx=s16(0x1f83), cy=s16(0x1f86);
  int32_t lineX=(cx<<12)-cx*A-cx*B, lineY=(cy<<12)-cy*C-cy*D;
  int outidx=0;
  for(uint32_t oy=0;oy<height;++oy){
   int32_t X=lineX,Y=lineY; uint8_t bit=0x80;
   for(uint32_t ox=0;ox<width;++ox){
    uint8_t px=0; const int32_t sx=X>>12, sy=Y>>12;
    if(sx>=0&&sy>=0&&static_cast<uint32_t>(sx)<width&&static_cast<uint32_t>(sy)<height){
     const uint32_t a=static_cast<uint32_t>(sy)*width+static_cast<uint32_t>(sx); const size_t src=0x600u+(a>>1);
     if(src<ram_.size()){px=ram_[src]; if(a&1) px>>=4; px&=0x0f;}
    }
    if(outidx>=0 && outidx+17<static_cast<int>(ram_.size())){
     if(px&1) ram_[outidx]|=bit;
     if(px&2) ram_[outidx+1]|=bit;
     if(px&4) ram_[outidx+16]|=bit;
     if(px&8) ram_[outidx+17]|=bit;
    }
    bit>>=1; if(!bit){bit=0x80;outidx+=32;} X+=A;Y+=C;
   }
   outidx+=2+rowPadding; if(outidx&0x10) outidx&=~0x10; else outidx-=static_cast<int>(width*4u)+rowPadding;
   lineX+=B;lineY+=D;
  }
 }
 void disintegrate(){
  const uint32_t width=ram_[0x1f89],height=ram_[0x1f8c]; if(!width||!height)return;
  const int32_t cx=s16(0x1f80),cy=s16(0x1f83),sx=s16(0x1f86),sy=s16(0x1f8f);
  int32_t startX=-cx*sx+(cx<<8), startY=-cy*sy+(cy<<8);
  std::vector<uint8_t> source((width*height+1)/2);
  for(size_t i=0;i<source.size() && 0x600+i<ram_.size();++i) source[i]=ram_[0x600+i];
  const size_t clearBytes=std::min<size_t>(ram_.size(),width*height/2u); std::fill(ram_.begin(),ram_.begin()+clearBytes,0);
  size_t spi=0;
  for(uint32_t i=0;i<height;++i){int32_t y=startY+static_cast<int32_t>(i)*sy;
   for(uint32_t j=0;j<width;++j){int32_t x=startX+static_cast<int32_t>(j)*sx; uint8_t packed=spi<source.size()?source[spi]:0;uint8_t px=(j&1)?packed>>4:packed;px&=15;
    int32_t dx=x>>8,dy=y>>8; if(dx>=0&&dy>=0&&static_cast<uint32_t>(dx)<width&&static_cast<uint32_t>(dy)<height){
     size_t idx=static_cast<size_t>(dy>>3)*width*4u+static_cast<size_t>(dx>>3)*32u+static_cast<size_t>(dy&7)*2u;uint8_t mask=uint8_t(0x80u>>(dx&7));
     if(idx+17<ram_.size()){if(px&1)ram_[idx]|=mask;if(px&2)ram_[idx+1]|=mask;if(px&4)ram_[idx+16]|=mask;if(px&8)ram_[idx+17]|=mask;}
    } if(j&1)++spi;
   }
  }
 }
 void transform(){double x=s16(0x1f81),y=s16(0x1f84),z=s16(0x1f87);double ax=-ram_[0x1f89]*2*PI/128.0,ay=-ram_[0x1f8a]*2*PI/128.0,az=-ram_[0x1f8b]*2*PI/128.0;double y2=y*cos(ax)-z*sin(ax),z2=y*sin(ax)+z*cos(ax);double x2=x*cos(ay)+z2*sin(ay);z=-x*sin(ay)+z2*cos(ay);double xr=x2*cos(az)-y2*sin(az),yr=x2*sin(az)+y2*cos(az);double sc=r16(0x1f90)/256.0;w16(0x1f80,static_cast<int16_t>(xr*sc));w16(0x1f83,static_cast<int16_t>(yr*sc));}
 const std::vector<uint8_t>& rom_; std::vector<uint8_t> data_rom_; HG51B hg_; bool lleEnabled_=false;
 std::array<uint8_t,0x2000> ram_{}; std::array<uint8_t,0x40000> cartRam_{}; std::array<uint8_t,32> garbagePattern_{}; bool running_=false,dmaActive_=false,cacheActive_=false,dmaLocked_=false,irqPending_=false,irqAsserted_=false,irqEnabled_=true,suspended_=false;uint32_t suspendCycles_=0,workCycles_=0;uint8_t pendingCommand_=0;uint64_t frac_=0;uint32_t dmaSource_=0,dmaTarget_=0;uint16_t dmaLength_=0,dmaOffset_=0;uint32_t dmaByteCycles_=0;uint8_t cachePage_=0;uint32_t cacheBase_=0;uint16_t cachePB_=0;uint8_t cacheLocks_=0;uint32_t cacheCycles_=0;uint8_t waitRom_=3,waitRam_=3,romConfig_=1;uint32_t master_clock_hz_=21477272u;
 std::array<uint16_t,2> cacheTags_{{0xffffu,0xffffu}}; uint16_t cacheLoadPB_=0; uint32_t cacheLoadBase_=0; bool cacheStartPending_=false; uint8_t pendingStartPc_=0; uint16_t pendingStartPB_=0; uint32_t lleCyclesRemaining_=0;
};
}
std::unique_ptr<CartridgeDevice> createCX4Device(const std::vector<uint8_t>& rom,uint32_t masterClockHz,std::vector<uint8_t> dataRom){return std::make_unique<CX4Device>(rom,masterClockHz,std::move(dataRom));}

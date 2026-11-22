#include "spc7110.hpp"
#include "state_io.hpp"
#include "spc7110_decomp.hpp"
#include <array>
#include <algorithm>
#include <cstdint>
#include <ctime>
#include <vector>
namespace {
class SPC7110Device final : public CartridgeDevice {
public:
    static constexpr uint64_t RTC_INTERFACE_HZ = 32768ull * 64ull;
    SPC7110Device(const std::vector<uint8_t>& rom,std::vector<uint8_t>& ram,bool& dirty,std::vector<uint8_t>& rtc,bool& rtcDirty,bool hasRtc,uint32_t masterFrequency)
      :rom_(rom),ram_(ram),dirty_(dirty),with_rtc_(hasRtc),decompressor_([this](uint32_t a){return dataromRead(a);}),rtc_(rtc),rtc_dirty_(rtcDirty),master_frequency_(masterFrequency ? masterFrequency : 21477272u),rom_mask_(powerOfTwoMask(rom.size())),ram_mask_(powerOfTwoMask(ram.size())){
        if(with_rtc_){if(rtc_.size()<20)rtc_.resize(20,0);synchronizeRtcFromPersistence();}
        reset();
    }
    bool implemented() const override { return true; }
    bool needsClock() const override { return true; }
    void clock(uint64_t masterClocks) override {
        advancePending(dcu_clocks_remaining_,masterClocks,[this]{dcuBeginTransfer();});
        advancePending(mul_clocks_remaining_,masterClocks,[this]{aluMultiply();});
        advancePending(div_clocks_remaining_,masterClocks,[this]{aluDivide();});
        if (with_rtc_ && masterClocks) {
            const uint64_t wholeSeconds = masterClocks / master_frequency_;
            const uint64_t remainderClocks = masterClocks % master_frequency_;
            const uint64_t scaled = rtc_interface_phase_ + remainderClocks * RTC_INTERFACE_HZ;
            const uint64_t interfaceTicks = wholeSeconds != 0 ? RTC_INTERFACE_HZ : scaled / master_frequency_;
            rtc_interface_phase_ = scaled % master_frequency_;
            if (rtc_wait_ticks_ != 0 && interfaceTicks != 0) {
                const uint64_t elapsed = std::min<uint64_t>(rtc_wait_ticks_, interfaceTicks);
                rtc_wait_ticks_ = static_cast<uint8_t>(rtc_wait_ticks_ - elapsed);
                if (rtc_wait_ticks_ == 0) rtc_ready_ = true;
            }
            rtc_master_accum_ += masterClocks;
            while (rtc_master_accum_ >= master_frequency_) {
                rtc_master_accum_ -= master_frequency_;
                rtcTickSecond();
            }
        }
    }
    void preparePersistentSave() override {
        if(!with_rtc_) return;
        const uint64_t now=static_cast<uint64_t>(std::time(nullptr));
        for(unsigned i=0;i<4;++i){uint8_t v=uint8_t(now>>(8*i));if(rtc_[16+i]!=v){rtc_[16+i]=v;rtc_dirty_=true;}}
    }
    uint32_t stateTag() const override{return 0x31375053u;}
    std::vector<uint8_t> saveState() const override{stateio::Writer w;uint32_t v=5;w.pod(v);w.array(regs_);w.pod(dcu_clocks_remaining_);w.pod(mul_clocks_remaining_);w.pod(div_clocks_remaining_);w.pod(dcu_mode_);w.pod(dcu_address_);w.pod(dcu_offset_);w.array(dcu_tile_);w.pod(rtc_state_);w.pod(rtc_mode_);w.pod(rtc_index_);w.pod(rtc_master_accum_);w.pod(rtc_hold_tick_);w.pod(rtc_interface_phase_);w.pod(rtc_wait_ticks_);w.pod(rtc_ready_);w.pod(rtc_mdr_);auto d=decompressor_.saveState();w.vector(d);w.vector(rtc_);return w.take();}
    bool loadState(const std::vector<uint8_t>&b)override{stateio::Reader r(b);uint32_t v=0;std::vector<uint8_t>d,rt;if(!r.pod(v)||v!=5||!r.array(regs_)||!r.pod(dcu_clocks_remaining_)||!r.pod(mul_clocks_remaining_)||!r.pod(div_clocks_remaining_)||!r.pod(dcu_mode_)||!r.pod(dcu_address_)||!r.pod(dcu_offset_)||!r.array(dcu_tile_)||!r.pod(rtc_state_)||!r.pod(rtc_mode_)||!r.pod(rtc_index_)||!r.pod(rtc_master_accum_)||!r.pod(rtc_hold_tick_)||!r.pod(rtc_interface_phase_)||!r.pod(rtc_wait_ticks_)||!r.pod(rtc_ready_)||!r.pod(rtc_mdr_)||!r.vector(d)||!r.vector(rt,1024)||!r.done())return false;if(!decompressor_.loadState(d))return false;if(with_rtc_){if(rt.size()<20)return false;rtc_=std::move(rt);rtc_dirty_=true;}return dcu_mode_<=3&&dcu_offset_<32&&dcu_clocks_remaining_<=20&&mul_clocks_remaining_<=30&&div_clocks_remaining_<=40&&rtc_state_<=WriteState&&(rtc_mode_==Linear||rtc_mode_==Indexed)&&rtc_index_<16&&rtc_master_accum_<master_frequency_&&rtc_interface_phase_<master_frequency_&&rtc_wait_ticks_<=8;}
    void reset() override {
        regs_.fill(0); regs_[0x31]=0;regs_[0x32]=1;regs_[0x33]=2;
        dcu_clocks_remaining_=mul_clocks_remaining_=div_clocks_remaining_=0; dcu_mode_=0; dcu_address_=0; dcu_offset_=0; dcu_tile_.fill(0); rtc_state_=Inactive; rtc_mode_=Linear; rtc_index_=0; rtc_wait_ticks_=0; rtc_ready_=false; rtc_mdr_=0; decompressor_.reset();
        if(with_rtc_ && rtc_.size()<20) rtc_.resize(20,0);
    }
    bool readHost(uint32_t address,uint8_t openBus,uint8_t& value) override {
        uint8_t bank=uint8_t(address>>16); uint16_t a=uint16_t(address);
        if(bank==0x50){ value=readReg(0x4800,openBus); return true; }
        if(bank==0x58){ value=readReg(0x4808,openBus); return true; }
        if((bank<=0x3f||(bank>=0x80&&bank<=0xbf)) && a>=0x4800 && a<=0x4842){
            value=readReg(a,openBus); return true;
        }
        if(isRam(bank,a)){ value=(regs_[0x30]&0x80)?ramRead(ramOffset(bank,a)):0x00; return true; }
        if(bank>=0xd0){
            unsigned slot=(bank>>4)-0x0d; unsigned sel=regs_[0x31+slot]&7;
            value=dataromRead(uint32_t((size_t(sel)<<20)|(address&0x0fffff))); return true;
        }
        return false;
    }
    bool write(uint32_t address,uint8_t value) override {
        uint8_t bank=uint8_t(address>>16); uint16_t a=uint16_t(address);
        if((bank<=0x3f||(bank>=0x80&&bank<=0xbf)) && a>=0x4800 && a<=0x4842){ writeReg(a,value); return true; }
        if(isRam(bank,a)){ if(regs_[0x30]&0x80) ramWrite(ramOffset(bank,a),value); return true; }
        return false;
    }
private:
    static size_t powerOfTwoMask(size_t size){return size && (size & (size-1))==0 ? size-1 : 0;}
    size_t romIndex(size_t p)const{return rom_mask_ ? (p&rom_mask_) : (p%rom_.size());}
    size_t ramIndex(size_t p)const{return ram_mask_ ? (p&ram_mask_) : (p%ram_.size());}
    enum RTCState{Inactive,ModeSelect,SeekState,ReadState,WriteState}; enum RTCMode{Linear=3,Indexed=12};
    static bool isRam(uint8_t b,uint16_t a){return ((b<=0x3f||(b>=0x80&&b<=0xbf))&&a>=0x6000&&a<=0x7fff)||(b>=0x40&&b<=0x4f);}
    static size_t ramOffset(uint8_t b,uint16_t a){ if(b>=0x40&&b<=0x4f)return (size_t(b-0x40)<<16)|a;return (size_t(b&0x1f)<<13)|(a-0x6000); }
    uint8_t ramRead(size_t p)const{return ram_.empty()?0xff:ram_[ramIndex(p)];}
    void ramWrite(size_t p,uint8_t v){if(ram_.empty())return;p=ramIndex(p);if(ram_[p]!=v){ram_[p]=v;dirty_=true;}}
    uint8_t romRead(size_t p)const{return rom_.empty()?0xff:rom_[romIndex(p)];}
    size_t dromBase()const{return rom_.size()>0x100000?0x100000:0;}
    size_t dromSize()const{ if(rom_.empty())return 0; return rom_.size()>0x100000?rom_.size()-0x100000:rom_.size(); }
    static size_t mirrorAddress(size_t addr,size_t size){ if(!size)return 0; size_t base=0,mask=size_t(1)<<23; while(addr>=size){ while(!(addr&mask))mask>>=1; addr-=mask; if(size>mask){size-=mask;base+=mask;} mask>>=1; } return base+addr; }
    uint8_t dataromRead(uint32_t address)const{ const unsigned sel=regs_[0x34]&3; const uint32_t mask=0x100000u*(1u<<sel)-1; if(sel!=3&&(address&0x400000))return 0; size_t sz=dromSize(); if(!sz)return 0xff; return rom_[dromBase()+mirrorAddress(address&mask,sz)]; }
    uint32_t dataOffset()const{return regs_[0x11]|(uint32_t(regs_[0x12])<<8)|(uint32_t(regs_[0x13])<<16);}
    uint16_t dataAdjust()const{return uint16_t(regs_[0x14]|(regs_[0x15]<<8));}
    uint16_t dataStride()const{return uint16_t(regs_[0x16]|(regs_[0x17]<<8));}
    void setDataOffset(uint32_t v){regs_[0x11]=uint8_t(v);regs_[0x12]=uint8_t(v>>8);regs_[0x13]=uint8_t(v>>16);}
    void setDataAdjust(uint16_t v){regs_[0x14]=uint8_t(v);regs_[0x15]=uint8_t(v>>8);}
    void dataPortRead(){uint32_t adjust=(regs_[0x18]&2)?dataAdjust():0u;if(regs_[0x18]&8)adjust=uint32_t(int32_t(int16_t(adjust)));regs_[0x10]=dataromRead((dataOffset()+adjust)&0xffffff);}
    void dataPortIncrement4810(){uint32_t stride=(regs_[0x18]&1)?dataStride():1u;uint32_t adjust=dataAdjust();if(regs_[0x18]&4)stride=uint32_t(int32_t(int16_t(stride)));if(regs_[0x18]&8)adjust=uint32_t(int32_t(int16_t(adjust)));if(!(regs_[0x18]&16))setDataOffset(dataOffset()+stride);else setDataAdjust(uint16_t(adjust+stride));dataPortRead();}
    void dataPortIncrementAdjust(unsigned mode){if((regs_[0x18]>>5)!=mode)return;uint32_t adjust=dataAdjust();if(regs_[0x18]&8)adjust=uint32_t(int32_t(int16_t(adjust)));setDataOffset(dataOffset()+adjust);dataPortRead();}
    template<class F> static void advancePending(uint8_t& remaining,uint64_t clocks,F&& done){if(!remaining)return;if(clocks<remaining){remaining=uint8_t(remaining-clocks);return;}remaining=0;done();}
    void dcuLoadAddress(){uint32_t table=regs_[1]|uint32_t(regs_[2])<<8|uint32_t(regs_[3])<<16;uint32_t address=(table+(uint32_t(regs_[4])<<2))&0xffffff;dcu_mode_=dataromRead(address)&3;dcu_address_=(uint32_t(dataromRead(address+1))<<16)|(uint32_t(dataromRead(address+2))<<8)|dataromRead(address+3);}
    void dcuBeginTransfer(){if(dcu_mode_==3)return;decompressor_.initialize(dcu_mode_,dcu_address_);decompressor_.decode();uint16_t seek=(regs_[0x0b]&2)?uint16_t(regs_[5]|regs_[6]<<8):uint16_t(0);while(seek--)decompressor_.decode();regs_[12]|=0x80;dcu_offset_=0;}
    uint8_t dcuRead(){
        if(!(regs_[12]&0x80))return 0;
        if(dcu_offset_==0){
            for(unsigned row=0;row<8;++row){
                const uint32_t result=decompressor_.result();
                switch(decompressor_.bpp()){
                case 1:dcu_tile_[row]=uint8_t(result);break;
                case 2:dcu_tile_[row*2]=uint8_t(result);dcu_tile_[row*2+1]=uint8_t(result>>8);break;
                case 4:dcu_tile_[row*2]=uint8_t(result);dcu_tile_[row*2+1]=uint8_t(result>>8);dcu_tile_[row*2+16]=uint8_t(result>>16);dcu_tile_[row*2+17]=uint8_t(result>>24);break;
                }
                uint8_t seek=(regs_[0x0b]&1)?regs_[7]:uint8_t(1);
                while(seek--)decompressor_.decode();
            }
        }
        const uint8_t data=dcu_tile_[dcu_offset_++];
        dcu_offset_&=8*decompressor_.bpp()-1;
        return data;
    }
    uint8_t readReg(uint16_t a,uint8_t open){
        if(a==0x4800){uint16_t c=uint16_t(regs_[9]|(regs_[10]<<8));--c;regs_[9]=uint8_t(c);regs_[10]=uint8_t(c>>8);return dcuRead();}
        if(a==0x4808)return 0;
        if(a==0x4810){uint8_t v=regs_[0x10];dataPortIncrement4810();return v;}
        if(a==0x481a){dataPortIncrementAdjust(3);return 0;}
        if(a==0x4841){
            if(!with_rtc_ || rtc_state_==Inactive || !rtc_ready_) return 0;
            if(rtc_state_==WriteState) return rtc_mdr_ & 0x0f;
            if(rtc_state_!=ReadState) return 0;
            const uint8_t v=rtc_[rtc_index_&15]&0x0f;
            rtc_index_=(rtc_index_+1)&15;
            beginRtcWait();
            return v;
        }
        if(a==0x4842){if(!with_rtc_)return open;return rtc_ready_?0x80:0x00;}
        if((a>=0x4801&&a<=0x480c)||(a>=0x4811&&a<=0x4818)||(a>=0x4820&&a<=0x482f)||(a>=0x4830&&a<=0x4834)) return regs_[a-0x4800];
        return open;
    }
    void writeReg(uint16_t a,uint8_t d){
        const unsigned r=a-0x4800;
        switch(a){
        case 0x4801:case 0x4802:case 0x4803:case 0x4805:case 0x4807:case 0x4809:case 0x480a:case 0x4811:case 0x4812:case 0x4816:case 0x4817:case 0x4820:case 0x4821:case 0x4822:case 0x4823:case 0x4824:case 0x4826:regs_[r]=d;return;
        case 0x4804:regs_[r]=d;dcuLoadAddress();return;
        case 0x4806:regs_[r]=d;regs_[12]&=0x7f;dcu_clocks_remaining_=20;return;
        case 0x480b:regs_[r]=d&3;return;
        case 0x4813:regs_[r]=d;dataPortRead();return;
        case 0x4814:regs_[r]=d;dataPortIncrementAdjust(1);return;
        case 0x4815:regs_[r]=d;if(regs_[0x18]&2)dataPortRead();dataPortIncrementAdjust(2);return;
        case 0x4818:regs_[r]=d&0x7f;dataPortRead();return;
        case 0x4825:regs_[r]=d;regs_[0x2f]|=0x81;mul_clocks_remaining_=30;return;
        case 0x4827:regs_[r]=d;regs_[0x2f]|=0x80;div_clocks_remaining_=40;return;
        case 0x482e:regs_[r]=d&1;return;
        case 0x4830:regs_[r]=d&0x87;return;
        case 0x4831:case 0x4832:case 0x4833:case 0x4834:regs_[r]=d&7;return;
        }
        if(a==0x4840&&with_rtc_){
            if(!(d&1)){rtc_state_=Inactive;rtc_wait_ticks_=0;rtc_ready_=true;}
            else{rtc_state_=ModeSelect;rtc_index_=0;rtc_wait_ticks_=0;rtc_ready_=true;}
            return;
        }
        if(a==0x4841&&with_rtc_){ rtcWrite(d); return; }
    }
    void aluMultiply(){
        uint32_t result;
        if(regs_[0x2e]&1)result=uint32_t(int32_t(int16_t(regs_[0x24]|regs_[0x25]<<8))*int32_t(int16_t(regs_[0x20]|regs_[0x21]<<8)));
        else result=uint32_t(uint16_t(regs_[0x24]|regs_[0x25]<<8))*uint32_t(uint16_t(regs_[0x20]|regs_[0x21]<<8));
        for(unsigned i=0;i<4;i++)regs_[0x28+i]=uint8_t(result>>(8*i));
        regs_[0x2f]&=0x7f;
    }
    void aluDivide(){
        const uint32_t dividend=regs_[0x20]|uint32_t(regs_[0x21])<<8|uint32_t(regs_[0x22])<<16|uint32_t(regs_[0x23])<<24;
        const uint16_t divisor=uint16_t(regs_[0x26]|regs_[0x27]<<8);
        uint32_t quotient=0;uint16_t remainder=uint16_t(dividend);
        if(divisor){
            if(regs_[0x2e]&1){int32_t n=int32_t(dividend);int32_t dv=int16_t(divisor);quotient=uint32_t(n/dv);remainder=uint16_t(n%dv);}
            else{quotient=dividend/divisor;remainder=uint16_t(dividend%divisor);}
        }
        for(unsigned i=0;i<4;i++)regs_[0x28+i]=uint8_t(quotient>>(8*i));
        regs_[0x2c]=uint8_t(remainder);regs_[0x2d]=uint8_t(remainder>>8);
        regs_[0x2f]&=0x7f;
    }
    void beginRtcWait(){rtc_ready_=false;rtc_wait_ticks_=8;}
    void rtcWrite(uint8_t d){
        d&=15; regs_[0x41]=d;
        if(rtc_state_==Inactive || !rtc_ready_) return;
        if(rtc_state_==ModeSelect){
            if(d!=Linear && d!=Indexed) return;
            rtc_mode_=RTCMode(d); rtc_state_=SeekState; rtc_index_=0; rtc_mdr_=d; beginRtcWait(); return;
        }
        if(rtc_state_==SeekState){
            rtc_state_=(rtc_mode_==Linear)?WriteState:ReadState; rtc_index_=d&15; rtc_mdr_=d; beginRtcWait(); return;
        }
        if(rtc_state_!=WriteState) return;
        const unsigned index=rtc_index_&15;
        const uint8_t old=rtc_[index];
        if(old!=d){rtc_[index]=d;rtc_dirty_=true;}
        if(index==13){
            const bool wasHeld=(old&1)!=0, held=(d&1)!=0;
            if(wasHeld&&!held&&rtc_hold_tick_){rtc_hold_tick_=false;rtcIncrementSecond();}
        }else if(index==15){
            if(d&1){
                if(rtc_[0]){rtc_[0]=0;rtc_dirty_=true;} if(rtc_[1]){rtc_[1]=0;rtc_dirty_=true;}
            }
        }
        rtc_index_=(rtc_index_+1)&15; rtc_mdr_=d; beginRtcWait();
    }
    static bool rtcLeap(unsigned y){return y%4==0&&(y%100!=0||y%400==0);}
    void rtcIncrementSecond(){
        unsigned sec=rtc_[0]+rtc_[1]*10, min=rtc_[2]+rtc_[3]*10, hour=rtc_[4]+rtc_[5]*10;
        unsigned day=std::max(1u,unsigned(rtc_[6]+rtc_[7]*10));
        unsigned mon=std::max(1u,unsigned(rtc_[8]+rtc_[9]*10));
        unsigned yr=rtc_[10]+rtc_[11]*10+(rtc_[11]>=9?1900:2000), wd=rtc_[12]%7;
        static const unsigned md[12]={31,28,31,30,31,30,31,31,30,31,30,31};
        if(++sec>=60){sec=0;if(++min>=60){min=0;if(++hour>=24){hour=0;if(rtc_[13]&2){++day;wd=(wd+1)%7;unsigned lim=md[(mon-1)%12]+((mon==2&&rtcLeap(yr))?1:0);if(day>lim){day=1;if(++mon>12){mon=1;++yr;}}}}}}
        auto put=[&](size_t i,uint8_t v){if(rtc_[i]!=v){rtc_[i]=v;rtc_dirty_=true;}};
        put(0,sec%10);put(1,sec/10);put(2,min%10);put(3,min/10);put(4,hour%10);put(5,hour/10);
        put(6,day%10);put(7,day/10);put(8,mon%10);put(9,mon/10);yr%=100;put(10,yr%10);put(11,yr/10);put(12,wd);
    }
    void rtcTickSecond(){
        if(!with_rtc_ || (rtc_[15]&3)) return;
        if(rtc_[13]&1){rtc_hold_tick_=true;return;}
        rtcIncrementSecond();
    }
    void rtcAdvanceSeconds(uint64_t n){while(n--)rtcTickSecond();}
    void synchronizeRtcFromPersistence(){
        if(!with_rtc_||rtc_.size()<20)return;
        uint64_t prev=0;for(unsigned i=0;i<4;++i)prev|=uint64_t(rtc_[16+i])<<(8*i);
        if(!prev)return;
        uint64_t now=static_cast<uint64_t>(std::time(nullptr));
        if(now>prev)rtcAdvanceSeconds(now-prev);
    }
    const std::vector<uint8_t>& rom_;std::vector<uint8_t>& ram_;bool& dirty_;bool with_rtc_;std::array<uint8_t,0x43> regs_{};SPC7110Decompressor decompressor_;uint8_t dcu_clocks_remaining_=0,mul_clocks_remaining_=0,div_clocks_remaining_=0;uint8_t dcu_mode_=0;uint32_t dcu_address_=0;uint32_t dcu_offset_=0;std::array<uint8_t,32> dcu_tile_{};std::vector<uint8_t>& rtc_;bool& rtc_dirty_;RTCState rtc_state_=Inactive;RTCMode rtc_mode_=Linear;unsigned rtc_index_=0;uint32_t master_frequency_=21477272u;uint64_t rtc_master_accum_=0;bool rtc_hold_tick_=false;uint64_t rtc_interface_phase_=0;uint8_t rtc_wait_ticks_=0;bool rtc_ready_=false;uint8_t rtc_mdr_=0;const size_t rom_mask_;const size_t ram_mask_;
};
}
std::unique_ptr<CartridgeDevice> createSPC7110Device(const std::vector<uint8_t>& rom,std::vector<uint8_t>& ram,bool& dirty,std::vector<uint8_t>& rtc,bool& rtcDirty,bool hasRtc,uint32_t masterFrequency){return std::make_unique<SPC7110Device>(rom,ram,dirty,rtc,rtcDirty,hasRtc,masterFrequency);}

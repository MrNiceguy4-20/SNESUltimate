#include "nec_dsp.hpp"
#include "state_io.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>
namespace {
constexpr uint16_t SR_RQM = 0x8000;
constexpr uint16_t SR_DRS = 0x1000;
constexpr uint16_t SR_DRC = 0x0400;
constexpr uint16_t SR_SOC = 0x0200;
constexpr uint16_t SR_SIC = 0x0100;
struct Flags {
    bool c=false, z=false, ov0=false, ov1=false, s0=false, s1=false;
};
class NecDspDevice final : public CartridgeDevice {
public:
    NecDspDevice(NecDspType type, DSP1Map map, NecDspFirmware fw,
                 uint32_t masterHz, std::vector<uint8_t>* extRam, bool* dirty)
        : type_(type), map_(map), programBytes_(std::move(fw.program)),
          masterHz_(masterHz ? masterHz : 21477272), extRam_(extRam), dirty_(dirty) {
        const bool upd96050 = type_ == NecDspType::ST010 || type_ == NecDspType::ST011;
        frequencyHz_ = type_ == NecDspType::ST010 ? 11000000u :
                       type_ == NecDspType::ST011 ? 22000000u : 7600000u;
        ram_.assign(upd96050 ? 0x800u : 0x100u, 0);
        stack_.assign(16u, 0);
        const std::size_t words = fw.data.size()/2;
        dataRom_.resize(words);
        for(std::size_t i=0;i<words;i++) dataRom_[i]=uint16_t(fw.data[i*2]) | (uint16_t(fw.data[i*2+1])<<8);
        const std::size_t ops = programBytes_.size()/3;
        program_.resize(ops);
        for(std::size_t i=0;i<ops;i++) program_[i]=uint32_t(programBytes_[i*3]) | (uint32_t(programBytes_[i*3+1])<<8) | (uint32_t(programBytes_[i*3+2])<<16);
        reset();
    }
    bool implemented() const override { return !program_.empty() && !dataRom_.empty(); }
    CartridgeSupportLevel supportLevel() const override { return CartridgeSupportLevel::FirmwareLLE; }
    bool needsClock() const override { return true; }
    bool hostIrqMayChangeWithoutHostAccess() const override { return false; }
    uint32_t stateTag() const override { return 0x37445055u; }
    void reset() override {
        a_=b_=tr_=trb_=dp_=rp_=dr_=sr_=k_=l_=m_=n_=si_=so_=pc_=0;
        sp_=0; fa_={}; fb_={}; frac_=0; waitRqm_=false;
        std::fill(ram_.begin(),ram_.end(),0);
        if (is96050() && extRam_ && extRam_->size() >= 0x1000) {
            for (std::size_t i=0;i<ram_.size();++i) ram_[i]=uint16_t((*extRam_)[i*2]) | (uint16_t((*extRam_)[i*2+1])<<8);
        }
        std::fill(stack_.begin(),stack_.end(),0);
    }
    void clock(uint64_t masterClocks) override {
        if(!implemented()) return;
        const uint64_t whole = masterClocks / masterHz_;
        const uint64_t rem = masterClocks % masterHz_;
        uint64_t cycles = whole * frequencyHz_;
        const uint64_t scaled = rem * static_cast<uint64_t>(frequencyHz_) + frac_;
        cycles += scaled / masterHz_;
        frac_ = scaled % masterHz_;
        if(waitRqm_) return;
        while(cycles-- && !waitRqm_) step();
    }
    bool read(uint32_t address,uint8_t& value) override {
        if(!owns(address)) return false;
        if(isSharedRam(address)) { value=readSharedRam(address); return true; }
        if(isStatus(address)) { value=uint8_t(statusValue()>>8); return true; }
        waitRqm_=false;
        if(sr_ & SR_DRC) {
            sr_ &= ~SR_RQM;
            value=uint8_t(dr_);
        } else if(sr_ & SR_DRS) {
            sr_ &= ~(SR_RQM|SR_DRS);
            value=uint8_t(dr_>>8);
        } else {
            sr_ |= SR_DRS;
            value=uint8_t(dr_);
        }
        return true;
    }
    bool write(uint32_t address,uint8_t value) override {
        if(!owns(address)) return false;
        if(isSharedRam(address)) { writeSharedRam(address,value); return true; }
        if(isStatus(address)) return true;
        waitRqm_=false;
        if(sr_ & SR_DRC) {
            sr_ &= ~SR_RQM;
            dr_=uint16_t((dr_&0xff00)|value);
        } else if(sr_ & SR_DRS) {
            sr_ &= ~(SR_RQM|SR_DRS);
            dr_=uint16_t((dr_&0x00ff)|(uint16_t(value)<<8));
        } else {
            sr_ |= SR_DRS;
            dr_=uint16_t((dr_&0xff00)|value);
        }
        return true;
    }
    std::vector<uint8_t> saveState() const override {
        stateio::Writer w; uint32_t ver=1; w.pod(ver); w.pod(type_); w.pod(map_);
        w.pod(a_);w.pod(b_);w.pod(tr_);w.pod(trb_);w.pod(dp_);w.pod(rp_);w.pod(dr_);w.pod(sr_);
        w.pod(k_);w.pod(l_);w.pod(m_);w.pod(n_);w.pod(si_);w.pod(so_);w.pod(pc_);w.pod(sp_);
        w.pod(fa_);w.pod(fb_);w.pod(frac_);w.pod(waitRqm_);w.vector(ram_);w.vector(stack_);return w.take();
    }
    bool loadState(const std::vector<uint8_t>& bytes) override {
        stateio::Reader r(bytes); uint32_t ver=0; NecDspType t{}; DSP1Map m{};
        if(!r.pod(ver)||ver!=1||!r.pod(t)||!r.pod(m)||t!=type_||m!=map_) return false;
#define RD(x) if(!r.pod(x))return false
        RD(a_);RD(b_);RD(tr_);RD(trb_);RD(dp_);RD(rp_);RD(dr_);RD(sr_);RD(k_);RD(l_);RD(m_);RD(n_);RD(si_);RD(so_);RD(pc_);RD(sp_);RD(fa_);RD(fb_);RD(frac_);RD(waitRqm_);
#undef RD
        std::vector<uint16_t> ram,stack; if(!r.vector(ram)||!r.vector(stack)||!r.done())return false;
        if(ram.size()!=ram_.size()) return false;
        ram_=std::move(ram);
        if (stack.size() != stack_.size()) {
            const std::size_t legacy = is96050() ? 8u : 4u;
            if (stack.size() != legacy) return false;
            stack.resize(stack_.size(), 0);
        }
        stack_=std::move(stack);
        normalizeRegisterWidths();
        return sp_<stack_.size();
    }
private:
    bool is96050() const { return type_==NecDspType::ST010||type_==NecDspType::ST011; }
    uint16_t statusValue() const {
        return (sr_ & SR_DRC) ? uint16_t(sr_ & ~SR_DRS) : sr_;
    }
    uint16_t pcMask() const { return is96050() ? 0x3fffu : 0x07ffu; }
    uint16_t rpMask() const { return is96050() ? 0x07ffu : 0x03ffu; }
    uint16_t dpMask() const { return is96050() ? 0x07ffu : 0x00ffu; }
    void normalizeRegisterWidths() {
        pc_ &= pcMask(); rp_ &= rpMask(); dp_ &= dpMask(); sp_ &= 0x0f;
        for (auto& entry : stack_) entry &= pcMask();
    }
    bool owns(uint32_t address) const {
        const uint8_t bank=uint8_t(address>>16); const uint16_t a=uint16_t(address);
        if(is96050()) return ((bank>=0x60&&bank<=0x6f)||(bank>=0xe0&&bank<=0xef)) && a<0x1000;
        if(type_==NecDspType::DSP3) return ((bank>=0x20&&bank<=0x3f)||(bank>=0xa0&&bank<=0xbf))&&a>=0x8000;
        if(type_==NecDspType::DSP4) return ((((bank<=0x1f)||(bank>=0x80&&bank<=0x9f))&&a>=0x6000&&a<0x8000)||(((bank>=0x30&&bank<=0x3f)||(bank>=0xb0&&bank<=0xbf))&&a>=0x8000));
        if(map_==DSP1Map::DSP2LoROM) return ((bank>=0x20&&bank<=0x3f)||(bank>=0xa0&&bank<=0xbf))&&((a>=0x6000&&a<0x7000)||a>=0x8000);
        if(map_==DSP1Map::LoROMSmall) return ((bank>=0x30&&bank<=0x3f)||(bank>=0xb0&&bank<=0xbf))&&a>=0x8000;
        if(map_==DSP1Map::LoROMLarge) return ((bank>=0x60&&bank<=0x6f)||(bank>=0xe0&&bank<=0xef))&&a<0x8000;
        return ((bank<=0x1f)||(bank>=0x80&&bank<=0x9f))&&a>=0x6000&&a<0x8000;
    }
    bool isSharedRam(uint32_t address) const { return is96050() && ((uint8_t(address>>16)&0x0f)>=8); }
    bool isStatus(uint32_t address) const {
        const uint16_t a=uint16_t(address);
        if(is96050()) return (a&1)!=0;
        if(type_==NecDspType::DSP3) return a>=0xc000;
        if(type_==NecDspType::DSP4) return (a>=0x7000&&a<0x8000)||a>=0xc000;
        if(map_==DSP1Map::HiROM) return a>=0x7000;
        return (a&0x4000)!=0;
    }
    uint16_t sharedWordIndex(uint32_t address) const { return uint16_t((address>>1)&0x7ff); }
    uint8_t readSharedRam(uint32_t address) const {
        const uint16_t i=sharedWordIndex(address);
        if(extRam_ && extRam_->size()>=0x1000) return (*extRam_)[(i*2+(address&1))&0xfff];
        const uint16_t v=ram_[i&uint16_t(ram_.size()-1)]; return (address&1)?uint8_t(v>>8):uint8_t(v);
    }
    void writeSharedRam(uint32_t address,uint8_t value) {
        const uint16_t i=sharedWordIndex(address);
        if(extRam_ && extRam_->size()>=0x1000) { auto& x=(*extRam_)[(i*2+(address&1))&0xfff]; if(x!=value){x=value;if(dirty_)*dirty_=true;} }
        uint16_t& w=ram_[i&uint16_t(ram_.size()-1)]; if(address&1)w=uint16_t((w&0xff)|(uint16_t(value)<<8));else w=uint16_t((w&0xff00)|value);
    }
    uint16_t data(std::size_t i) const { return dataRom_.empty()?0:dataRom_[i%dataRom_.size()]; }
    uint16_t& mem(uint16_t i){ return ram_[i&(ram_.size()-1)]; }
    void syncExternalWord(uint16_t i){
        if(!is96050()||!extRam_||extRam_->size()<0x1000)return;
        const uint16_t v=ram_[i&(ram_.size()-1)]; const std::size_t off=(i&0x7ffu)*2u;
        if((*extRam_)[off]!=uint8_t(v)||(*extRam_)[off+1]!=uint8_t(v>>8)){(*extRam_)[off]=uint8_t(v);(*extRam_)[off+1]=uint8_t(v>>8);if(dirty_)*dirty_=true;}
    }
    static bool flag(const Flags& f,int which){switch(which){case 0:return f.c;case 1:return f.z;case 2:return f.ov0;case 3:return f.ov1;case 4:return f.s0;default:return f.s1;}}
    uint16_t source(uint8_t s){
        switch(s){case 0:return trb_;case 1:return a_;case 2:return b_;case 3:return tr_;case 4:return dp_;case 5:return rp_;case 6:return data(rp_);case 7:return uint16_t(0x8000-(fa_.s1?1:0));case 8:sr_|=SR_RQM;return dr_;case 9:return dr_;case 10:return statusValue();case 11:case 12:return si_;case 13:return k_;case 14:return l_;case 15:return mem(dp_);default:return 0;}
    }
    void load(uint8_t d,uint16_t v){
        switch(d){case 0:break;case 1:a_=v;break;case 2:b_=v;break;case 3:tr_=v;break;case 4:dp_=uint16_t(v&dpMask());break;case 5:rp_=uint16_t(v&rpMask());break;case 6:dr_=v;sr_|=SR_RQM;break;case 7:sr_=uint16_t((sr_&0x907c)|(v&~0x907c));break;case 8:case 9:so_=v;break;case 10:k_=v;break;case 11:k_=v;l_=data(rp_);break;case 12:l_=v;k_=mem(uint16_t(dp_|0x40));break;case 13:l_=v;break;case 14:trb_=v;break;case 15:mem(dp_)=v;syncExternalWord(uint16_t(dp_&(ram_.size()-1)));break;}
    }
    void alu(uint32_t op,uint8_t code,uint16_t src){
        const bool selectB=((op>>15)&1)!=0; Flags f=selectB?fb_:fa_; uint16_t acc=selectB?b_:a_; const bool otherC=selectB?fa_.c:fb_.c;
        uint16_t p=0;switch((op>>20)&3){case 0:p=mem(dp_);break;case 1:p=src;break;case 2:p=m_;break;case 3:p=n_;break;}
        uint16_t result=acc;
        switch(code){case 1:result=acc|p;break;case 2:result=acc&p;break;case 3:result=acc^p;break;case 4:result=uint16_t(acc-p);break;case 5:result=uint16_t(acc+p);break;case 6:result=uint16_t(acc-p-(otherC?1:0));break;case 7:result=uint16_t(acc+p+(otherC?1:0));break;case 8:p=1;result=uint16_t(acc-1);break;case 9:p=1;result=uint16_t(acc+1);break;case 10:result=uint16_t(~acc);break;case 11:result=uint16_t((acc>>1)|(acc&0x8000));break;case 12:result=uint16_t((acc<<1)|(otherC?1:0));break;case 13:result=uint16_t((acc<<2)|3);break;case 14:result=uint16_t((acc<<4)|15);break;case 15:result=uint16_t((acc<<8)|(acc>>8));break;default:break;}
        f.z=result==0; f.s0=(result&0x8000)!=0; if(!f.ov1)f.s1=f.s0;
        if(code<=3||code==10||code>=13){f.c=f.ov0=f.ov1=false;}
        else if(code>=4&&code<=9){const uint16_t ov=uint16_t((acc^result)&(p^((code&1)?result:acc)));f.ov0=(ov&0x8000)!=0;if(f.ov0&&f.ov1)f.ov1=(f.s0==f.s1);else f.ov1=f.ov1||f.ov0;f.c=((acc^p^result^ov)&0x8000)!=0;}
        else if(code==11){f.c=(acc&1)!=0;f.ov0=f.ov1=false;}else if(code==12){f.c=(acc&0x8000)!=0;f.ov0=f.ov1=false;}
        if(selectB){b_=result;fb_=f;}else{a_=result;fa_=f;}
    }
    void updateDp(uint32_t op){uint16_t d=dp_;switch((op>>13)&3){case 1:d=uint16_t((d&0xf0)|((d+1)&15));break;case 2:d=uint16_t((d&0xf0)|((d-1)&15));break;case 3:d&=0xf0;break;default:break;}dp_=uint16_t((d^(((op>>9)&15)<<4))&dpMask());}
    void execOp(uint32_t op){const uint16_t src=source(uint8_t((op>>4)&15));const uint8_t ac=uint8_t((op>>16)&15);if(ac)alu(op,ac,src);const uint8_t d=uint8_t(op&15);load(d,src);if(d!=4)updateDp(op);if(((op>>8)&1)&&d!=5)rp_=uint16_t((rp_-1)&rpMask());}
    bool jumpCond(uint16_t jt) const {
        switch(jt){case 0x80:return !fa_.c;case 0x82:return fa_.c;case 0x84:return !fb_.c;case 0x86:return fb_.c;case 0x88:return !fa_.z;case 0x8a:return fa_.z;case 0x8c:return !fb_.z;case 0x8e:return fb_.z;case 0x90:return !fa_.ov0;case 0x92:return fa_.ov0;case 0x94:return !fb_.ov0;case 0x96:return fb_.ov0;case 0x98:return !fa_.ov1;case 0x9a:return fa_.ov1;case 0x9c:return !fb_.ov1;case 0x9e:return fb_.ov1;case 0xa0:return !fa_.s0;case 0xa2:return fa_.s0;case 0xa4:return !fb_.s0;case 0xa6:return fb_.s0;case 0xa8:return !fa_.s1;case 0xaa:return fa_.s1;case 0xac:return !fb_.s1;case 0xae:return fb_.s1;case 0xb0:return !(dp_&15);case 0xb1:return (dp_&15)!=0;case 0xb2:return (dp_&15)==15;case 0xb3:return (dp_&15)!=15;case 0xb4:return !(sr_&SR_SIC);case 0xb6:return (sr_&SR_SIC)!=0;case 0xb8:return !(sr_&SR_SOC);case 0xba:return (sr_&SR_SOC)!=0;case 0xbc:return !(sr_&SR_RQM);case 0xbe:return (sr_&SR_RQM)!=0;default:return false;}
    }
    void jump(uint32_t op){const uint16_t address=uint16_t((op>>2)&0x7ff);const uint16_t bank=uint16_t(op&3);const uint16_t target=uint16_t(((pc_&0x2000)|(bank<<11)|address)&pcMask());const uint16_t jt=uint16_t((op>>13)&0x1ff);
        if(jt==0){pc_=uint16_t(so_&pcMask());return;} if(jt==0x100){pc_=uint16_t((target&~0x2000)&pcMask());return;}if(jt==0x101){pc_=uint16_t((target|0x2000)&pcMask());return;}if(jt==0x140||jt==0x141){stack_[sp_&0x0f]=pc_;sp_=uint8_t((sp_+1)&0x0f);pc_=uint16_t((jt==0x140?(target&~0x2000):(target|0x2000))&pcMask());return;}
        if(jumpCond(jt)){if((uint16_t((pc_-1)&pcMask())==target)&&(jt==0xbc||jt==0xbe))waitRqm_=true;pc_=target;}
    }
    void step(){if(program_.empty())return;const uint32_t op=program_[pc_&pcMask()]&0xffffff;pc_=uint16_t((pc_+1)&pcMask());switch(op&0xc00000){case 0x000000:execOp(op);break;case 0x400000:execOp(op);sp_=uint8_t((sp_-1)&0x0f);pc_=uint16_t(stack_[sp_]&pcMask());break;case 0x800000:jump(op);break;default:load(uint8_t(op&15),uint16_t(op>>6));break;}const int32_t mul=int32_t(int16_t(k_))*int32_t(int16_t(l_));m_=uint16_t(uint32_t(mul)>>15);n_=uint16_t(uint32_t(mul)<<1);}
    NecDspType type_; DSP1Map map_; std::vector<uint8_t> programBytes_; std::vector<uint32_t> program_; std::vector<uint16_t> dataRom_,ram_,stack_; uint32_t masterHz_=0,frequencyHz_=0;uint64_t frac_=0;std::vector<uint8_t>* extRam_=nullptr;bool* dirty_=nullptr;
    uint16_t a_=0,b_=0,tr_=0,trb_=0,dp_=0,rp_=0,dr_=0,sr_=0,k_=0,l_=0,m_=0,n_=0,si_=0,so_=0,pc_=0;uint8_t sp_=0;Flags fa_{},fb_{};bool waitRqm_=false;
};
}
std::unique_ptr<CartridgeDevice> createNecDspDevice(NecDspType type,DSP1Map map,NecDspFirmware firmware,uint32_t masterClockHz,std::vector<uint8_t>* externalRam,bool* externalRamDirty){
    if(firmware.program.empty()||firmware.data.empty())return nullptr;
    return std::make_unique<NecDspDevice>(type,map,std::move(firmware),masterClockHz,externalRam,externalRamDirty);
}

#include "dsp2.hpp"
#include "state_io.hpp"
#include <algorithm>
#include <cstdint>
#include <vector>
namespace {
class DSP2Device final : public CartridgeDevice {
public:
    explicit DSP2Device(DSP1Map map):map_(map){reset();}
    void reset() override { waiting_=true; command_=0; expected_=0; transparent_=0; stage_=0; inLen_=outLen_=0; input_.clear(); output_.clear(); outPos_=0; drLatch_=0xff; holdDataRegister_=false; }
    bool implemented() const override { return true; }
    CartridgeSupportLevel supportLevel() const override { return CartridgeSupportLevel::PartialHLE; }
    uint32_t stateTag() const override{return 0x32505344u;}
    std::vector<uint8_t> saveState() const override{stateio::Writer w;uint32_t v=2;w.pod(v);w.pod(map_);w.pod(waiting_);w.pod(command_);w.pod(transparent_);w.pod(stage_);w.pod(inLen_);w.pod(outLen_);w.pod(expected_);w.pod(outPos_);w.pod(drLatch_);w.pod(holdDataRegister_);w.vector(input_);w.vector(output_);return w.take();}
    bool loadState(const std::vector<uint8_t>&b)override{stateio::Reader r(b);uint32_t v=0;if(!r.pod(v)||(v!=1&&v!=2)||!r.pod(map_)||!r.pod(waiting_)||!r.pod(command_)||!r.pod(transparent_)||!r.pod(stage_)||!r.pod(inLen_)||!r.pod(outLen_)||!r.pod(expected_)||!r.pod(outPos_))return false;if(v>=2){if(!r.pod(drLatch_)||!r.pod(holdDataRegister_))return false;}else{drLatch_=0xff;holdDataRegister_=false;}if(!r.vector(input_)||!r.vector(output_)||!r.done())return false;return outPos_<=output_.size();}
    bool read(uint32_t a,uint8_t&v) override { if(!owns(a))return false; if(outPos_<output_.size())v=output_[outPos_++];else v=holdDataRegister_?drLatch_:0xff; if(outPos_==output_.size()&&!output_.empty()){output_.clear();outPos_=0;} return true; }
    bool write(uint32_t a,uint8_t v) override { if(!owns(a))return false; drLatch_=v; holdDataRegister_=false; if(waiting_){begin(v);return true;} input_.push_back(v); if(input_.size()>=expected_) completeStage(); return true; }
private:
    bool owns(uint32_t address) const { const uint8_t b=static_cast<uint8_t>(address >> 16); const uint16_t a=static_cast<uint16_t>(address); switch(map_){case DSP1Map::LoROMSmall:return ((b>=0x30&&b<=0x3f)||(b>=0xb0&&b<=0xbf))&&a>=0x8000; case DSP1Map::LoROMLarge:return ((b>=0x60&&b<=0x6f)||(b>=0xe0&&b<=0xef))&&a<0x8000; case DSP1Map::HiROM:return ((b<=0x1f)||(b>=0x80&&b<=0x9f))&&a>=0x6000&&a<0x8000; case DSP1Map::DSP2LoROM:return ((b>=0x20&&b<=0x3f)||(b>=0xa0&&b<=0xbf))&&((a>=0x6000&&a<0x7000)||a>=0x8000);}return false; }
    void begin(uint8_t c){command_=c;stage_=0;input_.clear();output_.clear();outPos_=0;switch(c){case 0x01:expected_=32;waiting_=false;break;case 0x03:expected_=1;waiting_=false;break;case 0x05:case 0x06:expected_=1;waiting_=false;break;case 0x09:expected_=4;waiting_=false;break;case 0x0d:expected_=2;waiting_=false;break;default:waiting_=true;expected_=0;break;}}
    void completeStage(){
        if(command_==0x05&&stage_==0){inLen_=input_[0];input_.clear();expected_=size_t(inLen_)*2;stage_=1;if(expected_==0){waiting_=true;holdDataRegister_=true;}return;}
        if(command_==0x06&&stage_==0){inLen_=input_[0];input_.clear();expected_=inLen_;stage_=1;if(expected_==0)waiting_=true;return;}
        if(command_==0x0d&&stage_==0){inLen_=input_[0];outLen_=input_[1];input_.clear();expected_=(size_t(inLen_)+1)/2;stage_=1;if(expected_==0){execute();waiting_=true;}return;}
        execute();waiting_=true;
    }
    void execute(){output_.clear();outPos_=0;switch(command_){
        case 0x01:{ output_.assign(32,0); for(unsigned y=0;y<8;y++){uint8_t px[8];for(unsigned j=0;j<4;j++){uint8_t c=input_[y*4+j];px[j*2]=c>>4;px[j*2+1]=c&15;}for(unsigned p=0;p<4;p++){uint8_t q=0;for(unsigned x=0;x<8;x++)q|=((px[x]>>p)&1u)<<(7-x); if(p<2)output_[y*2+p]=q;else output_[16+y*2+(p-2)]=q;}}break;}
        case 0x03:transparent_=input_[0]&15;break;
        case 0x05:{output_.resize(inLen_);for(unsigned i=0;i<inLen_;i++){uint8_t a=input_[i],b=input_[inLen_+i];uint8_t hi=(b>>4)==transparent_?(a&0xf0):(b&0xf0);uint8_t lo=(b&15)==transparent_?(a&15):(b&15);output_[i]=hi|lo;}break;}
        case 0x06:{output_.resize(inLen_);for(unsigned i=0;i<inLen_;i++){uint8_t c=input_[i];output_[inLen_-1-i]=uint8_t((c<<4)|(c>>4));}break;}
        case 0x09:{uint32_t a=input_[0]|(uint32_t(input_[1])<<8),b=input_[2]|(uint32_t(input_[3])<<8),v=a*b;for(int i=0;i<4;i++)output_.push_back(static_cast<uint8_t>(v >> (8*i)));break;}
        case 0x0d:{output_.assign(outLen_,0);if(outLen_==0||input_.empty())break;std::vector<uint8_t> pix(outLen_*2);uint32_t mul=inLen_<=outLen_?0x10000u:(uint32_t(inLen_)<<17)/((uint32_t(outLen_)<<1)+1);uint32_t pos=0;for(size_t i=0;i<pix.size();i++){unsigned j=pos>>16;unsigned byte=j>>1;if (byte >= input_.size()) byte = static_cast<unsigned>(input_.size() - 1);pix[i]=(j&1)?(input_[byte]&15):(input_[byte]>>4);pos+=mul;}for(unsigned i=0;i<outLen_;i++)output_[i]=uint8_t((pix[i*2]<<4)|pix[i*2+1]);break;}
        default:break;}}
    DSP1Map map_; bool waiting_=true;uint8_t command_=0,transparent_=0,stage_=0,inLen_=0,outLen_=0;size_t expected_=0,outPos_=0;uint8_t drLatch_=0xff;bool holdDataRegister_=false;std::vector<uint8_t> input_,output_;
};}
std::unique_ptr<CartridgeDevice> createDSP2Device(DSP1Map map){return std::make_unique<DSP2Device>(map);}

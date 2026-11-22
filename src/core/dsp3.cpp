#include "dsp3.hpp"
#include "state_io.hpp"
#include <array>
#include <cstdint>
#include <vector>
#include <queue>
#include <limits>
#include <cstdlib>
namespace {
class DSP3Device final : public CartridgeDevice {
public:
    DSP3Device(){ reset(); }
    bool implemented() const override { return true; }
    CartridgeSupportLevel supportLevel() const override { return CartridgeSupportLevel::PartialHLE; }
    uint32_t stateTag() const override { return 0x33505344u; }
    std::vector<uint8_t> saveState() const override {
        stateio::Writer w; uint32_t ver=1; w.pod(ver);
        w.pod(waitingCommand_);w.pod(wordHalf_);w.pod(command_);w.pod(wordLatch_);w.pod(winW_);w.pod(winH_);w.pod(originX_);w.pod(originY_);w.pod(pathMaxSearch_);w.pod(pathMaxOutput_);w.pod(expectedWords_);w.pod(outPos_);
        w.vector(in_);w.vector(out_);w.pod(decStage_);w.pod(codewords_);w.pod(outwords_);w.pod(symbol_);w.pod(baseCode_);w.pod(bitBuf_);w.pod(bitCount_);w.pod(codeIndex_);w.pod(baseIndex_);w.pod(baseLength_);w.pod(baseCodes_);w.pod(lzStage_);w.pod(lzLength_);w.array(codes_);w.array(codeLengths_);w.array(codeOffsets_);
        w.pod(pathStage_);w.array(terrain_);w.array(cost_);w.array(weight_);w.vector(pathCoords_);w.pod(pathIndex_); return w.take();
    }
    bool loadState(const std::vector<uint8_t>& b) override {
        stateio::Reader r(b);uint32_t ver=0;if(!r.pod(ver)||ver!=1)return false;
        if(!r.pod(waitingCommand_)||!r.pod(wordHalf_)||!r.pod(command_)||!r.pod(wordLatch_)||!r.pod(winW_)||!r.pod(winH_)||!r.pod(originX_)||!r.pod(originY_)||!r.pod(pathMaxSearch_)||!r.pod(pathMaxOutput_)||!r.pod(expectedWords_)||!r.pod(outPos_))return false;
        if(!r.vector(in_)||!r.vector(out_)||!r.pod(decStage_)||!r.pod(codewords_)||!r.pod(outwords_)||!r.pod(symbol_)||!r.pod(baseCode_)||!r.pod(bitBuf_)||!r.pod(bitCount_)||!r.pod(codeIndex_)||!r.pod(baseIndex_)||!r.pod(baseLength_)||!r.pod(baseCodes_)||!r.pod(lzStage_)||!r.pod(lzLength_)||!r.array(codes_)||!r.array(codeLengths_)||!r.array(codeOffsets_))return false;
        if(!r.pod(pathStage_)||!r.array(terrain_)||!r.array(cost_)||!r.array(weight_)||!r.vector(pathCoords_)||!r.pod(pathIndex_)||!r.done())return false;
        return outPos_<=out_.size()&&pathIndex_<=pathCoords_.size()&&codeIndex_<=codes_.size()&&baseIndex_<=codeLengths_.size();
    }
    void reset() override {
        waitingCommand_=true; command_=0; in_.clear(); out_.clear(); outPos_=0;
        wordHalf_=false; wordLatch_=0; winW_=0; winH_=0; expectedWords_=0;
        originX_=originY_=pathMaxSearch_=pathMaxOutput_=0;
        terrain_.fill(0xff);cost_.fill(0xff);weight_.fill(0xff);pathStage_=PathStage::Idle;pathCoords_.clear();pathIndex_=0;
        decoderReset();
    }
    bool read(uint32_t a,uint8_t& v) override {
        if(!owns(a)) return false;
        if(isStatus(a)){ v=0x80; return true; }
        if(outPos_<out_.size()) {
            v=out_[outPos_++];
            if(outPos_==out_.size()){out_.clear();outPos_=0; if(decStage_==DecStage::Data) decoderProcess();}
        } else v=0x80;
        return true;
    }
    bool write(uint32_t a,uint8_t v) override {
        if(!owns(a)) return false;
        if(isStatus(a)) return true;
        if(waitingCommand_) {
            command_=v; in_.clear(); out_.clear(); outPos_=0; wordHalf_=false;
            if(command_==0x38){decoderReset();decStage_=DecStage::Codewords;waitingCommand_=false;expectedWords_=1;return true;}
            expectedWords_=wordsFor(command_);
            if(expectedWords_==0) execute(); else waitingCommand_=false;
            return true;
        }
        if(!wordHalf_){wordLatch_=v;wordHalf_=true;return true;}
        uint16_t w=uint16_t(wordLatch_) | (uint16_t(v)<<8); wordHalf_=false;
        if(decStage_!=DecStage::Idle){decoderFeed(w);return true;}
        if(pathStage_!=PathStage::Idle){pathFeed(w);return true;}
        in_.push_back(w);
        if(in_.size()>=expectedWords_) execute();
        return true;
    }
private:
    enum class DecStage:uint8_t{Idle,Codewords,Outwords,Symbols,Tree,Data};
    enum class PathStage:uint8_t{Idle,Terrain,Cost,WaitOutputRange};
    bool owns(uint32_t address) const {
        uint8_t b=uint8_t(address>>16); uint16_t a=uint16_t(address);
        return ((b>=0x20&&b<=0x3f)||(b>=0xa0&&b<=0xbf)) && a>=0x8000;
    }
    static bool isStatus(uint32_t address) { return uint16_t(address)>=0xc000; }
    static size_t wordsFor(uint8_t c){
        switch(c){
            case 0x03:return 1; case 0x06:return 1; case 0x07:return 2; case 0x0c:return 1;
            case 0x10:return 1; case 0x18:return 5; case 0x1c:return 2; case 0x1e:return 1;
            case 0x3e:return 1; default:return 0;
        }
    }
    void out16(uint16_t w){out_.push_back(uint8_t(w));out_.push_back(uint8_t(w>>8));}
    static int wrap(int v,int n){if(n<=0)return 0;v%=n;if(v<0)v+=n;return v;}
    void decoderReset(){
        decStage_=DecStage::Idle; codewords_=outwords_=0; bitBuf_=0;bitCount_=0;symbol_=0;
        codeIndex_=0;baseIndex_=0;baseLength_=0;baseCodes_=0;baseCode_=0xffff;lzStage_=0;lzLength_=0;
        codes_.fill(0);codeLengths_.fill(0);codeOffsets_.fill(0);
    }
    void pushBits(uint16_t w){bitBuf_=(bitBuf_<<16)|w;bitCount_+=16;}
    bool peekBits(unsigned n,uint32_t&v)const{if(n>bitCount_)return false;v=uint32_t((bitBuf_>>(bitCount_-n))&((uint64_t(1)<<n)-1));return true;}
    bool getBits(unsigned n,uint32_t&v){if(!peekBits(n,v))return false;bitCount_-=n;if(bitCount_==0)bitBuf_=0;else bitBuf_&=((uint64_t(1)<<bitCount_)-1);return true;}
    void decoderFeed(uint16_t w){
        if(decStage_==DecStage::Codewords){codewords_=w;decStage_=DecStage::Outwords;return;}
        if(decStage_==DecStage::Outwords){outwords_=w;decStage_=DecStage::Symbols;return;}
        pushBits(w);decoderProcess();
    }
    void decoderFinish(){decStage_=DecStage::Idle;waitingCommand_=true;expectedWords_=0;wordHalf_=false;}
    void decoderProcess(){
        if(outPos_<out_.size())return;
        for(;;){
            if(decStage_==DecStage::Symbols){
                while(codewords_){
                    uint32_t cmd=0;if(!peekBits(2,cmd))return;
                    unsigned extra=cmd==0?9:cmd==2?1:cmd==3?4:0;
                    if(bitCount_<2+extra)return;
                    getBits(2,cmd); uint32_t x=0; if(extra)getBits(extra,x);
                    if(cmd==0)symbol_=uint16_t(x);else if(cmd==1)++symbol_;else if(cmd==2)symbol_=uint16_t(symbol_+2+x);else symbol_=uint16_t(symbol_+4+x);
                    if(codeIndex_<codes_.size()) codes_[codeIndex_++]=symbol_;
                    --codewords_;
                }
                codeIndex_=0;symbol_=0;baseCodes_=0;baseIndex_=0;decStage_=DecStage::Tree;continue;
            }
            if(decStage_==DecStage::Tree){
                if(baseCodes_==0){uint32_t b;if(!getBits(1,b))return;baseLength_=b?3:2;baseCodes_=b?8:4;baseIndex_=0;symbol_=0;}
                while(baseCodes_){uint32_t q;if(!getBits(3,q))return;unsigned len=unsigned(q)+1;if(baseIndex_<codeLengths_.size()){codeLengths_[baseIndex_]=uint8_t(len);codeOffsets_[baseIndex_]=symbol_;}++baseIndex_;symbol_=uint16_t(symbol_+(1u<<len));--baseCodes_;}
                baseCode_=0xffff;lzStage_=0;decStage_=DecStage::Data;continue;
            }
            if(decStage_==DecStage::Data){
                if(outwords_==0){decoderFinish();return;}
                if(lzStage_==1){uint32_t b;if(!getBits(1,b))return;lzLength_=b?12:8;lzStage_=2;}
                if(lzStage_==2){uint32_t v;if(!getBits(lzLength_,v))return;lzStage_=0;--outwords_;out16(uint16_t(v));if(outwords_==0)decoderFinish();return;}
                if(baseCode_==0xffff){uint32_t b;if(!getBits(baseLength_,b))return;baseCode_=uint16_t(b);if(baseCode_>=codeLengths_.size()){decoderFinish();return;}}
                uint32_t suffix=0;unsigned n=codeLengths_[baseCode_];if(!getBits(n,suffix))return;
                size_t idx=size_t(codeOffsets_[baseCode_])+suffix;baseCode_=0xffff;if(idx>=codes_.size()){decoderFinish();return;}
                uint16_t sym=codes_[idx];
                if(sym&0xff00){symbol_=uint16_t(sym+0x7f02);(void)symbol_;lzStage_=1;continue;}
                --outwords_;out16(sym);if(outwords_==0)decoderFinish();return;
            }
            return;
        }
    }
    int cellIndex(int x,int y) const {return (winW_&&winH_)?(wrap(y,winH_)*winW_+wrap(x,winW_))&0x1ff:0;}
    int hexDistance(int x0,int y0,int x1,int y1) const {
        auto cube=[](int x,int y){int z=y-(x-(x&1))/2;int yy=-x-z;return std::array<int,3>{x,yy,z};};
        auto a=cube(x0,y0),b=cube(x1,y1);return (std::abs(a[0]-b[0])+std::abs(a[1]-b[1])+std::abs(a[2]-b[2]))/2;
    }
    void moveHex(int& x,int& y,unsigned d) const {
        static const int dx[6]={0,1,1,0,-1,-1};
        static const int dyEven[6]={-1,-1,0,1,0,-1};
        static const int dyOdd [6]={-1,0,1,1,1,0};
        d%=6;const int oldx=x;x=wrap(x+dx[d],winW_);y=wrap(y+((oldx&1)?dyOdd[d]:dyEven[d]),winH_);
    }
    void buildRingCoords(uint8_t minR,uint8_t maxR){
        pathCoords_.clear();pathIndex_=0;if(!winW_||!winH_)return;
        for(unsigned turn=0;turn<6;turn++)for(unsigned r=minR;r<=maxR;r++){
            int x=originX_,y=originY_;for(unsigned i=0;i<r;i++)moveHex(x,y,turn);
            for(unsigned i=0;i<r;i++){pathCoords_.push_back(uint16_t(uint8_t(x))|(uint16_t(uint8_t(y))<<8));moveHex(x,y,turn+2);}
        }
    }
    void emitPathCell(){out_.clear();outPos_=0;if(pathIndex_>=pathCoords_.size()){computeWeights();out16(0xffff);pathStage_=PathStage::WaitOutputRange;return;}out16(pathCoords_[pathIndex_]);pathStage_=PathStage::Terrain;}
    void computeWeights(){
        weight_.fill(0xff);if(!winW_||!winH_)return;int start=cellIndex(originX_,originY_);weight_[start]=0;
        using Q=std::pair<int,int>;std::priority_queue<Q,std::vector<Q>,std::greater<Q>>q;q.push({0,start});
        static const int dx[6]={0,1,1,0,-1,-1};static const int dye[6]={-1,-1,0,1,0,-1};static const int dyo[6]={-1,0,1,1,1,0};
        while(!q.empty()){auto [d,c]=q.top();q.pop();if(d!=weight_[c])continue;int x=c%winW_,y=c/winW_;for(int k=0;k<6;k++){int nx=wrap(x+dx[k],winW_),ny=wrap(y+((x&1)?dyo[k]:dye[k]),winH_),n=cellIndex(nx,ny);if((terrain_[n]&1)||terrain_[n]>=0x80||cost_[n]>=0x80)continue;int nd=d+cost_[n];if(nd<weight_[n]&&nd<255){weight_[n]=uint8_t(nd);q.push({nd,n});}}}
    }
    void beginPath(uint16_t rr){uint8_t minR=uint8_t(rr),maxR=uint8_t(rr>>8);if(minR==0)minR=1;if(pathMaxSearch_>=minR)minR=uint8_t(pathMaxSearch_+1);if(maxR>pathMaxSearch_)pathMaxSearch_=maxR;buildRingCoords(minR,maxR);emitPathCell();}
    void pathFeed(uint16_t w){
        if(pathStage_==PathStage::Terrain){int x=uint8_t(pathCoords_[pathIndex_]),y=uint8_t(pathCoords_[pathIndex_]>>8);terrain_[cellIndex(x,y)]=uint8_t(w);pathStage_=PathStage::Cost;return;}
        if(pathStage_==PathStage::Cost){int x=uint8_t(pathCoords_[pathIndex_]),y=uint8_t(pathCoords_[pathIndex_]>>8);cost_[cellIndex(x,y)]=uint8_t(w);++pathIndex_;emitPathCell();return;}
        if(pathStage_==PathStage::WaitOutputRange){uint8_t minR=uint8_t(w),maxR=uint8_t(w>>8);if(minR==0)minR=1;if(pathMaxOutput_>=minR)minR=uint8_t(pathMaxOutput_+1);if(maxR>pathMaxOutput_)pathMaxOutput_=maxR;buildRingCoords(minR,maxR);out_.clear();outPos_=0;for(uint16_t xy:pathCoords_){int x=uint8_t(xy),y=uint8_t(xy>>8);out16(xy);out16(weight_[cellIndex(x,y)]);}out16(0xffff);pathStage_=PathStage::Idle;waitingCommand_=true;wordHalf_=false;return;}
    }
    void execute(){
        out_.clear();outPos_=0;
        switch(command_){
            case 0x06:winW_=uint8_t(in_[0]);winH_=uint8_t(in_[0]>>8);break;
            case 0x03:{uint8_t x=uint8_t(in_[0]),y=uint8_t(in_[0]>>8);out16(uint16_t(uint16_t(winW_)*y+x));break;}
            case 0x07:{
                unsigned d=in_[0]&7;int x=uint8_t(in_[1]),y=uint8_t(in_[1]>>8);
                static const int dx[8]={0,1,1,0,-1,-1,0,0};static const int dyEven[8]={-1,-1,0,1,0,-1,0,0};static const int dyOdd[8]={-1,0,1,1,1,0,0,0};
                int oldx=x;x=wrap(x+dx[d],winW_);y=wrap(y+((oldx&1)?dyOdd[d]:dyEven[d]),winH_);
                uint16_t packed=uint16_t(uint8_t(x))|(uint16_t(uint8_t(y))<<8);out16(packed);out16(uint16_t(uint16_t(winW_)*uint8_t(y)+uint8_t(x)));break;
            }
            case 0x18:{
                std::array<uint8_t,8> bitmap{};for(int i=0;i<4;i++){bitmap[i*2]=uint8_t(in_[i+1]);bitmap[i*2+1]=uint8_t(in_[i+1]>>8);}std::array<uint8_t,8> plane{};
                for(int i=0;i<8;i++) for(int j=0;j<8;j++) plane[j]=uint8_t((plane[j]<<1)|((bitmap[i]>>j)&1));
                for(int i=0;i<8;i+=2) out16(uint16_t(plane[i])|(uint16_t(plane[i+1])<<8));
                break;
            }
            case 0x1c:out16(0);out16(0);break;
            case 0x3e:{originX_=uint8_t(in_[0]);originY_=uint8_t(in_[0]>>8);pathMaxSearch_=pathMaxOutput_=0;terrain_.fill(0xff);cost_.fill(0xff);weight_.fill(0xff);int c=cellIndex(originX_,originY_);terrain_[c]=0;cost_[c]=0xff;weight_[c]=0;break;}
            case 0x1e:{beginPath(in_[0]);waitingCommand_=false;wordHalf_=false;in_.clear();return;}
            case 0x0c:out16(0);break;
            case 0x10:{
                const uint16_t v=in_.empty()?0xffff:in_[0];
                in_.clear();out_.clear();outPos_=0;wordHalf_=false;
                if(v==0xffff){waitingCommand_=true;expectedWords_=0;}
                else {waitingCommand_=false;expectedWords_=1;}
                return;
            }
            default:break;
        }
        waitingCommand_=true;wordHalf_=false;in_.clear();
    }
    bool waitingCommand_=true,wordHalf_=false;uint8_t command_=0,wordLatch_=0,winW_=0,winH_=0,originX_=0,originY_=0,pathMaxSearch_=0,pathMaxOutput_=0;size_t expectedWords_=0,outPos_=0;
    std::vector<uint16_t> in_;std::vector<uint8_t> out_;
    DecStage decStage_=DecStage::Idle;uint16_t codewords_=0,outwords_=0,symbol_=0,baseCode_=0xffff;uint64_t bitBuf_=0;unsigned bitCount_=0;size_t codeIndex_=0,baseIndex_=0;uint8_t baseLength_=0,baseCodes_=0,lzStage_=0,lzLength_=0;
    std::array<uint16_t,512> codes_{};std::array<uint8_t,8> codeLengths_{};std::array<uint16_t,8> codeOffsets_{};
    PathStage pathStage_=PathStage::Idle;std::array<uint8_t,512> terrain_{},cost_{},weight_{};std::vector<uint16_t> pathCoords_;size_t pathIndex_=0;
};
}
std::unique_ptr<CartridgeDevice> createDSP3Device(){return std::make_unique<DSP3Device>();}

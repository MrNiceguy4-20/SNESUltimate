#include "dsp4.hpp"
#include "state_io.hpp"
#include <array>
#include <cstdint>
#include <vector>
namespace {
class DSP4Device final : public CartridgeDevice {
public:
    DSP4Device(){reset();}
    bool implemented() const override{return true;}
    CartridgeSupportLevel supportLevel() const override{return CartridgeSupportLevel::PartialHLE;}
    uint32_t stateTag() const override { return 0x34505344u; }
    std::vector<uint8_t> saveState() const override {
        stateio::Writer w;uint32_t ver=1;w.pod(ver);w.pod(waiting_);w.pod(cmdHalf_);w.pod(cmdLo_);w.pod(cmd_);w.pod(expected_);w.pod(outPos_);w.vector(in_);w.vector(out_);w.pod(stream_);w.pod(phase_);
        w.pod(oamRowMax_);w.pod(oamIndex_);w.pod(oamBits_);w.pod(spriteCount_);w.array(oamAttr_);w.array(oamRows_);
        w.pod(worldX_);w.pod(worldY_);w.pod(worldDx_);w.pod(worldDy_);w.pod(worldXenv_);w.pod(worldDdx_);w.pod(worldDdy_);w.pod(polyBottom_);w.pod(polyTop_);w.pod(polyCx0_);w.pod(polyCx1_);w.pod(polyPtr_);w.pod(polyRaster_);w.pod(viewportBottom_);w.pod(worldYofs_);w.pod(distance_);
        w.pod(viewX1_);w.pod(viewY1_);w.pod(viewXofs1_);w.pod(viewYofs1_);w.pod(viewX2_);w.pod(viewY2_);w.pod(viewXofs2_);w.pod(viewYofs2_);w.pod(viewTurnoffX_);w.pod(viewTurnoffDx_);w.pod(viewDx_);w.pod(viewDy_);w.pod(viewYofsenv_);w.pod(pendingSegments_);w.pod(lightCount_);
        w.array(polyClipRt_);w.array(polyClipLf_);w.array(polyCx_);w.array(polyPtr2_);w.array(polyBottom2_);w.array(polyTop2_);w.array(polyRaster2_);w.array(polyStart_);w.array(polyPlane_);
        w.pod(viewportCx_);w.pod(viewportCy_);w.pod(viewportLeft_);w.pod(viewportRight_);w.pod(viewportTop_);w.pod(spriteClipY_);w.pod(spriteX_);w.pod(spriteY_);w.pod(spriteAttr_);w.pod(raster_);w.pod(spriteSize_);return w.take();
    }
    bool loadState(const std::vector<uint8_t>& b) override {
        stateio::Reader r(b);uint32_t ver=0;if(!r.pod(ver)||ver!=1)return false;
#define RD(x) if(!r.pod(x))return false
        RD(waiting_);RD(cmdHalf_);RD(cmdLo_);RD(cmd_);RD(expected_);RD(outPos_);if(!r.vector(in_)||!r.vector(out_))return false;RD(stream_);RD(phase_);RD(oamRowMax_);RD(oamIndex_);RD(oamBits_);RD(spriteCount_);if(!r.array(oamAttr_)||!r.array(oamRows_))return false;
        RD(worldX_);RD(worldY_);RD(worldDx_);RD(worldDy_);RD(worldXenv_);RD(worldDdx_);RD(worldDdy_);RD(polyBottom_);RD(polyTop_);RD(polyCx0_);RD(polyCx1_);RD(polyPtr_);RD(polyRaster_);RD(viewportBottom_);RD(worldYofs_);RD(distance_);
        RD(viewX1_);RD(viewY1_);RD(viewXofs1_);RD(viewYofs1_);RD(viewX2_);RD(viewY2_);RD(viewXofs2_);RD(viewYofs2_);RD(viewTurnoffX_);RD(viewTurnoffDx_);RD(viewDx_);RD(viewDy_);RD(viewYofsenv_);RD(pendingSegments_);RD(lightCount_);
        if(!r.array(polyClipRt_)||!r.array(polyClipLf_)||!r.array(polyCx_)||!r.array(polyPtr2_)||!r.array(polyBottom2_)||!r.array(polyTop2_)||!r.array(polyRaster2_)||!r.array(polyStart_)||!r.array(polyPlane_))return false;
        RD(viewportCx_);RD(viewportCy_);RD(viewportLeft_);RD(viewportRight_);RD(viewportTop_);RD(spriteClipY_);RD(spriteX_);RD(spriteY_);RD(spriteAttr_);RD(raster_);RD(spriteSize_);
#undef RD
        return r.done()&&outPos_<=out_.size()&&oamIndex_<=128;
    }
    void reset() override{
        waiting_=true;cmdHalf_=false;cmd_=0;cmdLo_=0;in_.clear();out_.clear();outPos_=0;
        expected_=0; stream_=Stream::None; phase_=0;
        oamRowMax_=33;oamIndex_=oamBits_=spriteCount_=0;oamAttr_.fill(0);oamRows_.fill(0);
        worldX_=worldY_=worldDx_=worldDy_=worldXenv_=0;
        viewX1_=viewY1_=viewXofs1_=viewYofs1_=0; viewX2_=viewY2_=viewXofs2_=viewYofs2_=0;
        viewTurnoffX_=viewTurnoffDx_=0; viewDx_=viewDy_=0; viewYofsenv_=0;
        polyBottom_=polyTop_=polyCx0_=polyCx1_=polyPtr_=polyRaster_=viewportBottom_=worldYofs_=distance_=0;
        polyClipRt_.fill(0);polyClipLf_.fill(0);polyCx_.fill(0);polyPtr2_.fill(0);polyBottom2_.fill(0);polyTop2_.fill(0);polyRaster2_.fill(0);polyStart_.fill(0);polyPlane_.fill(0);
        viewportCx_=viewportCy_=viewportLeft_=viewportRight_=viewportTop_=spriteClipY_=spriteX_=spriteY_=spriteAttr_=raster_=0;spriteSize_=true;
    }
    bool read(uint32_t a,uint8_t&v) override{
        if(!owns(a))return false;
        if(outPos_<out_.size()){
            v=out_[outPos_++];
            if(outPos_==out_.size()){out_.clear();outPos_=0;}
        } else v=0xff;
        return true;
    }
    bool write(uint32_t a,uint8_t v) override{
        if(!owns(a))return false;
        if(waiting_){
            if(!cmdHalf_){cmdLo_=v;cmdHalf_=true;return true;}
            cmd_=uint16_t(cmdLo_)|(uint16_t(v)<<8);cmdHalf_=false;in_.clear();out_.clear();outPos_=0;
            expected_=inputBytes(cmd_);stream_=Stream::None;phase_=0;
            if(expected_==0)executeFixed();else waiting_=false;
            return true;
        }
        in_.push_back(v);
        if(in_.size()>=expected_){
            if(stream_==Stream::None) executeInitial(); else executeStreamPacket();
        }
        return true;
    }
private:
    enum class Stream:uint8_t{None,Op01,Op07,Op08,Op09,Op0D,Op0F,Op10};
    bool owns(uint32_t address)const{uint8_t b=uint8_t(address>>16);uint16_t a=uint16_t(address);return ((((b<=0x1f)||(b>=0x80&&b<=0x9f))&&a>=0x6000&&a<0x8000)||(((b>=0x30&&b<=0x3f)||(b>=0xb0&&b<=0xbf))&&a>=0x8000));}
    static size_t inputBytes(uint16_t c){switch(c){case 0x0000:return 4;case 0x0001:return 44;case 0x0003:return 0;case 0x0005:return 0;case 0x0006:return 0;case 0x0007:return 34;case 0x0008:return 90;case 0x0009:return 14;case 0x000a:return 6;case 0x000b:return 6;case 0x000d:return 42;case 0x000e:return 0;case 0x000f:return 46;case 0x0010:return 36;case 0x0011:return 8;default:return 0;}}
    int16_t w(size_t p)const{return int16_t(uint16_t(in_[p])|(uint16_t(in_[p+1])<<8));}
    int32_t d(size_t p)const{return int32_t(uint32_t(uint16_t(w(p)))|(uint32_t(uint16_t(w(p+2)))<<16));}
    void o16(int32_t x){uint16_t v=uint16_t(x);out_.push_back(uint8_t(v));out_.push_back(uint8_t(v>>8));}
    static int16_t inverse(int v){
        static constexpr uint16_t lut[64]={0x0000,0x8000,0x4000,0x2aaa,0x2000,0x1999,0x1555,0x1249,0x1000,0x0e38,0x0ccc,0x0ba2,0x0aaa,0x09d8,0x0924,0x0888,0x0800,0x0787,0x071c,0x06bc,0x0666,0x0618,0x05d1,0x0590,0x0555,0x051e,0x04ec,0x04bd,0x0492,0x0469,0x0444,0x0421,0x0400,0x03e0,0x03c3,0x03a8,0x038e,0x0375,0x035e,0x0348,0x0333,0x031f,0x030c,0x02fa,0x02e8,0x02d8,0x02c8,0x02b9,0x02aa,0x029c,0x028f,0x0282,0x0276,0x026a,0x025e,0x0253,0x0249,0x023e,0x0234,0x022b,0x0222,0x0219,0x0210,0x0208};
        if(v<0)v=0;
        if(v>63)v=63;
        return int16_t(lut[v]);
    }
    void setOam(int16_t x,int16_t y,int16_t attr,bool size,bool stop){
        bool draw=true;const int row1=(y>>3)&31,row2=(row1+1)&31;
        if(!((y<0)||((y&0x01ff)<0x00eb)))draw=false;
        if(size){if(oamRows_[row1]+1>=oamRowMax_||oamRows_[row2]+1>=oamRowMax_)draw=false;}else if(oamRows_[row1]>=oamRowMax_)draw=false;
        if(spriteCount_>=128)draw=false;
        if(draw){
            if(size){oamRows_[row1]=uint8_t(oamRows_[row1]+2);oamRows_[row2]=uint8_t(oamRows_[row2]+2);}else ++oamRows_[row1];
            o16(1);out_.push_back(uint8_t(x));out_.push_back(uint8_t(y));o16(attr);++spriteCount_;
            if(oamIndex_<oamAttr_.size()){
                if(x<0||x>255) oamAttr_[oamIndex_]|=uint16_t(1u<<oamBits_);
                ++oamBits_;
                if(size) oamAttr_[oamIndex_]|=uint16_t(1u<<oamBits_);
                ++oamBits_;
                if(oamBits_==16){oamBits_=0;++oamIndex_;}
            }
        }else if(stop)o16(0);
    }
    void finishCommand(){waiting_=true;stream_=Stream::None;phase_=0;expected_=0;in_.clear();}
    void arm(Stream s,size_t bytes,int phase){stream_=s;expected_=bytes;phase_=phase;in_.clear();}
    void project01(){
        out_.clear();outPos_=0;
        viewX2_=int16_t(((((worldX_+worldXenv_)>>16)*distance_)>>15)+((viewTurnoffX_*distance_)>>15));
        viewY2_=int16_t(((worldY_>>16)*distance_)>>15);viewXofs2_=viewX2_;
        viewYofs2_=int16_t(((int32_t(worldYofs_)*distance_)>>15)+polyBottom_-viewY2_);
        o16((worldX_+worldXenv_)>>16);o16(viewX2_);o16(worldY_>>16);o16(viewY2_);
        int seg=polyRaster_-viewY2_;if(viewY2_>=polyRaster_)seg=0;else polyRaster_=viewY2_;
        if(viewY2_<polyTop_){seg=0;if(viewY1_>=polyTop_)seg=viewY1_-polyTop_;}
        if(seg<0)seg=0;
        o16(seg);
        if(seg){
            int32_t px=(int32_t(viewXofs2_-viewXofs1_)*inverse(seg))<<1;
            int32_t py=(int32_t(viewYofs2_-viewYofs1_)*inverse(seg))<<1;
            int32_t xs=(int32_t(int16_t(polyCx0_+viewXofs1_))<<16);
            int32_t ys=(int32_t(int16_t(-viewportBottom_+viewYofs1_+viewYofsenv_+polyCx1_-worldYofs_))<<16);
            for(int i=0;i<seg;i++){o16(polyPtr_);o16((ys+0x8000)>>16);o16((xs+0x8000)>>16);polyPtr_=int16_t(polyPtr_-4);xs+=px;ys+=py;}
        }
        viewX1_=viewX2_;viewY1_=viewY2_;viewXofs1_=viewXofs2_;viewYofs1_=viewYofs2_;
        worldDx_+=(int32_t(int16_t(worldDdx_))<<8);worldDy_+=(int32_t(int16_t(worldDdy_))<<8);worldX_+=worldDx_+worldXenv_;worldY_+=worldDy_;viewTurnoffX_=int16_t(viewTurnoffX_+viewTurnoffDx_);
        arm(Stream::Op01,2,1);
    }
    void project07(){
        out_.clear();outPos_=0;viewX2_=int16_t(viewX2_+viewDx_);viewY2_=int16_t(viewY2_+viewDy_);viewXofs2_=viewX2_;
        viewYofs2_=int16_t(((int32_t(worldYofs_)*distance_)>>15)+polyBottom_-viewY2_);o16(viewX2_);o16(viewY2_);
        int seg=viewY1_-viewY2_;if(viewY2_>=polyRaster_)seg=0;else polyRaster_=viewY2_;
        if(viewY2_<polyTop_){seg=0;if(viewY1_>=polyTop_)seg=viewY1_-polyTop_;}if(seg<0)seg=0;
        o16(seg);
        if(seg){int32_t px=(int32_t(viewXofs2_-viewXofs1_)*inverse(seg))<<1;int32_t py=(int32_t(viewYofs2_-viewYofs1_)*inverse(seg))<<1;int32_t xs=(int32_t(int16_t(polyCx0_+viewXofs1_))<<16);int32_t ys=(int32_t(int16_t(-viewportBottom_+viewYofs1_+viewYofsenv_+polyCx1_-worldYofs_))<<16);for(int i=0;i<seg;i++){o16(polyPtr_);o16((ys+0x8000)>>16);o16((xs+0x8000)>>16);polyPtr_=int16_t(polyPtr_-4);xs+=px;ys+=py;}}
        viewX1_=viewX2_;viewY1_=viewY2_;viewXofs1_=viewXofs2_;viewYofs1_=viewYofs2_;arm(Stream::Op07,2,1);
    }
    void project0D(){
        out_.clear();outPos_=0;
        viewX2_=int16_t((((worldX_+worldXenv_)>>16)*distance_)>>15);
        viewY2_=int16_t(((worldY_>>16)*distance_)>>15);viewXofs2_=viewX2_;
        viewYofs2_=int16_t(((int32_t(worldYofs_)*distance_)>>15)+polyBottom_-viewY2_);
        o16((worldX_+worldXenv_)>>16);o16(viewX2_);o16(worldY_>>16);o16(viewY2_);
        int seg=viewY1_-viewY2_;if(viewY2_>=polyRaster_)seg=0;else polyRaster_=viewY2_;
        if(viewY2_<polyTop_){seg=0;if(viewY1_>=polyTop_)seg=viewY1_-polyTop_;}if(seg<0)seg=0;
        o16(seg);
        if(seg){
            int32_t px=(int32_t(viewXofs2_-viewXofs1_)*inverse(seg))<<1;
            int32_t py=(int32_t(viewYofs2_-viewYofs1_)*inverse(seg))<<1;
            int32_t xs=(int32_t(int16_t(polyCx0_+viewXofs1_))<<16);
            int32_t ys=(int32_t(int16_t(-viewportBottom_+viewYofs1_+viewYofsenv_+polyCx1_-worldYofs_))<<16);
            for(int i=0;i<seg;i++){o16(polyPtr_);o16((ys+0x8000)>>16);o16((xs+0x8000)>>16);polyPtr_=int16_t(polyPtr_-4);xs+=px;ys+=py;}
        }
        viewX1_=viewX2_;viewY1_=viewY2_;viewXofs1_=viewXofs2_;viewYofs1_=viewYofs2_;
        worldDx_+=(int32_t(int16_t(worldDdx_))<<8);worldDy_+=(int32_t(int16_t(worldDdy_))<<8);worldX_+=worldDx_+worldXenv_;worldY_+=worldDy_;
        arm(Stream::Op0D,2,1);
    }
    static uint16_t lightColor(int16_t dist,uint16_t color){
        int r=color&31,g=(color>>5)&31,b=(color>>10)&31;
        r=(r*int32_t(dist)>>15)&31;g=(g*int32_t(dist)>>15)&31;b=(b*int32_t(dist)>>15)&31;
        return uint16_t(r|(g<<5)|(b<<10));
    }
    void finishProject10(){
        if(pendingSegments_){
            int32_t px=(int32_t(viewXofs2_-viewXofs1_)*inverse(pendingSegments_))<<1;
            int32_t py=(int32_t(viewYofs2_-viewYofs1_)*inverse(pendingSegments_))<<1;
            int32_t xs=(int32_t(int16_t(polyCx0_+viewXofs1_))<<16);
            int32_t ys=(int32_t(int16_t(-viewportBottom_+viewYofs1_+viewYofsenv_+polyCx1_-worldYofs_))<<16);
            for(int i=0;i<pendingSegments_;i++){o16(polyPtr_);o16((ys+0x8000)>>16);o16((xs+0x8000)>>16);polyPtr_=int16_t(polyPtr_-4);xs+=px;ys+=py;}
        }
        viewX1_=viewX2_;viewY1_=viewY2_;viewXofs1_=viewXofs2_;viewYofs1_=viewYofs2_;pendingSegments_=0;lightCount_=0;arm(Stream::Op10,2,1);
    }
    void project10(){
        out_.clear();outPos_=0;viewX2_=int16_t(viewX2_+viewDx_);viewY2_=int16_t(viewY2_+viewDy_);viewXofs2_=viewX2_;
        viewYofs2_=int16_t(((int32_t(worldYofs_)*distance_)>>15)+polyBottom_-viewY2_);o16(viewX2_);o16(viewY2_);
        int seg=viewY1_-viewY2_;if(viewY2_>=polyRaster_)seg=0;else polyRaster_=viewY2_;if(viewY2_<polyTop_){seg=0;if(viewY1_>=polyTop_)seg=viewY1_-polyTop_;}if(seg<0)seg=0;
        o16(seg);pendingSegments_=seg;
        if(seg){lightCount_=0;arm(Stream::Op10,4,10);}else finishProject10();
    }
    static int16_t clamp16(int32_t v,int16_t lo,int16_t hi){return int16_t(v<lo?lo:(v>hi?hi:v));}
    void outputPolygon08(const std::array<int16_t,4>& vx,const std::array<int16_t,4>& env){
        out_.clear();outPos_=0;
        for(int polygon=0;polygon<2;polygon++){
            int poly=polygon;if(uint16_t(env[polygon*2])==0xc001||env[polygon*2+1]==0x3fff)poly=1;
            int seg=int(polyRaster2_[polygon*2])-int(vx[polygon*2+1]);
            if(seg>0){polyRaster2_[polygon*2]=vx[polygon*2+1];polyRaster2_[polygon*2+1]=vx[polygon*2+1];}else seg=0;
            if(vx[polygon*2+1]<polyTop2_[polygon*2])seg=0;
            if(seg>63)seg=63;
            o16(seg);
            if(!seg){polyStart_[polygon]=vx[poly*2];continue;}
            int32_t eL0=int32_t(env[polygon*2])*polyPlane_[poly]>>15,eL1=int32_t(env[polygon*2])*distance_>>15;
            int32_t eR0=int32_t(env[polygon*2+1])*polyPlane_[poly]>>15,eR1=int32_t(env[polygon*2+1])*distance_>>15;
            int32_t li=(int32_t(polyStart_[poly]+eL1)-int32_t(vx[poly*2]+eL0))*inverse(seg)<<1;
            int32_t ri=(int32_t(polyStart_[poly]+eR1)-int32_t(vx[poly*2]+eR0))*inverse(seg)<<1;
            if(seg==1){li=-li;ri=-ri;}
            int32_t wl=(int32_t(polyCx_[polygon*2]-polyStart_[poly]+eL0)<<16);
            int32_t wr=(int32_t(polyCx_[polygon*2+1]-polyStart_[poly]+eR0)<<16);
            polyPlane_[polygon]=distance_;
            for(int i=0;i<seg;i++){
                wl+=li;wr+=ri;int16_t xl=clamp16((wl+0x8000)>>16,polyClipLf_[polygon*2],polyClipRt_[polygon*2]);int16_t xr=clamp16((wr+0x8000)>>16,polyClipLf_[polygon*2+1],polyClipRt_[polygon*2+1]);
                o16(polyPtr2_[polygon*2]);out_.push_back(uint8_t(xl));out_.push_back(uint8_t(xr));polyPtr2_[polygon*2]=int16_t(polyPtr2_[polygon*2]-4);polyPtr2_[polygon*2+1]=int16_t(polyPtr2_[polygon*2+1]-4);
            }
            polyStart_[polygon]=vx[poly*2];
        }
        arm(Stream::Op08,2,1);
    }
    void start08(){
        for(int i=0;i<4;i++) polyClipRt_[i]=w(i*2);
        for(int i=0;i<4;i++) polyClipLf_[i]=w(8+i*2);
        for(int i=0;i<4;i++) polyCx_[i]=w(32+i*2);
        for(int i=0;i<4;i++) polyPtr2_[i]=w(40+i*2);
        for(int i=0;i<4;i++) polyBottom2_[i]=w(48+i*2);
        for(int i=0;i<4;i++) polyTop2_[i]=w(56+i*2);
        distance_=w(72);std::array<int16_t,4>v={w(74),w(76),w(78),w(80)},e={w(82),w(84),w(86),w(88)};
        polyStart_[0]=v[0];polyStart_[1]=v[2];polyRaster2_[0]=polyRaster2_[1]=v[1];polyRaster2_[2]=polyRaster2_[3]=v[3];polyPlane_[0]=polyPlane_[1]=distance_;
        int16_t l=clamp16(polyCx_[0]-v[0]+e[0],polyClipLf_[0],polyClipRt_[0]);int16_t r=clamp16(polyCx_[1]-v[0]+e[1],polyClipLf_[1],polyClipRt_[1]);out_.clear();outPos_=0;out_.push_back(uint8_t(l));out_.push_back(uint8_t(r));arm(Stream::Op08,2,1);
    }
    void start09(){viewportCx_=w(0);viewportCy_=w(2);viewportLeft_=w(6);viewportRight_=w(8);viewportTop_=w(10);viewportBottom_=w(12);polyBottom_=int16_t(viewportBottom_-viewportCy_);polyRaster_=0x100;arm(Stream::Op09,4,1);}
    void project09Terrain(){
        int16_t cx=w(0),wx=w(4),wy=w(6);spriteAttr_=w(8);polyCx0_=cx;
        int seg=polyBottom_-raster_;int16_t vx=int16_t(int32_t(wx)*distance_>>15),vy=int16_t(int32_t(wy)*distance_>>15);spriteX_=int16_t(viewportCx_+vx-cx);spriteY_=int16_t(viewportBottom_-seg+vy);spriteSize_=true;arm(Stream::Op09,2,5);
    }
    void start0F(){
        worldY_=d(2);polyBottom_=w(6);polyTop_=w(8);polyCx1_=w(10);viewportBottom_=w(12);worldX_=d(14);polyCx0_=w(18);polyPtr_=w(20);worldYofs_=w(22);worldDy_=d(24);worldDx_=d(28);distance_=w(32);worldXenv_=d(36);worldDdy_=w(40);worldDdx_=w(42);viewYofsenv_=w(44);viewX1_=int16_t((worldX_+worldXenv_)>>16);viewY1_=int16_t(worldY_>>16);viewXofs1_=int16_t(worldX_>>16);viewYofs1_=worldYofs_;viewTurnoffX_=viewTurnoffDx_=0;polyRaster_=polyBottom_;project0F();
    }
    void finishProject0F(){
        if(pendingSegments_){int32_t px=(int32_t(viewXofs2_-viewXofs1_)*inverse(pendingSegments_))<<1,py=(int32_t(viewYofs2_-viewYofs1_)*inverse(pendingSegments_))<<1;int32_t xs=int32_t(int16_t(polyCx0_+viewXofs1_))<<16,ys=int32_t(int16_t(-viewportBottom_+viewYofs1_+viewYofsenv_+polyCx1_-worldYofs_))<<16;for(int i=0;i<pendingSegments_;i++){o16(polyPtr_);o16((ys+0x8000)>>16);o16((xs+0x8000)>>16);polyPtr_=int16_t(polyPtr_-4);xs+=px;ys+=py;}}
        viewX1_=viewX2_;viewY1_=viewY2_;viewXofs1_=viewXofs2_;viewYofs1_=viewYofs2_;worldDx_+=(int32_t(int16_t(worldDdx_))<<8);worldDy_+=(int32_t(int16_t(worldDdy_))<<8);worldX_+=worldDx_+worldXenv_;worldY_+=worldDy_;viewTurnoffX_=int16_t(viewTurnoffX_+viewTurnoffDx_);pendingSegments_=0;lightCount_=0;arm(Stream::Op0F,2,2);
    }
    void project0F(){
        out_.clear();outPos_=0;viewX2_=int16_t(((((worldX_+worldXenv_)>>16)*distance_)>>15)+((viewTurnoffX_*distance_)>>15));viewY2_=int16_t(((worldY_>>16)*distance_)>>15);viewXofs2_=viewX2_;viewYofs2_=int16_t(((int32_t(worldYofs_)*distance_)>>15)+polyBottom_-viewY2_);o16((worldX_+worldXenv_)>>16);o16(viewX2_);o16(worldY_>>16);o16(viewY2_);int seg=polyRaster_-viewY2_;if(viewY2_>=polyRaster_)seg=0;else polyRaster_=viewY2_;if(viewY2_<polyTop_){seg=0;if(viewY1_>=polyTop_)seg=viewY1_-polyTop_;}if(seg<0)seg=0;
        o16(seg);pendingSegments_=seg;if(seg){lightCount_=0;arm(Stream::Op0F,4,1);}else finishProject0F();
    }
    void executeInitial(){
        if(cmd_==0x0001){
            worldY_=d(0);polyBottom_=w(4);polyTop_=w(6);polyCx1_=w(8);viewportBottom_=w(10);worldX_=d(12);polyCx0_=w(16);polyPtr_=w(18);worldYofs_=w(20);worldDy_=d(22);worldDx_=d(26);distance_=w(30);worldXenv_=d(34);worldDdy_=w(38);worldDdx_=w(40);viewYofsenv_=w(42);
            viewX1_=int16_t((worldX_+worldXenv_)>>16);viewY1_=int16_t(worldY_>>16);viewXofs1_=int16_t(worldX_>>16);viewYofs1_=worldYofs_;viewTurnoffX_=viewTurnoffDx_=0;polyRaster_=polyBottom_;project01();return;
        }
        if(cmd_==0x0007){
            worldY_=d(0);polyBottom_=w(4);polyTop_=w(6);polyCx1_=w(8);viewportBottom_=w(10);worldX_=d(12);polyCx0_=w(16);polyPtr_=w(18);worldYofs_=w(20);distance_=w(22);viewY2_=w(24);viewDy_=int16_t((int32_t(w(26))*distance_)>>15);viewX2_=w(28);viewDx_=int16_t((int32_t(w(30))*distance_)>>15);viewYofsenv_=w(32);viewX1_=int16_t(worldX_>>16);viewY1_=int16_t(worldY_>>16);viewXofs1_=viewX1_;viewYofs1_=worldYofs_;polyRaster_=polyBottom_;project07();return;
        }
        if(cmd_==0x0008){start08();return;}
        if(cmd_==0x0009){start09();return;}
        if(cmd_==0x000f){start0F();return;}
        if(cmd_==0x000d){
            worldY_=d(0);polyBottom_=w(4);polyTop_=w(6);polyCx1_=w(8);viewportBottom_=w(10);worldX_=d(12);polyCx0_=w(16);polyPtr_=w(18);worldYofs_=w(20);worldDy_=d(22);worldDx_=d(26);distance_=w(30);worldXenv_=int32_t(int16_t(w(34)))<<8;worldDdy_=w(36);worldDdx_=w(38);viewYofsenv_=w(40);
            viewX1_=int16_t((worldX_+worldXenv_)>>16);viewY1_=int16_t(worldY_>>16);viewXofs1_=int16_t(worldX_>>16);viewYofs1_=worldYofs_;polyRaster_=polyBottom_;project0D();return;
        }
        if(cmd_==0x0010){
            worldY_=d(2);polyBottom_=w(6);polyTop_=w(8);polyCx1_=w(10);viewportBottom_=w(12);worldX_=d(14);polyCx0_=w(18);polyPtr_=w(20);worldYofs_=w(22);distance_=w(24);viewY2_=w(26);viewDy_=int16_t((int32_t(w(28))*distance_)>>15);viewX2_=w(30);viewDx_=int16_t((int32_t(w(32))*distance_)>>15);viewYofsenv_=w(34);
            viewX1_=int16_t(worldX_>>16);viewY1_=int16_t(worldY_>>16);viewXofs1_=viewX1_;viewYofs1_=worldYofs_;polyRaster_=polyBottom_;project10();return;
        }
        executeFixed();
    }
    void executeStreamPacket(){
        if(stream_==Stream::Op01){
            if(phase_==1){distance_=w(0);if(distance_==int16_t(0x8000)){finishCommand();return;}if(uint16_t(distance_)==0x8001){arm(Stream::Op01,6,2);return;}arm(Stream::Op01,6,3);return;}
            if(phase_==2){distance_=w(0);viewTurnoffX_=w(2);viewTurnoffDx_=w(4);viewX1_=int16_t(viewX1_+((int32_t(viewTurnoffX_)*distance_)>>15));viewXofs1_=int16_t(viewXofs1_+((int32_t(viewTurnoffX_)*distance_)>>15));viewTurnoffX_=int16_t(viewTurnoffX_+viewTurnoffDx_);arm(Stream::Op01,2,1);return;}
            if(phase_==3){worldDdy_=w(0);worldDdx_=w(2);viewYofsenv_=w(4);worldXenv_=0;project01();return;}
        }else if(stream_==Stream::Op07){
            if(phase_==1){distance_=w(0);if(distance_==int16_t(0x8000)){finishCommand();return;}arm(Stream::Op07,10,2);return;}
            if(phase_==2){viewY2_=w(0);viewDy_=int16_t((int32_t(w(2))*distance_)>>15);viewX2_=w(4);viewDx_=int16_t((int32_t(w(6))*distance_)>>15);viewYofsenv_=w(8);project07();return;}
        }else if(stream_==Stream::Op08){
            if(phase_==1){distance_=w(0);if(distance_==int16_t(0x8000)){out_.clear();outPos_=0;o16(0);finishCommand();return;}arm(Stream::Op08,16,2);return;}
            if(phase_==2){std::array<int16_t,4>v={w(0),w(2),w(4),w(6)},e={w(8),w(10),w(12),w(14)};outputPolygon08(v,e);return;}
        }else if(stream_==Stream::Op09){
            if(phase_==1){raster_=w(0);
                if(raster_<polyRaster_){spriteClipY_=int16_t(viewportBottom_-(polyBottom_-raster_));polyRaster_=raster_;}
                distance_=w(2);if(distance_==int16_t(0x8000)){finishCommand();return;}if(distance_==0){arm(Stream::Op09,4,1);return;}if(uint16_t(distance_)==0x9000){arm(Stream::Op09,14,2);return;}arm(Stream::Op09,10,4);return;}
            if(phase_==2){int32_t energy=uint16_t(w(0));int16_t ib=w(2),cb=w(4),il=w(6),cl=w(8);distance_=w(10);int16_t cr=w(12);int16_t wx=int16_t(cr-cl-(energy*(il-cl)>>16));int16_t wy=int16_t(cb-(energy*(cb-ib)>>16));spriteX_=int16_t(viewportCx_+(int32_t(wx)*distance_>>15));spriteY_=int16_t(viewportBottom_-(polyBottom_-(int32_t(wy)*distance_>>15)));out_.clear();outPos_=0;o16(wx);arm(Stream::Op09,4,3);return;}
            if(phase_==3){spriteY_=int16_t(spriteY_+w(0));spriteAttr_=w(2);spriteSize_=true;arm(Stream::Op09,2,5);return;}
            if(phase_==4){project09Terrain();return;}
            if(phase_==5){raster_=w(0);if(raster_==int16_t(0x8000)){finishCommand();return;}if(raster_==0){if(!spriteSize_){arm(Stream::Op09,4,1);return;}spriteSize_=false;arm(Stream::Op09,2,5);return;}uint16_t h=uint16_t(raster_)>>8;if(h!=0x20&&h!=0x2e&&h!=0x40&&h!=0x60&&h!=0xa0&&h!=0xc0&&h!=0xe0){arm(Stream::Op09,4,1);return;}arm(Stream::Op09,4,6);return;}
            if(phase_==6){int16_t dy=w(0),dx=w(2),sx=int16_t(spriteX_+dx),sy=int16_t(spriteY_+dy),attr=int16_t(spriteAttr_+raster_);int pix=spriteSize_?15:7;out_.clear();outPos_=0;bool draw=(sx>=viewportLeft_-pix&&sx<=viewportRight_&&sy>=viewportTop_-pix&&sy<=viewportBottom_&&sy<=spriteClipY_);if(draw)setOam(sx,sy,attr,spriteSize_,true);else o16(0);arm(Stream::Op09,2,5);return;}
        }else if(stream_==Stream::Op0F){
            if(phase_==1){out_.clear();outPos_=0;o16(lightColor(w(0),uint16_t(w(2))));++lightCount_;if(lightCount_>=4)finishProject0F();else arm(Stream::Op0F,4,1);return;}
            if(phase_==2){distance_=w(0);if(distance_==int16_t(0x8000)){finishCommand();return;}if(uint16_t(distance_)==0x8001){arm(Stream::Op0F,6,3);return;}arm(Stream::Op0F,6,4);return;}
            if(phase_==3){distance_=w(0);viewTurnoffX_=w(2);viewTurnoffDx_=w(4);viewX1_=int16_t(viewX1_+((int32_t(viewTurnoffX_)*distance_)>>15));viewXofs1_=int16_t(viewXofs1_+((int32_t(viewTurnoffX_)*distance_)>>15));viewTurnoffX_=int16_t(viewTurnoffX_+viewTurnoffDx_);arm(Stream::Op0F,2,2);return;}
            if(phase_==4){worldDdy_=w(0);worldDdx_=w(2);viewYofsenv_=w(4);worldXenv_=0;project0F();return;}
        }else if(stream_==Stream::Op0D){
            if(phase_==1){distance_=w(0);if(distance_==int16_t(0x8000)){finishCommand();return;}arm(Stream::Op0D,6,2);return;}
            if(phase_==2){worldDdy_=w(0);worldDdx_=w(2);viewYofsenv_=w(4);worldXenv_=0;project0D();return;}
        }else if(stream_==Stream::Op10){
            if(phase_==10){out_.clear();outPos_=0;o16(lightColor(w(0),uint16_t(w(2))));++lightCount_;if(lightCount_>=4)finishProject10();else arm(Stream::Op10,4,10);return;}
            if(phase_==1){distance_=w(0);if(distance_==int16_t(0x8000)){finishCommand();return;}arm(Stream::Op10,8,2);return;}
            if(phase_==2){viewY2_=w(0);viewDy_=int16_t((int32_t(w(2))*distance_)>>15);viewX2_=w(4);viewDx_=int16_t((int32_t(w(6))*distance_)>>15);project10();return;}
        }
        finishCommand();
    }
    void executeFixed(){
        out_.clear();outPos_=0;
        switch(cmd_){
            case 0x0000:{int32_t p=int32_t(w(0))*int32_t(w(2));o16(p);o16(uint32_t(p)>>16);break;}
            case 0x0003:oamRowMax_=33;oamRows_.fill(0);break;
            case 0x0005:oamIndex_=oamBits_=spriteCount_=0;oamAttr_.fill(0);break;
            case 0x0006:for(auto v:oamAttr_)o16(v);break;
            case 0x000a:{static const int16_t tab[16]={0x0000,0x0030,0x0060,0x0090,0x00c0,0x00f0,0x0120,0x0150,int16_t(0xfe80),int16_t(0xfeb0),int16_t(0xfee0),int16_t(0xff10),int16_t(0xff40),int16_t(0xff70),int16_t(0xffa0),int16_t(0xffd0)};uint16_t n=uint16_t(w(2));o16(tab[(n>>8)&15]);o16(tab[(n>>12)&15]);o16(tab[n&15]);o16(tab[(n>>4)&15]);break;}
            case 0x000b:setOam(w(0),w(2),w(4),false,true);break;
            case 0x000e:oamRowMax_=16;oamRows_.fill(0);break;
            case 0x0011:{int32_t dd=w(0),c=w(2),b=w(4),a=w(6);uint16_t m=uint16_t(((a*0x155>>2)&0xf000)|((b*0x155>>6)&0x0f00)|((c*0x155>>10)&0x00f0)|((dd*0x155>>14)&0x000f));o16(m);break;}
            default:break;
        }
        finishCommand();
    }
    bool waiting_=true,cmdHalf_=false;uint8_t cmdLo_=0;uint16_t cmd_=0;size_t expected_=0,outPos_=0;std::vector<uint8_t>in_,out_;Stream stream_=Stream::None;int phase_=0;
    int oamRowMax_=33;size_t oamIndex_=0;int oamBits_=0,spriteCount_=0;std::array<uint16_t,32>oamAttr_{};std::array<uint8_t,32>oamRows_{};
    int32_t worldX_=0,worldY_=0,worldDx_=0,worldDy_=0,worldXenv_=0;int16_t worldDdx_=0,worldDdy_=0;
    int16_t polyBottom_=0,polyTop_=0,polyCx0_=0,polyCx1_=0,polyPtr_=0,polyRaster_=0,viewportBottom_=0,worldYofs_=0,distance_=0;
    int16_t viewX1_=0,viewY1_=0,viewXofs1_=0,viewYofs1_=0,viewX2_=0,viewY2_=0,viewXofs2_=0,viewYofs2_=0,viewTurnoffX_=0,viewTurnoffDx_=0,viewDx_=0,viewDy_=0,viewYofsenv_=0;int pendingSegments_=0,lightCount_=0;
    std::array<int16_t,4> polyClipRt_{},polyClipLf_{},polyCx_{},polyPtr2_{},polyBottom2_{},polyTop2_{},polyRaster2_{};std::array<int16_t,2> polyStart_{},polyPlane_{};
    int16_t viewportCx_=0,viewportCy_=0,viewportLeft_=0,viewportRight_=0,viewportTop_=0,spriteClipY_=0,spriteX_=0,spriteY_=0,spriteAttr_=0,raster_=0;bool spriteSize_=true;
};
}
std::unique_ptr<CartridgeDevice> createDSP4Device(){return std::make_unique<DSP4Device>();}

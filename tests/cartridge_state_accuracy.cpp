#include "cx4.hpp"
#include "dsp1.hpp"
#include "dsp2.hpp"
#include "dsp3.hpp"
#include "dsp4.hpp"
#include "sdd1.hpp"
#include "sa1.hpp"
#include "spc7110_decomp.hpp"
#include "superfx.hpp"
#include "superfx2.hpp"
#include "seta.hpp"
#include <cstdint>
#include <iostream>
#include <vector>
namespace {
int failures=0;
void expect(bool c,const char* m){if(!c){std::cerr<<"FAIL: "<<m<<'\n';++failures;}}
void put16(CartridgeDevice& d,uint32_t a,uint16_t v){d.write(a,uint8_t(v));d.write(a,uint8_t(v>>8));}
uint16_t get16(CartridgeDevice& d,uint32_t a){uint8_t l=0,h=0;d.read(a,l);d.read(a,h);return uint16_t(l)|(uint16_t(h)<<8);}
}
int main(){
    auto d3=createDSP3Device(); const uint32_t a3=0x208000;
    d3->write(a3,0x38);put16(*d3,a3,1);put16(*d3,a3,1);put16(*d3,a3,0x0840);
    auto d3s=d3->saveState(); expect(!d3s.empty(),"DSP3 exposes checkpoint state");
    put16(*d3,a3,0xffff); expect(d3->loadState(d3s),"DSP3 restores mid-decoder state");
    put16(*d3,a3,0x0000);expect(get16(*d3,a3)==0x0042,"DSP3 decoder resumes deterministically after restore");
    auto d4=createDSP4Device(); const uint32_t a4=0x006000; put16(*d4,a4,0x0001);
    std::vector<uint16_t> packet={0,0,4,0,0,0,0,0,0,0x1200,0,0,0,0,0,0,0,0,0,0,0,0};
    for(size_t i=0;i<10;i++)put16(*d4,a4,packet[i]); auto d4s=d4->saveState();
    for(size_t i=10;i<packet.size();i++)put16(*d4,a4,packet[i]);
    uint16_t h1[5];for(auto&v:h1)v=get16(*d4,a4);
    expect(d4->loadState(d4s),"DSP4 restores partial command packet");
    for(size_t i=10;i<packet.size();i++)put16(*d4,a4,packet[i]);
    bool same=true;for(auto v:h1)same&=(get16(*d4,a4)==v);expect(same,"DSP4 resumed packet produces identical projection header");
    std::vector<uint8_t> spcRom(2*1024*1024);for(size_t i=0;i<spcRom.size();++i)spcRom[i]=uint8_t(i*37+11);
    SPC7110Decompressor dec([&spcRom](uint32_t a){return spcRom[a%spcRom.size()];});dec.initialize(2,0x100);for(int i=0;i<7;i++)dec.decode();auto ds=dec.saveState();
    std::vector<uint32_t> seq(24);for(auto&v:seq){dec.decode();v=dec.result();}expect(dec.loadState(ds),"SPC7110 decompressor restores arithmetic context");
    bool replay=true;for(auto v:seq){dec.decode();replay&=(dec.result()==v);}expect(replay,"SPC7110 decompressor replays byte-identical stream");
    std::vector<uint8_t> srom(4*1024*1024);for(size_t i=0;i<srom.size();++i)srom[i]=uint8_t(i*13+7);std::vector<uint8_t>sram(0x8000);bool dirty=false;
    auto sd=createSDD1Device(srom,sram,dirty);sd->dmaRegisterWrite(0,2,0);sd->dmaRegisterWrite(0,3,0);sd->dmaRegisterWrite(0,4,0xc0);sd->dmaRegisterWrite(0,5,32);sd->dmaRegisterWrite(0,6,0);sd->write(0x004800,1);sd->write(0x004801,1);sd->clock(256);sd->dmaBegin(0,0xc00000,32,0x08);uint8_t tmp=0;for(int i=0;i<5;i++)sd->dmaRead(0,0,tmp);auto sds=sd->saveState();
    std::vector<uint8_t> sseq(12);for(auto&v:sseq)sd->dmaRead(0,0,v);expect(sd->loadState(sds),"S-DD1 restores active DMA stream");
    replay=true;for(auto v:sseq){uint8_t q=0;replay&=sd->dmaRead(0,0,q)&&q==v;}expect(replay,"S-DD1 resumes DMA at exact byte position");
    auto cx=createCX4Device(srom);cx->write(0x006123,0x5a);auto cxs=cx->saveState();cx->write(0x006123,0xa5);expect(cx->loadState(cxs),"CX4 restores checkpoint");uint8_t q=0;cx->read(0x006123,q);expect(q==0x5a,"CX4 internal RAM restored");
    {
        std::vector<uint8_t> rr(0x2000); bool dd=false; uint8_t v=0;
        auto st10=createST010Device(rr,dd); st10->write(0x600000,0); st10->write(0x680020,6);
        auto ss=st10->saveState(); st10->write(0x680020,1); expect(st10->loadState(ss),"ST010 restores checkpoint state"); st10->read(0x680020,v); expect(v==6,"ST010 command register restored");
        auto st11=createST011Device(rr,dd); st11->write(0x680000,1); st11->write(0x680000,0x12); auto s11=st11->saveState(); st11->write(0x680000,0x34); expect(st11->loadState(s11),"ST011 restores partial command packet");
        auto st18=createST018Device(rr,dd); st18->write(0x003804,0); st18->write(0x003804,1); st18->write(0x003804,0); auto s18=st18->saveState(); st18->read(0x003804,v); expect(st18->loadState(s18),"ST018 restores handshake/output FIFO"); uint8_t v2=0; st18->read(0x003804,v2); expect(v2==0x81,"ST018 restored output FIFO resumes deterministically");
    }
    auto fx=createSuperFX2Device(srom,128*1024);fx->write(0x003000,0x34);fx->write(0x003001,0x12);fx->write(0x006010,0x77);auto fxs=fx->saveState();fx->write(0x003000,0);fx->write(0x006010,0);expect(fx->loadState(fxs),"SuperFX restores checkpoint");uint8_t lo=0,hi=0,rv=0;fx->read(0x003000,lo);fx->read(0x003001,hi);fx->read(0x006010,rv);expect(lo==0x34&&hi==0x12&&rv==0x77,"SuperFX registers and RAM restored");
    {
        auto gsu1 = createSuperFXDevice(srom, 128 * 1024);
        auto gsu2 = createSuperFX2Device(srom, 128 * 1024);
        gsu1->write(0x003000, 0xA5); gsu1->write(0x003001, 0x5A);
        gsu2->write(0x003000, 0x3C); gsu2->write(0x003001, 0xC3);
        const auto gsu1State = gsu1->saveState();
        const auto gsu2State = gsu2->saveState();
        expect(!gsu2->loadState(gsu1State), "GSU-2 rejects a GSU-1 device checkpoint");
        expect(gsu2->saveState() == gsu2State, "rejected GSU-1 checkpoint leaves GSU-2 state unchanged");
        expect(!gsu1->loadState(gsu2State), "GSU-1 rejects a GSU-2 device checkpoint");
        expect(gsu1->saveState() == gsu1State, "rejected GSU-2 checkpoint leaves GSU-1 state unchanged");
        uint8_t vcr1 = 0, vcr2 = 0;
        expect(gsu1->read(0x00303B, vcr1) && vcr1 == 0x04, "generic GSU-1 factory reports the measured GSU-1 VCR code 04");
        expect(gsu2->read(0x00303B, vcr2) && vcr2 == 0x04, "GSU-2 factory retains GSU-2 VCR identity");
    }
    {
        std::vector<uint8_t> grom(1024 * 1024, 0x01);
        grom[0x20] = 0xA5;
        grom[0x100] = 0xEF; grom[0x101] = 0x00;
        grom[0x120] = 0x3D; grom[0x121] = 0x31; grom[0x122] = 0x00;
        for (int i=0; i<8; ++i) grom[0x140 + i] = 0x01; grom[0x148] = 0x00;
        grom[0x160] = 0x4E; grom[0x161] = 0x4C; grom[0x162] = 0x00;
        grom[0x180] = 0x3D; grom[0x181] = 0x4C; grom[0x182] = 0x00;
        auto g = createSuperFX2Device(grom, 128 * 1024);
        auto wr = [&](unsigned reg, uint16_t value) {
            g->write(0x003000 + reg * 2, uint8_t(value));
            g->write(0x003001 + reg * 2, uint8_t(value >> 8));
        };
        auto rd = [&](unsigned reg) {
            uint8_t l=0,h=0; g->read(0x003000 + reg * 2,l); g->read(0x003001 + reg * 2,h);
            return uint16_t(l | (uint16_t(h) << 8));
        };
        auto start = [&](uint16_t pc) {
            g->write(0x00303A, 0x18);
            g->write(0x00301E, uint8_t(pc));
            g->write(0x00301F, uint8_t(pc >> 8));
        };
        g->write(0x003004, 0x5A);
        auto latchState = g->saveState();
        g->write(0x003006, 0xCC);
        expect(g->loadState(latchState), "SuperFX restores shared host register latch");
        g->write(0x003005, 0x12);
        expect(rd(2) == 0x125A, "SuperFX restored host latch affects subsequent high-byte register write");
        wr(14, 0x0020);
        uint8_t sfr=0; g->read(0x003030, sfr);
        expect((sfr & 0x40) != 0, "SuperFX checkpoint setup has pending R14 ROM transfer");
        auto romState = g->saveState();
        start(0x0100); g->clock(20000);
        expect(rd(0) == 0x00A5, "SuperFX pending ROM transfer completes before GETB");
        expect(g->loadState(romState), "SuperFX restores pending R14 transfer state");
        start(0x0100); g->clock(20000);
        expect(rd(0) == 0x00A5, "restored pending R14 transfer replays identically");
        wr(0, 0x005A); wr(1, 0x0040);
        start(0x0120); g->clock(20000);
        auto ramPendingState = g->saveState();
        g->write(0x700040, 0x11);
        expect(g->loadState(ramPendingState), "SuperFX restores pending RAM write state");
        start(0x0140); g->clock(20000);
        g->write(0x00303A, 0x00);
        uint8_t pendingRam=0; g->read(0x700040, pendingRam);
        expect(pendingRam == 0x5A, "restored serialized RAM write commits after execution resumes");
        wr(0, 0x0003); wr(1, 0); wr(2, 0);
        start(0x0160); g->clock(20000);
        auto pixelState = g->saveState();
        wr(0, 0x0001); wr(1, 0); wr(2, 0);
        start(0x0160); g->clock(20000);
        expect(g->loadState(pixelState), "SuperFX restores pixel-cache state");
        wr(1, 0); wr(2, 0);
        start(0x0180); g->clock(20000);
        expect(rd(0) == 0x0003, "restored incomplete PLOT cache is visible to RPIX");
    }
    {
        std::vector<uint8_t> arom(2 * 1024 * 1024, 0xEA);
        std::vector<uint8_t> abw(256 * 1024, 0); bool adirty = false;
        auto sa = createSA1Device(arom, abw, adirty); sa->reset();
        sa->write(0x002226, 0x80);
        sa->write(0x002229, 0xFF);
        sa->write(0x400123, 0x5A);
        sa->write(0x003045, 0xA5);
        auto sas = sa->saveState();
        expect(!sas.empty(), "SA-1 exposes checkpoint state");
        sa->write(0x400123, 0x11); sa->write(0x003045, 0x22);
        expect(sa->loadState(sas), "SA-1 restores full device checkpoint");
        uint8_t b=0,i=0; sa->read(0x400123,b); sa->read(0x003045,i);
        expect(b==0x5A && i==0xA5, "SA-1 checkpoint restores BW-RAM and I-RAM");
    }
    expect(!d3->loadState({1,2,3}),"truncated state rejected");expect(d3->stateTag()!=d4->stateTag(),"device tags distinguish incompatible state formats");
    if(failures){std::cerr<<failures<<" cartridge-state regressions failed\n";return 1;}
    std::cout<<"Cartridge coprocessor checkpoint regression passed\n";return 0;
}

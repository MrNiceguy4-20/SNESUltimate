#include "sdd1.hpp"
#include "state_io.hpp"
#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>
namespace {
class SDD1Device final : public CartridgeDevice {
public:
    explicit SDD1Device(const std::vector<uint8_t>& rom, std::vector<uint8_t>& ram, bool& dirty)
        : rom_(rom), ram_(ram), dirty_(dirty), rom_mask_(powerOfTwoMask(rom.size())), ram_mask_(powerOfTwoMask(ram.size())) { reset(); }
    void reset() override {
        regs_.fill(0);
        for (unsigned i = 0; i < 4; ++i) regs_[4 + i] = static_cast<uint8_t>(i);
        dma_address_.fill(0);
        dma_size_.fill(0);
        enable_age_.fill(0);
        active_channel_ = -1;
        active_dma_remaining_ = 0;
        stream_.clear();
        stream_pos_ = 0;
        produced_bytes_ = 0;
        decode_elapsed_ = 0;
        output_latch_ = 0;
        stream_mode_ = 0;
    }
    bool implemented() const override { return true; }
    bool needsClock() const override { return true; }
    bool hostIrqMayChangeWithoutHostAccess() const override { return false; }
    bool hostAccessRequiresClock(uint32_t address, bool write) const override {
        const uint8_t bank = static_cast<uint8_t>(address >> 16);
        const uint16_t addr = static_cast<uint16_t>(address);
        if ((bank <= 0x3f || (bank >= 0x80 && bank <= 0xbf)) && addr >= 0x4800 && addr <= 0x480f) return true;
        (void)write;
        return false;
    }
    uint32_t stateTag() const override { return 0x31444453u; }
    std::vector<uint8_t> saveState() const override {
        stateio::Writer w;
        uint32_t v = 2;
        w.pod(v); w.array(regs_); w.array(dma_address_); w.array(dma_size_); w.array(enable_age_);
        w.pod(active_channel_); w.pod(active_dma_remaining_); w.vector(stream_); w.pod(stream_pos_);
        w.pod(produced_bytes_); w.pod(decode_elapsed_); w.pod(output_latch_); w.pod(stream_mode_);
        return w.take();
    }
    bool loadState(const std::vector<uint8_t>& b) override {
        stateio::Reader r(b); uint32_t v = 0;
        if (!r.pod(v) || v != 2 || !r.array(regs_) || !r.array(dma_address_) || !r.array(dma_size_) || !r.array(enable_age_) ||
            !r.pod(active_channel_) || !r.pod(active_dma_remaining_) || !r.vector(stream_) || !r.pod(stream_pos_) ||
            !r.pod(produced_bytes_) || !r.pod(decode_elapsed_) || !r.pod(output_latch_) || !r.pod(stream_mode_) || !r.done()) return false;
        if (active_channel_ < -1 || active_channel_ >= 8 || stream_pos_ > stream_.size() || produced_bytes_ > stream_.size() || stream_mode_ > 3) return false;
        return true;
    }
    void clock(uint64_t masterClocks) override {
        for (unsigned n = 0; n < 8; ++n) {
            if ((regs_[1] & (1u << n)) != 0) {
                const uint64_t room = UINT64_MAX - enable_age_[n];
                enable_age_[n] += std::min<uint64_t>(masterClocks, room);
            }
        }
        if (active_channel_ >= 0) {
            const uint64_t room = UINT64_MAX - decode_elapsed_;
            decode_elapsed_ += std::min<uint64_t>(masterClocks, room);
            updateProducedBytes();
        }
    }
    bool read(uint32_t address, uint8_t& value) override {
        const uint8_t bank = static_cast<uint8_t>(address >> 16);
        const uint16_t addr = static_cast<uint16_t>(address);
        if ((bank <= 0x3f || (bank >= 0x80 && bank <= 0xbf)) && addr >= 0x4800 && addr <= 0x480f) {
            switch (addr) {
            case 0x4800: value = regs_[0]; return true;
            case 0x4801: value = regs_[1]; return true;
            case 0x4804: case 0x4805: case 0x4806: case 0x4807:
                value = regs_[addr - 0x4800]; return true;
            default:
                value = romRead(addr);
                return true;
            }
        }
        if (isRam(bank, addr)) { value = ramRead(ramOffset(bank, addr)); return true; }
        if (bank >= 0xc0) { value = romRead(mappedOffset(address)); return true; }
        if (addr >= 0x8000 && (((bank >= 0x20 && bank <= 0x3f) && (regs_[5] & 0x80)) ||
                              ((bank >= 0xa0 && bank <= 0xbf) && (regs_[7] & 0x80)))) {
            const uint8_t mbank = static_cast<uint8_t>(bank & 0x9f);
            const std::size_t p = (static_cast<std::size_t>(mbank & 0x1f) << 15) | (addr & 0x7fff);
            value = romRead(p); return true;
        }
        return false;
    }
    bool write(uint32_t address, uint8_t value) override {
        const uint8_t bank = static_cast<uint8_t>(address >> 16);
        const uint16_t addr = static_cast<uint16_t>(address);
        if ((bank <= 0x3f || (bank >= 0x80 && bank <= 0xbf)) && addr >= 0x4800 && addr <= 0x480f) {
            if (addr == 0x4800) regs_[0] = value;
            else if (addr == 0x4801) {
                regs_[1] = value;
                for (unsigned n = 0; n < 8; ++n) if (value & (1u << n)) enable_age_[n] = 0;
            } else if (addr >= 0x4804 && addr <= 0x4807) {
                regs_[addr - 0x4800] = static_cast<uint8_t>(value & 0x8f);
            }
            return true;
        }
        if (isRam(bank, addr)) { ramWrite(ramOffset(bank, addr), value); return true; }
        return false;
    }
    void dmaRegisterWrite(int channel, int offset, uint8_t value) override {
        if (channel < 0 || channel >= 8) return;
        switch (offset & 0x0f) {
        case 2: dma_address_[channel] = (dma_address_[channel] & 0xffff00u) | value; break;
        case 3: dma_address_[channel] = (dma_address_[channel] & 0xff00ffu) | (static_cast<uint32_t>(value) << 8); break;
        case 4: dma_address_[channel] = (dma_address_[channel] & 0x00ffffu) | (static_cast<uint32_t>(value) << 16); break;
        case 5: dma_size_[channel] = static_cast<uint16_t>((dma_size_[channel] & 0xff00u) | value); break;
        case 6: dma_size_[channel] = static_cast<uint16_t>((dma_size_[channel] & 0x00ffu) | (static_cast<uint16_t>(value) << 8)); break;
        default: break;
        }
    }
    void dmaBegin(int channel, uint32_t aAddress, uint32_t length, uint8_t control) override {
        active_channel_ = -1;
        active_dma_remaining_ = 0;
        stream_.clear(); stream_pos_ = 0; produced_bytes_ = 0; decode_elapsed_ = 0; output_latch_ = 0; stream_mode_ = 0;
        if (channel < 0 || channel >= 8) return;
        const uint8_t bit = static_cast<uint8_t>(1u << channel);
        const bool aToB = (control & 0x80) == 0;
        const bool fixed = (control & 0x08) != 0;
        if (!aToB || !fixed || (regs_[0] & bit) == 0 || (regs_[1] & bit) == 0) return;
        if (static_cast<uint8_t>(aAddress >> 16) < 0xc0) return;
        if ((dma_address_[channel] & 0xffffffu) != (aAddress & 0xffffffu)) return;
        const uint32_t sniffed = dma_size_[channel] ? dma_size_[channel] : 0x10000u;
        const uint32_t bytes = std::min<uint32_t>(length ? length : 0x10000u, sniffed);
        stream_.resize(bytes);
        decompress(stream_.data(), stream_.size(), mappedOffset(aAddress));
        active_channel_ = channel;
        active_dma_remaining_ = sniffed;
        stream_mode_ = static_cast<uint8_t>(romRead(mappedOffset(aAddress)) >> 6);
        decode_elapsed_ = enable_age_[channel];
        updateProducedBytes();
    }
    bool dmaRead(int channel, uint32_t, uint8_t& value) override {
        if (channel != active_channel_ || active_dma_remaining_ == 0) return false;
        if (stream_pos_ < produced_bytes_ && stream_pos_ < stream_.size()) {
            output_latch_ = stream_[stream_pos_++];
        }
        value = output_latch_;
        if (--active_dma_remaining_ == 0) {
            regs_[1] = static_cast<uint8_t>(regs_[1] & ~(1u << channel));
            active_channel_ = -1;
            stream_.clear(); stream_pos_ = 0; produced_bytes_ = 0; decode_elapsed_ = 0;
        }
        return true;
    }
    void dmaEnd(int channel) override {
        if (channel == active_channel_) {
            active_channel_ = -1;
            active_dma_remaining_ = 0;
            stream_.clear(); stream_pos_ = 0; produced_bytes_ = 0; decode_elapsed_ = 0;
        }
    }
private:
    void updateProducedBytes() {
        if (stream_.empty()) { produced_bytes_ = 0; return; }
        uint64_t count = 0;
        if (stream_mode_ == 3) {
            if (decode_elapsed_ >= 23) count = 1 + (decode_elapsed_ - 23) / 8;
        } else {
            if (decode_elapsed_ >= 31) count = 2 + 2 * ((decode_elapsed_ - 31) / 16);
        }
        produced_bytes_ = std::min<std::size_t>(stream_.size(), static_cast<std::size_t>(count));
    }
    static std::size_t powerOfTwoMask(std::size_t size) { return size && (size & (size - 1)) == 0 ? size - 1 : 0; }
    std::size_t romIndex(std::size_t p) const { return rom_mask_ ? (p & rom_mask_) : (p % rom_.size()); }
    std::size_t ramIndex(std::size_t p) const { return ram_mask_ ? (p & ram_mask_) : (p % ram_.size()); }
    struct Evo { uint8_t size,mps,lps; };
    static constexpr std::array<Evo,33> evo_{{
        {0,25,25},{0,2,1},{0,3,1},{0,4,2},{0,5,3},{1,6,4},{1,7,5},{1,8,6},
        {1,9,7},{2,10,8},{2,11,9},{2,12,10},{2,13,11},{3,14,12},{3,15,13},{3,16,14},
        {3,17,15},{4,18,16},{4,19,17},{5,20,18},{5,21,19},{6,22,20},{6,23,21},{7,24,22},
        {7,24,23},{0,26,1},{1,27,2},{2,28,4},{3,29,8},{4,30,12},{5,31,16},{6,32,18},{7,24,22}
    }};
    static constexpr std::array<uint8_t,128> run_{{
        128,64,96,32,112,48,80,16,120,56,88,24,104,40,72,8,124,60,92,28,108,44,76,12,116,52,84,20,100,36,68,4,
        126,62,94,30,110,46,78,14,118,54,86,22,102,38,70,6,122,58,90,26,106,42,74,10,114,50,82,18,98,34,66,2,
        127,63,95,31,111,47,79,15,119,55,87,23,103,39,71,7,123,59,91,27,107,43,75,11,115,51,83,19,99,35,67,3,
        125,61,93,29,109,45,77,13,117,53,85,21,101,37,69,5,121,57,89,25,105,41,73,9,113,49,81,17,97,33,65,1
    }};
    static bool isRam(uint8_t bank, uint16_t addr) {
        return (((bank<=0x3f)||(bank>=0x80&&bank<=0xbf)) && addr>=0x6000&&addr<=0x7fff) ||
               (bank>=0x70&&bank<=0x73);
    }
    static std::size_t ramOffset(uint8_t bank, uint16_t addr) {
        if (bank>=0x70&&bank<=0x73) return (static_cast<std::size_t>(bank-0x70)<<16)|addr;
        return (static_cast<std::size_t>(bank&0x1f)<<13)|(addr-0x6000);
    }
    uint8_t ramRead(std::size_t p) const { return ram_.empty()?0xff:ram_[ramIndex(p)]; }
    void ramWrite(std::size_t p,uint8_t v){ if(ram_.empty())return; p=ramIndex(p); if(ram_[p]!=v){ram_[p]=v;dirty_=true;} }
    uint8_t romRead(std::size_t p) const { return rom_.empty()?0xff:rom_[romIndex(p)]; }
    std::size_t mappedOffset(uint32_t a) const {
        const uint8_t bank=static_cast<uint8_t>(a>>16);
        if (bank>=0xc0) {
            const unsigned slot=(bank-0xc0)>>4;
            const unsigned sel=regs_[4+(slot&3)]&0x0f;
            return (static_cast<std::size_t>(sel)<<20) | (a&0x0fffff);
        }
        return a&0x3fffff;
    }
    uint8_t input(std::size_t& p) const { uint8_t v=romRead(p); ++p; return v; }
    void decompress(uint8_t* out, std::size_t len, std::size_t src) const {
        if (!out || len==0) return;
        std::size_t p=src;
        uint8_t first=input(p), second=input(p);
        const int type=first>>6;
        int high=0,low=0;
        switch(first&0x30){case 0x00:high=0x1c0;low=1;break;case 0x10:high=0x180;low=1;break;case 0x20:high=0x0c0;low=1;break;default:high=0x180;low=3;break;}
        uint16_t bits=static_cast<uint16_t>((first<<11)|(second<<3)); int valid=5;
        std::array<uint8_t,8> ctr{}; std::array<uint8_t,32> state{}; std::array<uint8_t,32> mps{}; std::array<int,8> prev{};
        auto codeword=[&](int n)->uint8_t{ if(!valid){bits|=input(p);valid=8;} bits<<=1; --valid; bits^=0x8000; if(bits&0x8000) return static_cast<uint8_t>(0x80+(1<<n)); uint8_t t=static_cast<uint8_t>((bits>>8)|(0x7f>>n)); bits<<=n; valid-=n; if(valid<0){bits|=static_cast<uint16_t>(input(p)<<(-valid));valid+=8;} return run_[t]; };
        auto golomb=[&](int n)->uint8_t{ if(!ctr[n]) ctr[n]=codeword(n); --ctr[n]; if(ctr[n]==0x80){ctr[n]=0;return 2;} return ctr[n]==0?1:0; };
        auto prob=[&](uint8_t c)->uint8_t{ uint8_t st=state[c], b=golomb(evo_[st].size); if(b&1){state[c]=evo_[st].lps; if(st<2){mps[c]^=1;return mps[c];} return mps[c]^1;} if(b){state[c]=evo_[st].mps;} return mps[c]; };
        auto get=[&](uint8_t plane)->uint8_t{ uint8_t b=prob(static_cast<uint8_t>(((plane&1)<<4)|((prev[plane]&high)>>5)|(prev[plane]&low))); prev[plane]=(prev[plane]<<1)|b; return b; };
        std::size_t o=0; uint8_t plane=0, phase=0;
        while(o<len){
            if(type==3){ uint8_t v=0; for(uint8_t mask=1,pl=0;mask;mask<<=1,++pl) if(get(pl))v|=mask; out[o++]=v; continue; }
            uint8_t a=0,b=0; for(uint8_t mask=0x80;mask;mask>>=1){if(get(plane))a|=mask;if(get(static_cast<uint8_t>(plane+1)))b|=mask;}
            out[o++]=a; if(o<len)out[o++]=b;
            if(type==1){ phase=static_cast<uint8_t>(phase+32); if(phase==0) plane=static_cast<uint8_t>((plane+2)&7); }
            else if(type==2){ phase=static_cast<uint8_t>(phase+32); if(phase==0) plane^=2; }
        }
    }
    const std::vector<uint8_t>& rom_;
    std::vector<uint8_t>& ram_;
    bool& dirty_;
    std::array<uint8_t,8> regs_{};
    std::array<uint32_t,8> dma_address_{};
    std::array<uint16_t,8> dma_size_{};
    std::array<uint64_t,8> enable_age_{};
    int active_channel_=-1;
    uint32_t active_dma_remaining_=0;
    std::vector<uint8_t> stream_;
    std::size_t stream_pos_=0;
    std::size_t produced_bytes_=0;
    uint64_t decode_elapsed_=0;
    uint8_t output_latch_=0;
    uint8_t stream_mode_=0;
    const std::size_t rom_mask_;
    const std::size_t ram_mask_;
};
}
std::unique_ptr<CartridgeDevice> createSDD1Device(const std::vector<uint8_t>& rom, std::vector<uint8_t>& ram, bool& dirty){ return std::make_unique<SDD1Device>(rom,ram,dirty); }

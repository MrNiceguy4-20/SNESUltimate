#include "spc7110_decomp.hpp"
#include "state_io.hpp"
namespace {
struct Model { uint8_t probability; uint8_t next[2]; };
constexpr Model evolution[53] = {
  {0x5a,{1,1}},{0x25,{2,6}},{0x11,{3,8}},{0x08,{4,10}},{0x03,{5,12}},{0x01,{5,15}},
  {0x5a,{7,7}},{0x3f,{8,19}},{0x2c,{9,21}},{0x20,{10,22}},{0x17,{11,23}},{0x11,{12,25}},{0x0c,{13,26}},{0x09,{14,28}},{0x07,{15,29}},{0x05,{16,31}},{0x04,{17,32}},{0x03,{18,34}},{0x02,{5,35}},
  {0x5a,{20,20}},{0x48,{21,39}},{0x3a,{22,40}},{0x2e,{23,42}},{0x26,{24,44}},{0x1f,{25,45}},{0x19,{26,46}},{0x15,{27,25}},{0x11,{28,26}},{0x0e,{29,26}},{0x0b,{30,27}},{0x09,{31,28}},{0x08,{32,29}},{0x07,{33,30}},{0x05,{34,31}},{0x04,{35,33}},{0x04,{36,33}},{0x03,{37,34}},{0x02,{38,35}},{0x02,{5,36}},
  {0x58,{40,39}},{0x4d,{41,47}},{0x43,{42,48}},{0x3b,{43,49}},{0x34,{44,50}},{0x2e,{45,51}},{0x29,{46,44}},{0x25,{24,45}},
  {0x56,{48,47}},{0x4f,{49,47}},{0x47,{50,48}},{0x41,{51,49}},{0x3c,{52,50}},{0x37,{43,51}},
};
}
void SPC7110Decompressor::reset() {
    for (auto& root : context_) for (auto& node : root) node = {};
    bpp_ = 1; offset_ = 0; bits_ = 8; range_ = 256; input_ = 0; output_ = 0; pixels_ = 0; colormap_ = 0xfedcba9876543210ull; result_ = 0;
}
uint32_t SPC7110Decompressor::deinterleave(uint64_t data, uint32_t bits) {
    data = data & ((1ull << bits) - 1);
    data = 0x5555555555555555ull & (data << bits | data >> 1);
    data = 0x3333333333333333ull & (data | data >> 1);
    data = 0x0f0f0f0f0f0f0f0full & (data | data >> 2);
    data = 0x00ff00ff00ff00ffull & (data | data >> 4);
    data = 0x0000ffff0000ffffull & (data | data >> 8);
    return uint32_t(data | data >> 16);
}
uint64_t SPC7110Decompressor::moveToFront(uint64_t list, uint32_t nibble) {
    for (uint64_t n = 0, mask = ~15ull; n < 64; n += 4, mask <<= 4) {
        if ((list >> n & 15) != nibble) continue;
        return (list & mask) + (list << 4 & ~mask) + nibble;
    }
    return list;
}
void SPC7110Decompressor::initialize(uint32_t mode, uint32_t origin) {
    for (auto& root : context_) for (auto& node : root) node = {};
    bpp_ = 1u << mode; offset_ = origin; bits_ = 8; range_ = 256;
    input_ = read(); input_ = uint16_t(input_ << 8 | read());
    output_ = 0; pixels_ = 0; colormap_ = 0xfedcba9876543210ull;
}
void SPC7110Decompressor::decode() {
    for (uint32_t pixel = 0; pixel < 8; pixel++) {
        uint64_t map = colormap_;
        uint32_t diff = 0;
        if (bpp_ > 1) {
            uint32_t pa = (bpp_ == 2 ? pixels_ >> 2 & 3 : pixels_ >> 0 & 15);
            uint32_t pb = (bpp_ == 2 ? pixels_ >> 14 & 3 : pixels_ >> 28 & 15);
            uint32_t pc = (bpp_ == 2 ? pixels_ >> 16 & 3 : pixels_ >> 32 & 15);
            if (pa != pb || pb != pc) {
                uint32_t match = pa ^ pb ^ pc;
                diff = 4;
                if ((match ^ pc) == 0) diff = 3;
                if ((match ^ pb) == 0) diff = 2;
                if ((match ^ pa) == 0) diff = 1;
            }
            colormap_ = moveToFront(colormap_, pa);
            map = moveToFront(map, pc);
            map = moveToFront(map, pb);
            map = moveToFront(map, pa);
        }
        for (uint32_t plane = 0; plane < bpp_; plane++) {
            uint32_t bit = bpp_ > 1 ? 1u << plane : 1u << (pixel & 3);
            uint32_t history = (bit - 1) & output_;
            uint32_t set = 0;
            if (bpp_ == 1) set = pixel >= 4;
            if (bpp_ == 2) set = diff;
            if (plane >= 2 && history <= 1) set = diff;
            auto& ctx = context_[set][bit + history - 1];
            const Model& model = evolution[ctx.prediction];
            uint8_t lpsOffset = uint8_t(range_ - model.probability);
            bool symbol = input_ >= (uint32_t(lpsOffset) << 8);
            output_ = uint8_t(output_ << 1 | (symbol ^ ctx.swap));
            if (!symbol) range_ = lpsOffset;
            else { range_ = uint16_t(range_ - lpsOffset); input_ = uint16_t(input_ - (uint32_t(lpsOffset) << 8)); }
            while (range_ <= 0x7f) {
                ctx.prediction = model.next[symbol];
                range_ = uint16_t(range_ << 1);
                input_ = uint16_t(input_ << 1);
                if (--bits_ == 0) { bits_ = 8; input_ = uint16_t(input_ + read()); }
            }
            if (symbol && model.probability > 0x55) ctx.swap ^= 1;
        }
        uint32_t index = output_ & ((1u << bpp_) - 1);
        if (bpp_ == 1) index ^= uint32_t(pixels_ >> 15 & 1);
        pixels_ = pixels_ << bpp_ | (map >> 4 * index & 15);
    }
    if (bpp_ == 1) result_ = uint32_t(pixels_);
    if (bpp_ == 2) result_ = deinterleave(pixels_, 16);
    if (bpp_ == 4) result_ = deinterleave(deinterleave(pixels_, 32), 32);
}
std::vector<uint8_t> SPC7110Decompressor::saveState() const {
    stateio::Writer w; uint32_t ver = 2; w.pod(ver);
    w.array(context_); w.pod(bpp_); w.pod(offset_); w.pod(bits_); w.pod(range_); w.pod(input_); w.pod(output_); w.pod(pixels_); w.pod(colormap_); w.pod(result_);
    return w.take();
}
bool SPC7110Decompressor::loadState(const std::vector<uint8_t>& b) {
    stateio::Reader r(b); uint32_t ver = 0;
    if (!r.pod(ver) || ver != 2) return false;
    if (!r.array(context_) || !r.pod(bpp_) || !r.pod(offset_) || !r.pod(bits_) || !r.pod(range_) || !r.pod(input_) || !r.pod(output_) || !r.pod(pixels_) || !r.pod(colormap_) || !r.pod(result_) || !r.done()) return false;
    if (bpp_ != 1 && bpp_ != 2 && bpp_ != 4) return false;
    if (bits_ < 1 || bits_ > 8) return false;
    for (auto& root : context_) for (auto& node : root) if (node.prediction >= 53) return false;
    return true;
}

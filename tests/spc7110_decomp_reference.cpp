#include "spc7110_decomp.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>
namespace {
struct Model { uint8_t probability; uint8_t next[2]; };
const Model evolution[53] = {
  {0x5a,{1,1}},{0x25,{2,6}},{0x11,{3,8}},{0x08,{4,10}},{0x03,{5,12}},{0x01,{5,15}},
  {0x5a,{7,7}},{0x3f,{8,19}},{0x2c,{9,21}},{0x20,{10,22}},{0x17,{11,23}},{0x11,{12,25}},{0x0c,{13,26}},{0x09,{14,28}},{0x07,{15,29}},{0x05,{16,31}},{0x04,{17,32}},{0x03,{18,34}},{0x02,{5,35}},
  {0x5a,{20,20}},{0x48,{21,39}},{0x3a,{22,40}},{0x2e,{23,42}},{0x26,{24,44}},{0x1f,{25,45}},{0x19,{26,46}},{0x15,{27,25}},{0x11,{28,26}},{0x0e,{29,26}},{0x0b,{30,27}},{0x09,{31,28}},{0x08,{32,29}},{0x07,{33,30}},{0x05,{34,31}},{0x04,{35,33}},{0x04,{36,33}},{0x03,{37,34}},{0x02,{38,35}},{0x02,{5,36}},
  {0x58,{40,39}},{0x4d,{41,47}},{0x43,{42,48}},{0x3b,{43,49}},{0x34,{44,50}},{0x2e,{45,51}},{0x29,{46,44}},{0x25,{24,45}},
  {0x56,{48,47}},{0x4f,{49,47}},{0x47,{50,48}},{0x41,{51,49}},{0x3c,{52,50}},{0x37,{43,51}},
};
struct Reference {
    const std::vector<uint8_t>& drom;
    struct Ctx { uint8_t prediction = 0, swap = 0; } context[5][15];
    uint32_t bpp = 0, offset = 0, bits = 0;
    uint16_t range = 0, input = 0;
    uint8_t output = 0;
    uint64_t pixels = 0, colormap = 0;
    uint32_t result = 0;
    explicit Reference(const std::vector<uint8_t>& d) : drom(d) {}
    uint8_t read() { return drom[(offset++) % drom.size()]; }
    uint32_t deinterleave(uint64_t data, uint32_t n) {
        data = data & ((1ull << n) - 1);
        data = 0x5555555555555555ull & (data << n | data >> 1);
        data = 0x3333333333333333ull & (data | data >> 1);
        data = 0x0f0f0f0f0f0f0f0full & (data | data >> 2);
        data = 0x00ff00ff00ff00ffull & (data | data >> 4);
        data = 0x0000ffff0000ffffull & (data | data >> 8);
        return uint32_t(data | data >> 16);
    }
    uint64_t moveToFront(uint64_t list, uint32_t nibble) {
        for (uint64_t n = 0, mask = ~15ull; n < 64; n += 4, mask <<= 4) {
            if ((list >> n & 15) != nibble) continue;
            return (list & mask) + (list << 4 & ~mask) + nibble;
        }
        return list;
    }
    void initialize(uint32_t mode, uint32_t origin) {
        for (auto& root : context) for (auto& node : root) node = {0, 0};
        bpp = 1u << mode; offset = origin; bits = 8; range = 256;
        input = read(); input = uint16_t(input << 8 | read());
        output = 0; pixels = 0; colormap = 0xfedcba9876543210ull;
    }
    void decode() {
        for (uint32_t pixel = 0; pixel < 8; pixel++) {
            uint64_t map = colormap;
            uint32_t diff = 0;
            if (bpp > 1) {
                uint32_t pa = (bpp == 2 ? pixels >> 2 & 3 : pixels >> 0 & 15);
                uint32_t pb = (bpp == 2 ? pixels >> 14 & 3 : pixels >> 28 & 15);
                uint32_t pc = (bpp == 2 ? pixels >> 16 & 3 : pixels >> 32 & 15);
                if (pa != pb || pb != pc) {
                    uint32_t match = pa ^ pb ^ pc;
                    diff = 4;
                    if ((match ^ pc) == 0) diff = 3;
                    if ((match ^ pb) == 0) diff = 2;
                    if ((match ^ pa) == 0) diff = 1;
                }
                colormap = moveToFront(colormap, pa);
                map = moveToFront(map, pc);
                map = moveToFront(map, pb);
                map = moveToFront(map, pa);
            }
            for (uint32_t plane = 0; plane < bpp; plane++) {
                uint32_t bit = bpp > 1 ? 1u << plane : 1u << (pixel & 3);
                uint32_t history = (bit - 1) & output;
                uint32_t set = 0;
                if (bpp == 1) set = pixel >= 4;
                if (bpp == 2) set = diff;
                if (plane >= 2 && history <= 1) set = diff;
                auto& ctx = context[set][bit + history - 1];
                auto& model = evolution[ctx.prediction];
                uint8_t lpsOffset = uint8_t(range - model.probability);
                bool symbol = input >= (lpsOffset << 8);
                output = uint8_t(output << 1 | (symbol ^ ctx.swap));
                if (!symbol) range = lpsOffset;
                else { range = uint16_t(range - lpsOffset); input = uint16_t(input - (lpsOffset << 8)); }
                while (range <= 0xff / 2) {
                    ctx.prediction = model.next[symbol];
                    range <<= 1; input = uint16_t(input << 1);
                    if (--bits == 0) { bits = 8; input = uint16_t(input + read()); }
                }
                if (symbol && model.probability > 0x55) ctx.swap ^= 1;
            }
            uint32_t index = output & ((1u << bpp) - 1);
            if (bpp == 1) index ^= uint32_t(pixels >> 15 & 1);
            pixels = pixels << bpp | (map >> 4 * index & 15);
        }
        if (bpp == 1) result = uint32_t(pixels);
        if (bpp == 2) result = deinterleave(pixels, 16);
        if (bpp == 4) result = deinterleave(deinterleave(pixels, 32), 32);
    }
};
}
int main() {
    std::mt19937 rng(0x7110);
    unsigned mismatches = 0;
    for (unsigned mode = 0; mode < 3; ++mode) {
        for (unsigned trial = 0; trial < 6; ++trial) {
            std::vector<uint8_t> drom(0x40000);
            for (auto& b : drom) b = uint8_t(rng() & ((trial & 1) ? 0xff : (rng() & 0xff)));
            const size_t origin = (rng() % 0x1000);
            Reference ref(drom);
            ref.initialize(mode, uint32_t(origin));
            ref.decode();
            SPC7110Decompressor ours([&drom](uint32_t a) { return drom[a % drom.size()]; });
            ours.initialize(mode, uint32_t(origin));
            ours.decode();
            for (unsigned row = 0; row < 400; ++row) {
                if (ours.result() != ref.result || ours.bpp() != ref.bpp) ++mismatches;
                ref.decode();
                ours.decode();
            }
            if (mismatches) { std::cout << "mode " << mode << " trial " << trial << " mismatches " << mismatches << std::endl; return 1; }
        }
    }
    std::cout << "spc7110 decompressor matches ares reference ok\n";
    return 0;
}

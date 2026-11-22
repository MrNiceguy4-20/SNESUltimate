#include "bus.hpp"
#include "ppu.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>
struct Rng {
    uint32_t s;
    uint32_t next() { s = s * 1664525u + 1013904223u; return s >> 8; }
    uint8_t byte() { return static_cast<uint8_t>(next()); }
};
struct Machine {
    Bus bus;
    PPU ppu;
    Machine(bool fast) {
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.setJitScanline(fast);
    }
    void w(uint16_t reg, uint8_t v) { ppu.writeRegister(reg, v); }
    void w16(uint16_t reg, uint16_t v) { w(reg, static_cast<uint8_t>(v)); w(reg, static_cast<uint8_t>(v >> 8)); }
};
struct Write { int tick; uint16_t reg; uint8_t value; };
static void configure(Machine& m, uint32_t seed, const std::vector<uint8_t>& regs) {
    Rng r{seed};
    m.w(0x2100, 0x80);
    m.w(0x2115, 0x80);
    m.w(0x2116, 0);
    m.w(0x2117, 0);
    for (int i = 0; i < 32768; ++i) {
        m.w(0x2118, r.byte() & (i < 2048 ? 0xFF : 0x7F));
        m.w(0x2119, r.byte());
    }
    m.w(0x2121, 0);
    for (int i = 0; i < 512; ++i) m.w(0x2122, r.byte() & (i & 1 ? 0x7F : 0xFF));
    m.w(0x2102, 0);
    m.w(0x2103, 0);
    for (int i = 0; i < 544; ++i) m.w(0x2104, r.byte());
    static const uint16_t order[] = {0x2101, 0x2105, 0x2106, 0x2107, 0x2108, 0x2109, 0x210A, 0x210B, 0x210C,
                                     0x2123, 0x2124, 0x2125, 0x2126, 0x2127, 0x2128, 0x2129, 0x212A, 0x212B,
                                     0x212C, 0x212D, 0x212E, 0x212F, 0x2130, 0x2131};
    for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); ++i) m.w(order[i], regs[i]);
    for (uint16_t reg = 0x210D; reg <= 0x2114; ++reg) m.w16(reg, static_cast<uint16_t>(r.next()));
    m.w(0x2132, static_cast<uint8_t>(0x20 | (r.byte() & 0x1F)));
    m.w(0x2132, static_cast<uint8_t>(0x40 | (r.byte() & 0x1F)));
    m.w(0x2132, static_cast<uint8_t>(0x80 | (r.byte() & 0x1F)));
    m.w(0x2133, 0x00);
    m.w(0x2100, 0x0F);
}
static void runFrame(Machine& a, Machine& b, int firstTick, const std::vector<Write>& writes, int& tick) {
    const uint64_t target = a.ppu.frameCounter() + 1;
    while (a.ppu.frameCounter() < target) {
        for (const Write& wr : writes)
            if (wr.tick == tick - firstTick) { a.w(wr.reg, wr.value); b.w(wr.reg, wr.value); }
        a.ppu.tick();
        b.ppu.tick();
        ++tick;
    }
}
static void compare(const Machine& a, const Machine& b, uint32_t seed) {
    assert(a.ppu.width() == b.ppu.width() && a.ppu.height() == b.ppu.height());
    for (int y = 0; y < a.ppu.height(); ++y)
        for (int x = 0; x < a.ppu.width(); ++x) {
            const uint32_t pa = a.ppu.framebuffer()[static_cast<size_t>(y) * a.ppu.framebufferStride() + x];
            const uint32_t pb = b.ppu.framebuffer()[static_cast<size_t>(y) * b.ppu.framebufferStride() + x];
            if (pa != pb) {
                std::cerr << "mismatch seed " << seed << " at " << x << "," << y << "\n";
                assert(pa == pb);
            }
        }
}
int main() {
    int fastModeCases = 0;
    for (uint32_t seed = 1; seed <= 60; ++seed) {
        Rng r{seed * 7919u};
        static const uint8_t modes[] = {0, 1, 1, 1, 0, 0, 1, 0};
        std::vector<uint8_t> regs(24);
        for (auto& v : regs) v = r.byte();
        const uint8_t mode = modes[seed % 8];
        regs[1] = static_cast<uint8_t>(mode | (r.byte() & 0xF8));
        regs[2] = 0x00;
        regs[9] = 0x00; regs[10] = 0x00; regs[11] = 0x00; regs[12] = 0x00; regs[13] = 0x00; regs[14] = 0x00;
        regs[18] = static_cast<uint8_t>(r.byte() | 0x03);
        regs[19] = 0x00;
        regs[20] = 0x00; regs[21] = 0x00;
        regs[22] = static_cast<uint8_t>(r.byte() & 0x32 & (seed % 4 == 0 ? 0x00 : 0x3F));
        regs[23] = r.byte();
        ++fastModeCases;
        Machine slow(false), fast(true);
        configure(slow, seed, regs);
        configure(fast, seed, regs);
        std::vector<Write> writes;
        for (int i = 0; i < 6; ++i) {
            static const uint16_t pool[] = {0x210D, 0x210E, 0x210F, 0x2110, 0x2107, 0x212C, 0x212D, 0x2126, 0x2130, 0x2131, 0x2105};
            const uint16_t reg = pool[r.next() % (sizeof(pool) / sizeof(pool[0]))];
            writes.push_back({static_cast<int>(r.next() % (340 * 200)), reg, r.byte()});
        }
        int tick = 0;
        runFrame(slow, fast, 0, {}, tick);
        compare(slow, fast, seed);
        runFrame(slow, fast, tick, writes, tick);
        compare(slow, fast, seed);
        runFrame(slow, fast, tick, {}, tick);
        compare(slow, fast, seed);
    }
    assert(fastModeCases >= 50);
    std::cout << "ppu jit scanline matches per-dot renderer\n";
    return 0;
}

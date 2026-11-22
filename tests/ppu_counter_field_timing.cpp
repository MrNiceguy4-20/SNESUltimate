#include "bus.hpp"
#include "ppu.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
static void advanceTo(PPU& ppu, uint64_t field, int v, int h) {
    while (ppu.frameCounter() != field || ppu.scanline() != v || ppu.dot() != h) {
        ppu.tick();
    }
}
int main() {
    {
        PPU ppu;
        advanceTo(ppu, 0, 0, 300);
        ppu.latchCounters();
        assert(ppu.readRegister(0x213C) == static_cast<uint8_t>(300));
        while (!(ppu.scanline() == 1 && ppu.dot() == 11)) ppu.tick();
        ppu.latchCounters();
        assert((ppu.readRegister(0x213C) & 0x01) == 0);
        assert(ppu.readRegister(0x213C) == 11);
        (void)ppu.readRegister(0x213F);
        assert(ppu.readRegister(0x213C) == 11);
    }
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        while (ppu.dot() != 20) ppu.tick();
        bus.write(0x004201, 0x80);
        bus.write(0x004201, 0x00);
        assert(ppu.latchedHCounter() == 20);
        while (ppu.dot() != 30) ppu.tick();
        bus.write(0x004201, 0x00);
        (void)ppu.readRegister(0x2137);
        assert(ppu.latchedHCounter() == 20);
        bus.write(0x004201, 0x80);
        assert(ppu.latchedHCounter() == 20);
        while (ppu.dot() != 40) ppu.tick();
        (void)ppu.readRegister(0x2137);
        assert(ppu.latchedHCounter() == 40);
    }
    {
        PPU ppu;
        assert((ppu.readRegister(0x213F) & 0x80) == 0);
        while (ppu.frameCounter() == 0) ppu.tick();
        assert(ppu.scanline() == 0 && ppu.dot() == 0);
        assert((ppu.readRegister(0x213F) & 0x80) == 0);
        ppu.tick();
        assert(ppu.scanline() == 0 && ppu.dot() == 1);
        assert((ppu.readRegister(0x213F) & 0x80) != 0);
    }
    {
        PPU ppu;
        ppu.setPalRegion(true);
        ppu.writeRegister(0x2133, 0x01);
        assert(ppu.masterClocksForLine(311, false) == 1364);
        assert(ppu.masterClocksForLine(311, true) == 1368);
        advanceTo(ppu, 1, 311, 0);
        assert(ppu.dotsThisLine() == PPU::LONG_DOTS_PER_LINE);
        uint64_t clocks = 0;
        for (int h = 0; h < PPU::LONG_DOTS_PER_LINE; ++h) {
            assert(ppu.dot() == h);
            clocks += static_cast<uint64_t>(ppu.masterClocksThisDot());
            if (h == 340) {
                ppu.latchCounters();
                assert(ppu.latchedHCounter() == 340);
            }
            ppu.tick();
        }
        assert(clocks == 1368);
        assert(ppu.scanline() == 312 && ppu.dot() == 0);
    }
    std::cout << "phase24 PPU counter/field timing accuracy ok\n";
    return 0;
}

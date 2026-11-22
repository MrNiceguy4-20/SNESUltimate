#include "ppu.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
int main() {
    {
        PPU ppu;
        assert(ppu.scanline() == 0);
        assert(ppu.dot() == 0);
        uint64_t total = 0;
        for (int h = 0; h < PPU::DOTS_PER_LINE; ++h) {
            while (ppu.dot() != h) ppu.tick();
            const int clocks = ppu.masterClocksThisDot();
            if (h == 323 || h == 327) {
                assert(clocks == 6);
            } else {
                assert(clocks == 4);
            }
            total += static_cast<uint64_t>(clocks);
            ppu.tick();
        }
        assert(total == 1364);
        assert(ppu.scanline() == 1);
        assert(ppu.dot() == 0);
    }
    {
        PPU ppu;
        while (ppu.dot() != 322) ppu.tick();
        assert(ppu.masterClocksThisDot() == 4);
        while (ppu.dot() != 326) ppu.tick();
        assert(ppu.masterClocksThisDot() == 4);
    }
    {
        PPU ppu;
        assert(ppu.masterClocksForLine(240, true) == 1360);
        assert(ppu.masterClocksForLine(240, false) == 1364);
        assert(ppu.masterClocksForLine(0, true) == 1364);
        assert(ppu.masterClocksForLine(100, false) == 1364);
    }
    std::cout << "phase44 PPU long-dot timing accuracy ok\n";
    return 0;
}

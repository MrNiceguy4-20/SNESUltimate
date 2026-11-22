#include "bus.hpp"
#include "ppu.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
int main() {
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        const uint8_t s0 = ppu.readRegister(0x213F);
        assert((s0 & 0x0F) == 0x03);
        assert((s0 & 0x10) == 0x00);
        assert((s0 & 0x80) == 0x00);
        assert((s0 & 0x40) == 0x00);
    }
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.readRegister(0x2137);
        const uint8_t s = ppu.readRegister(0x213F);
        assert((s & 0x40) != 0);
        const uint8_t s2 = ppu.readRegister(0x213F);
        assert((s2 & 0x40) == 0);
    }
    {
        PPU ppu;
        ppu.setPalRegion(true);
        assert(ppu.palRegion());
        const uint8_t s = ppu.readRegister(0x213F);
        assert((s & 0x10) != 0);
        ppu.setPalRegion(false);
        assert((ppu.readRegister(0x213F) & 0x10) == 0);
    }
    {
        PPU ppu;
        assert((ppu.readRegister(0x213F) & 0x80) == 0);
        const int lines = ppu.linesThisFrame();
        for (int i = 0; i < lines * PPU::DOTS_PER_LINE; ++i)
            ppu.tick();
        assert(ppu.frameCounter() == 1);
        assert(ppu.scanline() == 0 && ppu.dot() == 0);
        assert((ppu.readRegister(0x213F) & 0x80) == 0);
        ppu.tick();
        assert((ppu.readRegister(0x213F) & 0x80) != 0);
    }
    std::cout << "phase52 PPU STAT78 accuracy ok\n";
    return 0;
}

#include "bus.hpp"
#include "ppu.hpp"
#include "snes.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
int main() {
    {
        PPU ppu;
        assert(ppu.dot() == 0);
        assert(ppu.hblank());
        ppu.tick();
        assert(ppu.dot() == 1);
        assert(!ppu.hblank());
        while (ppu.dot() < 100) ppu.tick();
        assert(!ppu.hblank());
        while (ppu.dot() < PPU::HBLANK_START_DOT) ppu.tick();
        assert(ppu.dot() == PPU::HBLANK_START_DOT);
        assert(ppu.hblank());
        while (ppu.dot() < PPU::DOTS_PER_LINE - 1) ppu.tick();
        assert(ppu.dot() == PPU::DOTS_PER_LINE - 1);
        assert(ppu.hblank());
        ppu.tick();
        assert(ppu.dot() == 0);
        assert(ppu.scanline() == 1);
        assert(ppu.hblank());
        ppu.tick();
        assert(ppu.dot() == 1);
        assert(!ppu.hblank());
    }
    {
        SNES snes;
        snes.reset();
        const uint64_t before = snes.scheduler().now();
        snes.runCycles(100);
        assert(snes.scheduler().now() > before);
        assert(snes.ppu().dot() >= 0);
    }
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        bus.reset();
        bus.updateVideoTiming(0, 0, ppu.hblank(), ppu.vblank());
        const uint8_t s0 = bus.read(0x4212);
        assert((s0 & 0x40) != 0);
        ppu.tick();
        bus.updateVideoTiming(ppu.scanline(), ppu.dot(), ppu.hblank(), ppu.vblank());
        const uint8_t s1 = bus.read(0x4212);
        assert((s1 & 0x40) == 0);
    }
    std::cout << "phase43 PPU HBlank status accuracy ok\n";
    return 0;
}

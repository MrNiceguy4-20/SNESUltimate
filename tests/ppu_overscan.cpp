#include "bus.hpp"
#include "ppu.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
int main() {
    {
        PPU ppu;
        assert(!ppu.overscanEnabled());
        assert(ppu.height() == 224);
        assert(ppu.vblankStartLine() == 225);
        ppu.writeRegister(0x2133, 0x04);
        assert(ppu.overscanEnabled());
        assert(ppu.height() == 239);
        assert(ppu.vblankStartLine() == 240);
        ppu.writeRegister(0x2133, 0x00);
        assert(ppu.height() == 224);
        assert(ppu.vblankStartLine() == 225);
    }
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.writeRegister(0x2100, 0x80);
        ppu.writeRegister(0x2133, 0x04);
        ppu.writeRegister(0x2100, 0x0F);
        while (ppu.scanline() < 230) {
            ppu.tick();
        }
        while (ppu.dot() < PPU::VISIBLE_DOT_START + 4)
            ppu.tick();
        assert(ppu.scanline() == 230);
        assert(ppu.height() == 239);
        const uint32_t* fb = ppu.framebuffer();
        (void)fb[230 * ppu.framebufferStride() + 0];
        (void)fb[238 * ppu.framebufferStride() + 255];
    }
    {
        PPU ppu;
        assert(ppu.height() == 224);
        assert(230 >= ppu.height());
    }
    std::cout << "phase50 PPU overscan accuracy ok\n";
    return 0;
}

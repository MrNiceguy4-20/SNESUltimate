#include "bus.hpp"
#include "ppu.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
static void setBackdropRed(PPU& ppu) {
    ppu.writeRegister(0x2100, 0x80);
    ppu.writeRegister(0x2121, 0x00);
    ppu.writeRegister(0x2122, 0x1F);
    ppu.writeRegister(0x2122, 0x00);
    ppu.writeRegister(0x2100, 0x0F);
}
static void advanceToVisibleX(PPU& ppu, int x) {
    while (ppu.scanline() < 1) ppu.tick();
    while (ppu.dot() < PPU::VISIBLE_DOT_START + x) ppu.tick();
}
int main() {
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.setPpu2Revision(PPU::Ppu2Revision::PPU2B);
        setBackdropRed(ppu);
        advanceToVisibleX(ppu, 8);
        bus.write(0x7E0000, 0x80);
        bus.write(0x002100, 0x0F);
        ppu.tick();
        ppu.tick();
        const uint32_t* fb = ppu.framebuffer();
        assert(fb[8] == 0xFF000000u);
        assert(fb[9] == 0xFF0000FFu);
    }
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.setPpu2Revision(PPU::Ppu2Revision::OneChip);
        assert(ppu.ppu2Version() == 3);
        assert(ppu.oneChipPpu());
        setBackdropRed(ppu);
        advanceToVisibleX(ppu, 8);
        bus.write(0x7E0000, 0x80);
        bus.write(0x002100, 0x0F);
        ppu.tick();
        const uint32_t* fb = ppu.framebuffer();
        assert(fb[8] == 0xFF000000u);
    }
    std::cout << "phase5 INIDISP early-read/revision behavior ok\n";
    return 0;
}

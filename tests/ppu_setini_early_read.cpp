#include "bus.hpp"
#include "ppu.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
static void advanceToVisibleX(PPU& ppu, int x) {
    while (ppu.scanline() < 1) ppu.tick();
    while (ppu.dot() < PPU::VISIBLE_DOT_START + x) ppu.tick();
}
int main() {
    Bus bus;
    PPU ppu;
    ppu.attachBus(&bus);
    bus.attachPPU(&ppu);
    ppu.setPpu2Revision(PPU::Ppu2Revision::PPU2B);
    ppu.writeRegister(0x2100, 0x0F);
    ppu.writeRegister(0x2105, 0x07);
    ppu.writeRegister(0x212C, 0x03);
    ppu.writeRegister(0x2133, 0x40);
    advanceToVisibleX(ppu, 8);
    bus.write(0x7E0000, 0x00);
    bus.write(0x002133, 0x40);
    assert(ppu.readRegister(0x2133) == bus.openBus());
    ppu.tick();
    ppu.tick();
    while (ppu.dot() < PPU::VISIBLE_DOT_START + 12) ppu.tick();
    bus.write(0x7E0000, 0x08);
    bus.write(0x002133, 0x00);
    ppu.tick();
    assert(ppu.width() == 512);
    std::cout << "phase13 SETINI early-read raster behavior ok\n";
    return 0;
}

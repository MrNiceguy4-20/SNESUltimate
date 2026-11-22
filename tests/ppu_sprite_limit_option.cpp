#include "bus.hpp"
#include "ppu.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
static void setVramAddress(PPU& ppu, uint16_t word) {
    ppu.writeRegister(0x2115, 0x80);
    ppu.writeRegister(0x2116, static_cast<uint8_t>(word & 0xff));
    ppu.writeRegister(0x2117, static_cast<uint8_t>(word >> 8));
}
static void writeSolid4bppTile(PPU& ppu, uint16_t tile, uint8_t color) {
    setVramAddress(ppu, static_cast<uint16_t>(tile * 16));
    for (int row = 0; row < 8; ++row) {
        ppu.writeRegister(0x2118, (color & 1) ? 0xFF : 0x00);
        ppu.writeRegister(0x2119, (color & 2) ? 0xFF : 0x00);
    }
    for (int row = 0; row < 8; ++row) {
        ppu.writeRegister(0x2118, (color & 4) ? 0xFF : 0x00);
        ppu.writeRegister(0x2119, (color & 8) ? 0xFF : 0x00);
    }
}
static unsigned litSprites(bool removeLimit) {
    Bus bus;
    PPU ppu;
    ppu.attachBus(&bus);
    bus.attachPPU(&ppu);
    ppu.setSpriteLimitRemoved(removeLimit);
    ppu.writeRegister(0x2100, 0x80);
    ppu.writeRegister(0x2101, 0x00);
    writeSolid4bppTile(ppu, 0, 1);
    ppu.writeRegister(0x2121, 129);
    ppu.writeRegister(0x2122, 0x1F);
    ppu.writeRegister(0x2122, 0x00);
    ppu.writeRegister(0x2102, 0);
    ppu.writeRegister(0x2103, 0);
    for (int i = 0; i < 128; ++i) {
        ppu.writeRegister(0x2104, static_cast<uint8_t>(i < 40 ? i * 6 : 0));
        ppu.writeRegister(0x2104, static_cast<uint8_t>(i < 40 ? 1 : 224));
        ppu.writeRegister(0x2104, 0);
        ppu.writeRegister(0x2104, 0);
    }
    ppu.writeRegister(0x212C, 0x10);
    ppu.writeRegister(0x2100, 0x0f);
    while (ppu.scanline() < 2) ppu.tick();
    while (ppu.dot() <= PPU::VISIBLE_DOT_START + 255) ppu.tick();
    const uint32_t* row = ppu.framebuffer() + static_cast<size_t>(ppu.framebufferStride());
    unsigned lit = 0;
    for (int i = 0; i < 40; ++i)
        if (row[i * 6 + 3] == 0xFF0000FFu) ++lit;
    return lit;
}
int main() {
    const unsigned limited = litSprites(false);
    const unsigned unlimited = litSprites(true);
    assert(limited == 32);
    assert(unlimited == 40);
    std::cout << "ppu sprite limit option ok\n";
    return 0;
}

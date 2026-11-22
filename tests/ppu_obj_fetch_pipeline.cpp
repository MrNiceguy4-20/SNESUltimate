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
static void writeColor(PPU& ppu, uint8_t index, uint16_t color) {
    ppu.writeRegister(0x2121, index);
    ppu.writeRegister(0x2122, static_cast<uint8_t>(color & 0xff));
    ppu.writeRegister(0x2122, static_cast<uint8_t>((color >> 8) & 0x7f));
}
static void hideAllSprites(PPU& ppu) {
    ppu.writeRegister(0x2102, 0);
    ppu.writeRegister(0x2103, 0);
    for (int i = 0; i < 128; ++i) {
        ppu.writeRegister(0x2104, 0);
        ppu.writeRegister(0x2104, 224);
        ppu.writeRegister(0x2104, 0);
        ppu.writeRegister(0x2104, 0);
    }
}
static void writeSprite0(PPU& ppu, uint8_t y, uint8_t tile, uint8_t attr) {
    ppu.writeRegister(0x2102, 0);
    ppu.writeRegister(0x2103, 0);
    ppu.writeRegister(0x2104, 0);
    ppu.writeRegister(0x2104, y);
    ppu.writeRegister(0x2104, tile);
    ppu.writeRegister(0x2104, attr);
}
int main() {
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.writeRegister(0x2100, 0x80);
        ppu.writeRegister(0x2101, 0x00);
        writeSolid4bppTile(ppu, 0, 1);
        writeColor(ppu, 129, 0x001f);
        writeSprite0(ppu, 1, 0, 0);
        ppu.writeRegister(0x212C, 0x10);
        ppu.writeRegister(0x2100, 0x0f);
        while (ppu.scanline() < 2) ppu.tick();
        assert(ppu.scanline() == 2 && ppu.dot() == 0);
        ppu.writeRegister(0x2101, 0x01);
        while (ppu.dot() <= PPU::VISIBLE_DOT_START) ppu.tick();
        assert(ppu.framebuffer()[static_cast<size_t>(ppu.framebufferStride())] == 0xFF0000FFu);
    }
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.writeRegister(0x2100, 0x80);
        ppu.writeRegister(0x2101, 0x60);
        hideAllSprites(ppu);
        writeSolid4bppTile(ppu, 0xF0, 1);
        writeColor(ppu, 129, 0x001f);
        writeSprite0(ppu, 1, 0xff, 0);
        ppu.writeRegister(0x212C, 0x10);
        ppu.writeRegister(0x2100, 0x0f);
        while (ppu.scanline() < 2) ppu.tick();
        while (ppu.dot() <= PPU::VISIBLE_DOT_START + 8) ppu.tick();
        const size_t pixel8 = static_cast<size_t>(ppu.framebufferStride() + 8);
        assert(ppu.framebuffer()[pixel8] == 0xFF0000FFu);
    }
    std::cout << "phase3 timed OBJ fetch/tile-wrap accuracy ok\n";
    return 0;
}

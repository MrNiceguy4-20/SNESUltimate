#include "bus.hpp"
#include "ppu.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
static void writeSolid4bppTile0(PPU& ppu) {
    ppu.writeRegister(0x2115, 0x80);
    ppu.writeRegister(0x2116, 0x00);
    ppu.writeRegister(0x2117, 0x00);
    for (int row = 0; row < 8; ++row) {
        ppu.writeRegister(0x2118, 0xFF);
        ppu.writeRegister(0x2119, 0x00);
    }
}
static void writeColor(PPU& ppu, uint8_t index, uint16_t color) {
    ppu.writeRegister(0x2121, index);
    ppu.writeRegister(0x2122, static_cast<uint8_t>(color & 0xFF));
    ppu.writeRegister(0x2122, static_cast<uint8_t>((color >> 8) & 0x7F));
}
static void writeSprite(PPU& ppu, uint8_t x, uint8_t y, uint8_t tile, uint8_t attr) {
    ppu.writeRegister(0x2104, x);
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
        writeSolid4bppTile0(ppu);
        writeColor(ppu, 129, 0x001F);
        writeColor(ppu, 145, 0x7C00);
        ppu.writeRegister(0x2102, 0x00);
        ppu.writeRegister(0x2103, 0x00);
        writeSprite(ppu, 0, 0, 0, 0x00);
        writeSprite(ppu, 0, 0, 0, 0x02);
        ppu.writeRegister(0x212C, 0x10);
        ppu.writeRegister(0x2102, 0x00);
        ppu.writeRegister(0x2103, 0x00);
        ppu.writeRegister(0x2100, 0x0F);
        while (ppu.scanline() < 1) ppu.tick();
        for (int i = 0; i <= PPU::VISIBLE_DOT_START; ++i) ppu.tick();
        assert(ppu.framebuffer()[0] == 0xFF0000FFu);
    }
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.writeRegister(0x2100, 0x80);
        writeSolid4bppTile0(ppu);
        writeColor(ppu, 129, 0x001F);
        writeColor(ppu, 145, 0x7C00);
        ppu.writeRegister(0x2102, 0x00);
        ppu.writeRegister(0x2103, 0x00);
        writeSprite(ppu, 0, 0, 0, 0x00);
        writeSprite(ppu, 0, 0, 0, 0x02);
        ppu.writeRegister(0x212C, 0x10);
        ppu.writeRegister(0x2102, 0x02);
        ppu.writeRegister(0x2103, 0x80);
        assert(ppu.objPriorityRotation());
        assert(ppu.firstSpriteIndex() == 1);
        ppu.writeRegister(0x2100, 0x0F);
        while (ppu.scanline() < 1) ppu.tick();
        for (int i = 0; i <= PPU::VISIBLE_DOT_START; ++i) ppu.tick();
        assert(ppu.framebuffer()[0] == 0xFFFF0000u);
    }
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.writeRegister(0x2100, 0x80);
        ppu.writeRegister(0x2101, 0xA0);
        ppu.writeRegister(0x2102, 0x00);
        ppu.writeRegister(0x2103, 0x00);
        for (int i = 0; i < 33; ++i) writeSprite(ppu, 0, 0, 0, 0);
        ppu.writeRegister(0x2100, 0x0F);
        assert(!ppu.objRangeOver());
        assert(!ppu.objTimeOver());
        while (ppu.dot() < 65) ppu.tick();
        assert(!ppu.objRangeOver());
        ppu.tick();
        assert(ppu.objRangeOver());
        assert(!ppu.objTimeOver());
        while (ppu.dot() < PPU::OBJ_FETCH_START_DOT) ppu.tick();
        assert(!ppu.objTimeOver());
        while (ppu.dot() < 339) {
            ppu.tick();
            assert(!ppu.objTimeOver());
        }
        ppu.tick();
        assert(ppu.scanline() == 1 && ppu.dot() == 0);
        assert(ppu.objTimeOver());
        (void)bus.write(0x7E0000, 0x10);
        const uint8_t stat77 = ppu.readRegister(0x213E);
        assert((stat77 & 0xC0) == 0xC0);
        assert((stat77 & 0x10) == 0x00);
        assert((stat77 & 0x0F) == 0x01);
        const uint64_t frame = ppu.frameCounter();
        while (ppu.frameCounter() == frame) ppu.tick();
        assert(!ppu.objRangeOver());
        assert(!ppu.objTimeOver());
    }
    std::cout << "phase38 OAM/status accuracy ok\n";
    return 0;
}

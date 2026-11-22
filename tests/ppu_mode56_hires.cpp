#include "bus.hpp"
#include "ppu.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
static void fillTile4bpp(PPU& ppu, uint16_t wordBase, uint8_t fill) {
    ppu.writeRegister(0x2115, 0x80);
    ppu.writeRegister(0x2116, static_cast<uint8_t>(wordBase & 0xFF));
    ppu.writeRegister(0x2117, static_cast<uint8_t>((wordBase >> 8) & 0xFF));
    for (int row = 0; row < 8; ++row) {
        uint8_t b0 = 0, b1 = 0, b2 = 0, b3 = 0;
        for (int col = 0; col < 8; ++col) {
            if (fill & 1) b0 |= static_cast<uint8_t>(0x80 >> col);
            if (fill & 2) b1 |= static_cast<uint8_t>(0x80 >> col);
            if (fill & 4) b2 |= static_cast<uint8_t>(0x80 >> col);
            if (fill & 8) b3 |= static_cast<uint8_t>(0x80 >> col);
        }
        ppu.writeRegister(0x2118, b0);
        ppu.writeRegister(0x2119, b1);
    }
    for (int row = 0; row < 8; ++row) {
        uint8_t b2 = 0, b3 = 0;
        for (int col = 0; col < 8; ++col) {
            if (fill & 4) b2 |= static_cast<uint8_t>(0x80 >> col);
            if (fill & 8) b3 |= static_cast<uint8_t>(0x80 >> col);
        }
        ppu.writeRegister(0x2118, b2);
        ppu.writeRegister(0x2119, b3);
    }
}
static void writeColor(PPU& ppu, uint8_t index, uint16_t color) {
    ppu.writeRegister(0x2121, index);
    ppu.writeRegister(0x2122, static_cast<uint8_t>(color & 0xFF));
    ppu.writeRegister(0x2122, static_cast<uint8_t>((color >> 8) & 0x7F));
}
int main() {
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.writeRegister(0x2100, 0x80);
        ppu.writeRegister(0x2105, 0x05);
        assert(ppu.hiresOutputEnabled());
        assert(ppu.width() == 512);
        assert(ppu.framebufferStride() == 512);
        ppu.writeRegister(0x2107, 0x00);
        ppu.writeRegister(0x210B, 0x01);
        ppu.writeRegister(0x212C, 0x01);
        ppu.writeRegister(0x212D, 0x01);
        writeColor(ppu, 0, 0x0000);
        writeColor(ppu, 1, 0x001F);
        writeColor(ppu, 2, 0x03E0);
        ppu.writeRegister(0x2115, 0x80);
        ppu.writeRegister(0x2116, 0x00);
        ppu.writeRegister(0x2117, 0x00);
        ppu.writeRegister(0x2118, 0x00);
        ppu.writeRegister(0x2119, 0x00);
        fillTile4bpp(ppu, 0x1000, 1);
        fillTile4bpp(ppu, 0x1010, 2);
        ppu.writeRegister(0x2100, 0x0F);
        while (ppu.scanline() < 1) ppu.tick();
        for (int i = 0; i < PPU::VISIBLE_DOT_START + 8; ++i)
            ppu.tick();
        const uint32_t* fb = ppu.framebuffer();
        bool anyLit = false;
        for (int x = 0; x < 16; ++x) {
            if ((fb[x] & 0x00FFFFFF) != 0)
                anyLit = true;
        }
        assert(anyLit);
        assert(ppu.bgMode() == 5);
    }
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.writeRegister(0x2100, 0x80);
        ppu.writeRegister(0x2105, 0x06);
        assert(ppu.bgMode() == 6);
        assert(ppu.hiresOutputEnabled());
        assert(ppu.width() == 512);
        ppu.writeRegister(0x2100, 0x0F);
        while (ppu.scanline() < 1) ppu.tick();
        for (int i = 0; i < PPU::VISIBLE_DOT_START + 4; ++i)
            ppu.tick();
        assert(ppu.bgMode() == 6);
    }
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        writeColor(ppu, 0, 0x001F);
        ppu.writeRegister(0x2105, 0x01);
        ppu.writeRegister(0x2100, 0x0F);
        for (int i = 0; i < PPU::DOTS_PER_LINE * 2; ++i) ppu.tick();
        assert(ppu.scanline() == 2);
        assert(ppu.width() == 256);
        const uint32_t before = ppu.framebuffer()[0];
        assert((before & 0x00FFFFFFu) != 0);
        ppu.writeRegister(0x2105, 0x06);
        while (ppu.scanline() < 1) ppu.tick();
        for (int i = 0; i < PPU::VISIBLE_DOT_START + 1; ++i) ppu.tick();
        assert(ppu.width() == 512);
        const uint32_t* fb = ppu.framebuffer();
        assert(fb[0] == before);
        assert(fb[1] == before);
        assert(fb[2] == before);
        assert(fb[3] == before);
        ppu.writeRegister(0x2105, 0x01);
        assert(ppu.width() == 512);
    }
    std::cout << "phase45 PPU Mode 5/6 hi-res accuracy ok\n";
    return 0;
}

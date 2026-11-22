#include "bus.hpp"
#include "ppu.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
static void writeColor(PPU& ppu, uint8_t index, uint16_t color) {
    ppu.writeRegister(0x2121, index);
    ppu.writeRegister(0x2122, static_cast<uint8_t>(color & 0xFF));
    ppu.writeRegister(0x2122, static_cast<uint8_t>((color >> 8) & 0x7F));
}
static void fillTile2bpp(PPU& ppu, uint16_t wordBase, uint8_t fill) {
    ppu.writeRegister(0x2115, 0x80);
    ppu.writeRegister(0x2116, static_cast<uint8_t>(wordBase & 0xFF));
    ppu.writeRegister(0x2117, static_cast<uint8_t>((wordBase >> 8) & 0xFF));
    const uint8_t plane0 = (fill & 1) ? 0xFF : 0x00;
    const uint8_t plane1 = (fill & 2) ? 0xFF : 0x00;
    for (int row = 0; row < 8; ++row) {
        ppu.writeRegister(0x2118, plane0);
        ppu.writeRegister(0x2119, plane1);
    }
}
static uint32_t expectedRgba(uint16_t bgr555) {
    const uint8_t r5 = static_cast<uint8_t>(bgr555 & 0x1F);
    const uint8_t g5 = static_cast<uint8_t>((bgr555 >> 5) & 0x1F);
    const uint8_t b5 = static_cast<uint8_t>((bgr555 >> 10) & 0x1F);
    const uint8_t r = static_cast<uint8_t>(((r5 * 255u + 15u) / 31u));
    const uint8_t g = static_cast<uint8_t>(((g5 * 255u + 15u) / 31u));
    const uint8_t b = static_cast<uint8_t>(((b5 * 255u + 15u) / 31u));
    return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) |
           (static_cast<uint32_t>(b) << 16) | 0xFF000000u;
}
int main() {
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.writeRegister(0x2100, 0x80);
        writeColor(ppu, 0, 0x001F);
        writeColor(ppu, 1, 0x03E0);
        ppu.writeRegister(0x2105, 0x00);
        ppu.writeRegister(0x2107, 0x00);
        ppu.writeRegister(0x210B, 0x01);
        ppu.writeRegister(0x2115, 0x80);
        ppu.writeRegister(0x2116, 0x00);
        ppu.writeRegister(0x2117, 0x00);
        ppu.writeRegister(0x2118, 0x00);
        ppu.writeRegister(0x2119, 0x00);
        fillTile2bpp(ppu, 0x1000, 1);
        ppu.writeRegister(0x2133, 0x08);
        ppu.writeRegister(0x212C, 0x01);
        ppu.writeRegister(0x212D, 0x00);
        assert(ppu.hiresOutputEnabled());
        assert(ppu.width() == 512);
        assert(ppu.framebufferStride() == 512);
        ppu.writeRegister(0x2100, 0x0F);
        while (ppu.scanline() < 1) ppu.tick();
        for (int i = 0; i < PPU::VISIBLE_DOT_START + 4; ++i)
            ppu.tick();
        const uint32_t* fb = ppu.framebuffer();
        const uint32_t red = expectedRgba(0x001F);
        const uint32_t green = expectedRgba(0x03E0);
        assert(fb[0] == expectedRgba(0x0000));
        assert(fb[1] == green);
        assert(fb[2] == red);
        assert(fb[3] == green);
        while (ppu.scanline() < 2 || ppu.dot() <= PPU::VISIBLE_DOT_START) ppu.tick();
        assert(fb[512] == expectedRgba(0x0000));
        assert(fb[513] == green);
    }
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.writeRegister(0x2100, 0x80);
        writeColor(ppu, 0, 0x001F);
        writeColor(ppu, 1, 0x03E0);
        ppu.writeRegister(0x2105, 0x00);
        ppu.writeRegister(0x2107, 0x00);
        ppu.writeRegister(0x210B, 0x01);
        ppu.writeRegister(0x2115, 0x80);
        ppu.writeRegister(0x2116, 0x00);
        ppu.writeRegister(0x2117, 0x00);
        ppu.writeRegister(0x2118, 0x00);
        ppu.writeRegister(0x2119, 0x00);
        fillTile2bpp(ppu, 0x1000, 1);
        ppu.writeRegister(0x212C, 0x01);
        ppu.writeRegister(0x212D, 0x00);
        ppu.writeRegister(0x2130, 0x02);
        ppu.writeRegister(0x2131, 0x01);
        ppu.writeRegister(0x2133, 0x08);
        ppu.writeRegister(0x2100, 0x0F);
        while (ppu.scanline() < 1) ppu.tick();
        for (int i = 0; i < PPU::VISIBLE_DOT_START + 2; ++i)
            ppu.tick();
        const uint32_t* fb = ppu.framebuffer();
        assert(fb[0] == expectedRgba(0x0000));
        assert(fb[1] == expectedRgba(0x03E0));
        assert(fb[2] == expectedRgba(0x001F));
        assert(fb[3] == expectedRgba(0x03E0));
    }
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.writeRegister(0x2100, 0x80);
        writeColor(ppu, 0, 0x001F);
        writeColor(ppu, 1, 0x03E0);
        ppu.writeRegister(0x2132, 0x9F);
        ppu.writeRegister(0x2105, 0x00);
        ppu.writeRegister(0x2107, 0x00);
        ppu.writeRegister(0x210B, 0x01);
        ppu.writeRegister(0x2115, 0x80);
        ppu.writeRegister(0x2116, 0x00);
        ppu.writeRegister(0x2117, 0x00);
        ppu.writeRegister(0x2118, 0x00);
        ppu.writeRegister(0x2119, 0x00);
        fillTile2bpp(ppu, 0x1000, 1);
        ppu.writeRegister(0x212C, 0x01);
        ppu.writeRegister(0x212D, 0x00);
        ppu.writeRegister(0x2130, 0x00);
        ppu.writeRegister(0x2131, 0x01);
        ppu.writeRegister(0x2133, 0x08);
        ppu.writeRegister(0x2100, 0x0F);
        while (ppu.scanline() < 1) ppu.tick();
        for (int i = 0; i < PPU::VISIBLE_DOT_START + 2; ++i) ppu.tick();
        const uint32_t* fb = ppu.framebuffer();
        assert(fb[0] == expectedRgba(0x0000));
        assert(fb[1] == expectedRgba(0x7FE0));
        assert(fb[2] == expectedRgba(0x7C1F));
        assert(fb[3] == expectedRgba(0x7FE0));
    }
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.writeRegister(0x2100, 0x80);
        writeColor(ppu, 0, 0x03E0);
        ppu.writeRegister(0x2105, 0x05);
        ppu.writeRegister(0x2133, 0x00);
        assert(ppu.hiresOutputEnabled());
        assert(ppu.width() == 512);
        ppu.writeRegister(0x2100, 0x0F);
        while (ppu.scanline() < 1) ppu.tick();
        for (int i = 0; i < PPU::VISIBLE_DOT_START + 4; ++i)
            ppu.tick();
        const uint32_t* fb = ppu.framebuffer();
        const uint32_t green = expectedRgba(0x03E0);
        assert(fb[0] == expectedRgba(0x0000));
        assert(fb[1] == green);
    }
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.writeRegister(0x2100, 0x80);
        writeColor(ppu, 0, 0x001F);
        ppu.writeRegister(0x2133, 0x00);
        ppu.writeRegister(0x2105, 0x00);
        assert(!ppu.hiresOutputEnabled());
        assert(ppu.width() == 256);
        assert(ppu.framebufferStride() == 512);
        ppu.writeRegister(0x2100, 0x0F);
        while (ppu.scanline() < 1) ppu.tick();
        for (int i = 0; i < PPU::VISIBLE_DOT_START + 4; ++i)
            ppu.tick();
        assert(ppu.framebuffer()[0] == expectedRgba(0x001F));
    }
    std::cout << "phase49 PPU pseudo-hires accuracy ok\n";
    return 0;
}

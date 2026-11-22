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
static void advanceToDot(PPU& ppu, int targetDot) {
    while (ppu.dot() < targetDot)
        ppu.tick();
    assert(ppu.dot() == targetDot);
}
int main() {
    {
        assert(PPU::screenXFromDot(21) == -1);
        assert(PPU::screenXFromDot(22) == 0);
        assert(PPU::screenXFromDot(23) == 1);
        assert(PPU::screenXFromDot(277) == 255);
        assert(PPU::screenXFromDot(278) == -1);
        assert(PPU::screenXFromDot(0) == -1);
        assert(PPU::screenXFromDot(339) == -1);
    }
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.writeRegister(0x2100, 0x80);
        writeColor(ppu, 0, 0x001F);
        ppu.writeRegister(0x2100, 0x0F);
        while (ppu.scanline() < 1) ppu.tick();
        advanceToDot(ppu, PPU::VISIBLE_DOT_START + 64);
        assert(ppu.scanline() == 1);
        ppu.writeRegister(0x2100, 0x80);
        advanceToDot(ppu, PPU::VISIBLE_DOT_START + 128);
        const uint32_t* fb = ppu.framebuffer();
        const uint32_t red = expectedRgba(0x001F);
        const uint32_t black = 0xFF000000u;
        assert(fb[0] == red);
        assert(fb[32] == red);
        assert(fb[63] == red);
        assert(fb[64] == black);
        assert(fb[100] == black);
        assert(fb[127] == black);
    }
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.writeRegister(0x2100, 0x80);
        writeColor(ppu, 0, 0x001F);
        ppu.writeRegister(0x2100, 0x0F);
        while (ppu.scanline() < 1) ppu.tick();
        advanceToDot(ppu, PPU::VISIBLE_DOT_START + 80);
        ppu.writeRegister(0x2121, 5);
        ppu.writeRegister(0x2122, 0x00);
        ppu.writeRegister(0x2122, 0x7C);
        assert(ppu.cgramColor(0) == 0x7C00);
        advanceToDot(ppu, PPU::VISIBLE_DOT_START + 128);
        const uint32_t* fb = ppu.framebuffer();
        const uint32_t red = expectedRgba(0x001F);
        const uint32_t blue = expectedRgba(0x7C00);
        assert(fb[0] == red);
        assert(fb[79] == red);
        assert(fb[80] == blue);
        assert(fb[127] == blue);
    }
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.writeRegister(0x2100, 0x80);
        writeColor(ppu, 0, 0x001F);
        writeColor(ppu, 7, 0x03E0);
        ppu.writeRegister(0x2121, 7);
        ppu.writeRegister(0x2100, 0x0F);
        advanceToDot(ppu, 10);
        ppu.writeRegister(0x2122, 0xE0);
        ppu.writeRegister(0x2122, 0x03);
        assert(ppu.cgramColor(7) == 0x03E0);
        assert(ppu.cgramColor(0) == 0x001F);
        advanceToDot(ppu, 40);
        ppu.writeRegister(0x2121, 7);
        ppu.writeRegister(0x2122, 0x00);
        ppu.writeRegister(0x2122, 0x7C);
        assert(ppu.cgramColor(0) == 0x7C00);
        assert(ppu.cgramColor(7) == 0x03E0);
    }
    std::cout << "phase41/42 PPU per-dot + visible-window accuracy ok\n";
    return 0;
}

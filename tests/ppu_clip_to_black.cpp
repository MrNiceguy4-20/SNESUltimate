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
static void setVramWord(PPU& ppu, uint16_t byteAddress, uint16_t value) {
    const uint16_t word = static_cast<uint16_t>(byteAddress >> 1);
    ppu.writeRegister(0x2115, 0x80);
    ppu.writeRegister(0x2116, static_cast<uint8_t>(word));
    ppu.writeRegister(0x2117, static_cast<uint8_t>(word >> 8));
    ppu.writeRegister(0x2118, static_cast<uint8_t>(value));
    ppu.writeRegister(0x2119, static_cast<uint8_t>(value >> 8));
}
static void setupSolidObj(PPU& ppu) {
    for (int row = 0; row < 8; ++row)
        setVramWord(ppu, static_cast<uint16_t>(row * 2), 0x00FF);
    for (int row = 0; row < 8; ++row)
        setVramWord(ppu, static_cast<uint16_t>(16 + row * 2), 0x0000);
    ppu.writeRegister(0x2101, 0x00);
    ppu.writeRegister(0x2102, 0x00);
    ppu.writeRegister(0x2103, 0x00);
    ppu.writeRegister(0x2104, 0x00);
    ppu.writeRegister(0x2104, 0x00);
    ppu.writeRegister(0x2104, 0x00);
    ppu.writeRegister(0x2104, 0x00);
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
        ppu.writeRegister(0x2130, 0xC0);
        ppu.writeRegister(0x2131, 0x00);
        ppu.writeRegister(0x2100, 0x0F);
        while (ppu.scanline() < 1) ppu.tick();
        for (int i = 0; i < PPU::VISIBLE_DOT_START + 8; ++i)
            ppu.tick();
        const uint32_t* fb = ppu.framebuffer();
        assert(fb[0] == 0xFF000000u);
        assert(fb[4] == 0xFF000000u);
    }
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.writeRegister(0x2100, 0x80);
        writeColor(ppu, 0, 0x001F);
        ppu.writeRegister(0x2130, 0x00);
        ppu.writeRegister(0x2131, 0x00);
        ppu.writeRegister(0x2100, 0x0F);
        while (ppu.scanline() < 1) ppu.tick();
        for (int i = 0; i < PPU::VISIBLE_DOT_START + 8; ++i)
            ppu.tick();
        const uint32_t* fb = ppu.framebuffer();
        const uint32_t red = expectedRgba(0x001F);
        assert(fb[0] == red);
    }
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.writeRegister(0x2100, 0x80);
        writeColor(ppu, 0, 0x001F);
        ppu.writeRegister(0x2130, 0xC0);
        ppu.writeRegister(0x2131, 0x20);
        ppu.writeRegister(0x2132, 0x80 | 0x1F);
        ppu.writeRegister(0x2132, static_cast<uint8_t>(0x80 | 31));
        ppu.writeRegister(0x2100, 0x0F);
        while (ppu.scanline() < 1) ppu.tick();
        for (int i = 0; i < PPU::VISIBLE_DOT_START + 4; ++i)
            ppu.tick();
        const uint32_t* fb = ppu.framebuffer();
        assert((fb[0] & 0x00FF0000) != 0 || (fb[0] & 0x0000FF00) != 0 || (fb[0] & 0x000000FF) != 0);
    }
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.writeRegister(0x2100, 0x80);
        writeColor(ppu, 0, 0x001F);
        ppu.writeRegister(0x2130, 0xC0);
        ppu.writeRegister(0x2131, 0x60);
        ppu.writeRegister(0x2132, static_cast<uint8_t>(0x80 | 31));
        ppu.writeRegister(0x2100, 0x0F);
        while (ppu.scanline() < 1) ppu.tick();
        for (int i = 0; i <= PPU::VISIBLE_DOT_START; ++i) ppu.tick();
        assert(ppu.framebuffer()[0] == expectedRgba(0x7C00));
    }
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.writeRegister(0x2100, 0x80);
        writeColor(ppu, 0, 0x001F);
        ppu.writeRegister(0x212D, 0x00);
        ppu.writeRegister(0x2130, 0x02);
        ppu.writeRegister(0x2131, 0x60);
        ppu.writeRegister(0x2132, static_cast<uint8_t>(0x80 | 31));
        ppu.writeRegister(0x2100, 0x0F);
        while (ppu.scanline() < 1) ppu.tick();
        for (int i = 0; i <= PPU::VISIBLE_DOT_START; ++i) ppu.tick();
        assert(ppu.framebuffer()[0] == expectedRgba(0x7C1F));
    }
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.writeRegister(0x2100, 0x80);
        setupSolidObj(ppu);
        writeColor(ppu, 0, 0x001F);
        writeColor(ppu, 129, 0x03E0);
        ppu.writeRegister(0x212C, 0x00);
        ppu.writeRegister(0x212D, 0x10);
        ppu.writeRegister(0x2130, 0x02);
        ppu.writeRegister(0x2131, 0x60);
        ppu.writeRegister(0x2132, static_cast<uint8_t>(0x80 | 31));
        ppu.writeRegister(0x2100, 0x0F);
        while (ppu.scanline() < 1) ppu.tick();
        for (int i = 0; i <= PPU::VISIBLE_DOT_START; ++i) ppu.tick();
        assert(ppu.framebuffer()[0] == expectedRgba(static_cast<uint16_t>(15 | (15 << 5))));
    }
    for (int preventMode = 1; preventMode <= 2; ++preventMode) {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.writeRegister(0x2100, 0x80);
        writeColor(ppu, 0, 0x001F);
        ppu.writeRegister(0x2125, 0x20);
        ppu.writeRegister(0x2126, 0);
        ppu.writeRegister(0x2127, 127);
        ppu.writeRegister(0x2130, static_cast<uint8_t>(preventMode << 4));
        ppu.writeRegister(0x2131, 0x20);
        ppu.writeRegister(0x2132, static_cast<uint8_t>(0x80 | 31));
        ppu.writeRegister(0x2100, 0x0F);
        while (ppu.scanline() < 1) ppu.tick();
        for (int i = 0; i <= PPU::VISIBLE_DOT_START + 200; ++i) ppu.tick();
        const uint32_t red = expectedRgba(0x001F);
        const uint32_t magenta = expectedRgba(0x7C1F);
        if (preventMode == 1) {
            assert(ppu.framebuffer()[0] == magenta);
            assert(ppu.framebuffer()[200] == red);
        } else {
            assert(ppu.framebuffer()[0] == red);
            assert(ppu.framebuffer()[200] == magenta);
        }
    }
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.writeRegister(0x2100, 0x80);
        writeColor(ppu, 0, 0x7FFF);
        ppu.writeRegister(0x2130, 0x00);
        ppu.writeRegister(0x2131, 0x60);
        ppu.writeRegister(0x2132, static_cast<uint8_t>(0xE0 | 31));
        ppu.writeRegister(0x2100, 0x0F);
        while (ppu.scanline() < 1) ppu.tick();
        for (int i = 0; i <= PPU::VISIBLE_DOT_START; ++i) ppu.tick();
        assert(ppu.framebuffer()[0] == expectedRgba(0x7FFF));
    }
    std::cout << "phase47 PPU clip-to-black accuracy ok\n";
    return 0;
}

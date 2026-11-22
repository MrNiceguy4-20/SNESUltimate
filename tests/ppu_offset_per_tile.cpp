#include "bus.hpp"
#include "ppu.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
static void writeColor(PPU& ppu, uint8_t index, uint16_t color) {
    ppu.writeRegister(0x2121, index);
    ppu.writeRegister(0x2122, static_cast<uint8_t>(color & 0xFF));
    ppu.writeRegister(0x2122, static_cast<uint8_t>((color >> 8) & 0x7F));
}
static void pokeVramWord(PPU& ppu, uint16_t wordAddr, uint16_t value) {
    ppu.writeRegister(0x2115, 0x80);
    ppu.writeRegister(0x2116, static_cast<uint8_t>(wordAddr & 0xFF));
    ppu.writeRegister(0x2117, static_cast<uint8_t>((wordAddr >> 8) & 0xFF));
    ppu.writeRegister(0x2118, static_cast<uint8_t>(value & 0xFF));
    ppu.writeRegister(0x2119, static_cast<uint8_t>((value >> 8) & 0xFF));
}
static void setHorizontalScroll(PPU& ppu, uint16_t reg, uint16_t value) {
    ppu.writeRegister(reg, static_cast<uint8_t>(value & 0xFF));
    ppu.writeRegister(reg, static_cast<uint8_t>((value >> 8) & 0x03));
}
static std::array<uint32_t, 24> renderMode2FineScrollOptScene(bool enableOffset, bool tile16 = false) {
    Bus bus;
    PPU ppu;
    ppu.attachBus(&bus);
    bus.attachPPU(&ppu);
    ppu.writeRegister(0x2100, 0x80);
    ppu.writeRegister(0x2105, static_cast<uint8_t>(0x02 | (tile16 ? 0x10 : 0x00)));
    ppu.writeRegister(0x2107, 0x00);
    ppu.writeRegister(0x2109, 0x04);
    ppu.writeRegister(0x210B, 0x01);
    ppu.writeRegister(0x212C, 0x01);
    writeColor(ppu, 0, 0x0000);
    writeColor(ppu, 1, 0x7FFF);
    writeColor(ppu, 2, 0x001F);
    for (int x = 0; x < 32; ++x) {
        const uint16_t tile = tile16
            ? static_cast<uint16_t>(x >= 2 ? 0x20 : 0x00)
            : static_cast<uint16_t>(x & 1);
        pokeVramWord(ppu, static_cast<uint16_t>(x), tile);
    }
    for (int tile = 0; tile < 64; ++tile) {
        uint16_t planeWord = 0x00FF;
        if (tile16) {
            if (tile == 1) planeWord = 0xFF00;
        } else {
            planeWord = (tile & 1) ? 0xFF00 : 0x00FF;
        }
        for (int row = 0; row < 8; ++row)
            pokeVramWord(ppu, static_cast<uint16_t>(0x1000 + tile * 0x10 + row), planeWord);
    }
    for (int x = 0; x < 8; ++x)
        pokeVramWord(ppu, static_cast<uint16_t>(0x0400 + x),
                     enableOffset ? static_cast<uint16_t>(0x2000 | 24) : 0);
    for (int x = 0; x < 8; ++x)
        pokeVramWord(ppu, static_cast<uint16_t>(0x0420 + x), 0);
    setHorizontalScroll(ppu, 0x210D, 7);
    setHorizontalScroll(ppu, 0x2111, 0);
    ppu.writeRegister(0x2112, 0);
    ppu.writeRegister(0x2112, 0);
    ppu.writeRegister(0x2100, 0x0F);
    while (ppu.scanline() < 1) ppu.tick();
    for (int i = 0; i < PPU::VISIBLE_DOT_START + 40; ++i)
        ppu.tick();
    std::array<uint32_t, 24> result{};
    for (int x = 0; x < 24; ++x)
        result[static_cast<size_t>(x)] = ppu.framebuffer()[x] & 0x00FFFFFFu;
    return result;
}
int main() {
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.writeRegister(0x2100, 0x80);
        ppu.writeRegister(0x2105, 0x02);
        assert(ppu.bgMode() == 2);
        ppu.writeRegister(0x2107, 0x00);
        ppu.writeRegister(0x2109, 0x04);
        ppu.writeRegister(0x210B, 0x01);
        ppu.writeRegister(0x212C, 0x01);
        writeColor(ppu, 0, 0x0000);
        writeColor(ppu, 1, 0x7FFF);
        pokeVramWord(ppu, 0x0000, 0x0000);
        ppu.writeRegister(0x2115, 0x80);
        ppu.writeRegister(0x2116, 0x00);
        ppu.writeRegister(0x2117, 0x10);
        for (int i = 0; i < 8; ++i) {
            ppu.writeRegister(0x2118, 0xFF);
            ppu.writeRegister(0x2119, 0x00);
        }
        pokeVramWord(ppu, 0x0800, static_cast<uint16_t>(0x2000 | 8));
        ppu.writeRegister(0x2100, 0x0F);
        while (ppu.scanline() < 1) ppu.tick();
        for (int i = 0; i < PPU::VISIBLE_DOT_START + 32; ++i)
            ppu.tick();
        const uint32_t* fb = ppu.framebuffer();
        bool any = false;
        for (int x = 0; x < 32; ++x)
            if ((fb[x] & 0x00FFFFFF) != 0) any = true;
        assert(any);
    }
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.writeRegister(0x2100, 0x80);
        ppu.writeRegister(0x2105, 0x12);
        ppu.writeRegister(0x2107, 0x00);
        ppu.writeRegister(0x2109, 0x04);
        ppu.writeRegister(0x210B, 0x01);
        ppu.writeRegister(0x212C, 0x01);
        writeColor(ppu, 0, 0x0000);
        writeColor(ppu, 1, 0x7FFF);
        writeColor(ppu, 2, 0x001F);
        for (int tile = 0; tile < 4; ++tile) {
            const uint16_t planeWord = tile < 2 ? 0x00FF : 0xFF00;
            for (int row = 0; row < 8; ++row)
                pokeVramWord(ppu, static_cast<uint16_t>(0x1000 + tile * 0x10 + row), planeWord);
        }
        pokeVramWord(ppu, 0x0000, 0x0000);
        pokeVramWord(ppu, 0x0001, 0x0002);
        pokeVramWord(ppu, 0x0800, static_cast<uint16_t>(0x2000 | 16));
        ppu.writeRegister(0x2100, 0x0F);
        while (ppu.scanline() < 1) ppu.tick();
        for (int i = 0; i < PPU::VISIBLE_DOT_START + 24; ++i) ppu.tick();
        const uint32_t c = ppu.framebuffer()[8] & 0x00FFFFFFu;
        const uint8_t r = static_cast<uint8_t>((c >> 16) & 0xFF);
        const uint8_t g = static_cast<uint8_t>((c >> 8) & 0xFF);
        const uint8_t b = static_cast<uint8_t>(c & 0xFF);
        assert(c != 0u);
        assert(r == g && g == b);
    }
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.writeRegister(0x2100, 0x80);
        ppu.writeRegister(0x2105, 0x42);
        ppu.writeRegister(0x2107, 0x00);
        ppu.writeRegister(0x2109, 0x04);
        ppu.writeRegister(0x210B, 0x01);
        ppu.writeRegister(0x212C, 0x01);
        writeColor(ppu, 0, 0x0000);
        writeColor(ppu, 1, 0x7FFF);
        writeColor(ppu, 2, 0x001F);
        for (int row = 0; row < 8; ++row) {
            pokeVramWord(ppu, static_cast<uint16_t>(0x1000 + row), 0x00FF);
            pokeVramWord(ppu, static_cast<uint16_t>(0x1010 + row), 0xFF00);
        }
        for (int row = 8; row < 16; ++row) {
            pokeVramWord(ppu, static_cast<uint16_t>(0x1000 + row), 0x0000);
            pokeVramWord(ppu, static_cast<uint16_t>(0x1010 + row), 0x0000);
        }
        for (int x = 0; x < 8; ++x)
            pokeVramWord(ppu, static_cast<uint16_t>(x), static_cast<uint16_t>(x == 6 ? 1 : 0));
        pokeVramWord(ppu, 0x0800, 0x2000 | 0);
        pokeVramWord(ppu, 0x0801, 0x2000 | 32);
        ppu.writeRegister(0x2100, 0x0F);
        while (ppu.scanline() < 1) ppu.tick();
        for (int i = 0; i < PPU::VISIBLE_DOT_START + 32; ++i)
            ppu.tick();
        const uint32_t* fb = ppu.framebuffer();
        const uint32_t x16 = fb[16] & 0x00FFFFFFu;
        assert(x16 != 0u);
        const uint8_t r = static_cast<uint8_t>((x16 >> 16) & 0xFF);
        const uint8_t g = static_cast<uint8_t>((x16 >> 8) & 0xFF);
        const uint8_t b = static_cast<uint8_t>(x16 & 0xFF);
        assert(r == g && g == b);
    }
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.writeRegister(0x2100, 0x80);
        ppu.writeRegister(0x2105, 0x04);
        assert(ppu.bgMode() == 4);
        ppu.writeRegister(0x2105, 0x06);
        assert(ppu.bgMode() == 6);
    }
    {
        const auto baseline = renderMode2FineScrollOptScene(false);
        const auto withOffset = renderMode2FineScrollOptScene(true);
        assert(withOffset[0] == baseline[0]);
        assert(withOffset[1] != baseline[1]);
    }
    {
        const auto baseline = renderMode2FineScrollOptScene(false, true);
        const auto withOffset = renderMode2FineScrollOptScene(true, true);
        assert(withOffset[0] == baseline[0]);
        assert(withOffset[1] != baseline[1]);
    }
    std::cout << "phase46 PPU offset-per-tile accuracy ok\n";
    return 0;
}

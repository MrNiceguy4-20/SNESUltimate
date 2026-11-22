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
static void writeSprite(PPU& ppu, uint8_t x, uint8_t y) {
    ppu.writeRegister(0x2104, x);
    ppu.writeRegister(0x2104, y);
    ppu.writeRegister(0x2104, 0);
    ppu.writeRegister(0x2104, 0);
}
int main() {
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.writeRegister(0x2100, 0x80);
        ppu.writeRegister(0x2101, 0xA0);
        ppu.writeRegister(0x2102, 0x00);
        ppu.writeRegister(0x2103, 0x00);
        for (int i = 0; i < 33; ++i) writeSprite(ppu, 0, 0);
        ppu.writeRegister(0x2100, 0x0F);
        while (ppu.dot() < 65) ppu.tick();
        assert(!ppu.objRangeOver());
        assert(!ppu.objTimeOver());
        ppu.tick();
        assert(ppu.objRangeOver());
        assert(!ppu.objTimeOver());
        while (ppu.dot() < PPU::VISIBLE_DOT_END) ppu.tick();
        while (!ppu.objTimeOver()) ppu.tick();
        assert(ppu.objTimeOver());
    }
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.writeRegister(0x2100, 0x80);
        writeColor(ppu, 0, 0x001F);
        writeColor(ppu, 5, 0x03E0);
        ppu.writeRegister(0x2121, 5);
        ppu.writeRegister(0x2100, 0x0F);
        for (int i = 0; i < PPU::VISIBLE_DOT_START + 8; ++i) ppu.tick();
        assert(ppu.dot() == PPU::VISIBLE_DOT_START + 8);
        assert(PPU::screenXFromDot(ppu.dot()) >= 0);
        assert(!ppu.hblank());
        ppu.writeRegister(0x2122, 0x00);
        ppu.writeRegister(0x2122, 0x7C);
        assert(ppu.cgramColor(0) == 0x7C00);
        assert(ppu.cgramColor(5) == 0x03E0);
        ppu.writeRegister(0x2121, 5);
        const uint8_t lo = ppu.readRegister(0x213B);
        const uint8_t hi = ppu.readRegister(0x213B);
        assert(lo == 0x00);
        assert((hi & 0x7F) == 0x7C);
    }
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.writeRegister(0x2100, 0x80);
        writeColor(ppu, 0, 0x001F);
        writeColor(ppu, 5, 0x03E0);
        ppu.writeRegister(0x2121, 5);
        ppu.writeRegister(0x2100, 0x0F);
        while (ppu.dot() < PPU::HBLANK_START_DOT) ppu.tick();
        assert(ppu.dot() == PPU::HBLANK_START_DOT);
        assert(ppu.hblank());
        assert(PPU::screenXFromDot(ppu.dot()) >= 0);
        ppu.writeRegister(0x2122, 0x00);
        ppu.writeRegister(0x2122, 0x7C);
        assert(ppu.cgramColor(0) == 0x001F);
        assert(ppu.cgramColor(5) == 0x7C00);
    }
    std::cout << "phase39 PPU pipeline/CGRAM accuracy ok\n";
    return 0;
}

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
int main() {
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.writeRegister(0x2100, 0x80);
        ppu.writeRegister(0x2106, 0x31);
        ppu.writeRegister(0x2105, 0x07);
        ppu.writeRegister(0x212C, 0x01);
        writeColor(ppu, 0, 0x0000);
        writeColor(ppu, 1, 0x001F);
        auto writeM7 = [&](uint16_t reg, uint16_t v) {
            ppu.writeRegister(reg, static_cast<uint8_t>(v & 0xFF));
            ppu.writeRegister(reg, static_cast<uint8_t>((v >> 8) & 0xFF));
        };
        writeM7(0x211B, 0x0100);
        writeM7(0x211C, 0x0000);
        writeM7(0x211D, 0x0000);
        writeM7(0x211E, 0x0100);
        writeM7(0x211F, 0x0000);
        writeM7(0x2120, 0x0000);
        ppu.writeRegister(0x2100, 0x0F);
        for (int i = 0; i < PPU::VISIBLE_DOT_START + 16; ++i)
            ppu.tick();
        assert(ppu.bgMode() == 7);
        (void)ppu.framebuffer()[0];
    }
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.writeRegister(0x2100, 0x80);
        ppu.writeRegister(0x2105, 0x07);
        ppu.writeRegister(0x2133, 0x40);
        ppu.writeRegister(0x2106, 0x12);
        ppu.writeRegister(0x212C, 0x02);
        writeColor(ppu, 1, 0x001F);
        writeColor(ppu, 2, 0x03E0);
        auto writeM7 = [&](uint16_t reg, uint16_t v) {
            ppu.writeRegister(reg, static_cast<uint8_t>(v & 0xFF));
            ppu.writeRegister(reg, static_cast<uint8_t>(v >> 8));
        };
        writeM7(0x211B, 0x0100); writeM7(0x211C, 0);
        writeM7(0x211D, 0); writeM7(0x211E, 0x0100);
        writeM7(0x211F, 0); writeM7(0x2120, 0);
        ppu.writeRegister(0x2115, 0x80);
        ppu.writeRegister(0x2116, 0); ppu.writeRegister(0x2117, 0);
        ppu.writeRegister(0x2118, 0); ppu.writeRegister(0x2119, 1);
        ppu.writeRegister(0x2118, 0); ppu.writeRegister(0x2119, 2);
        ppu.writeRegister(0x2100, 0x0F);
        for (int i = 0; i <= PPU::VISIBLE_DOT_START + 1; ++i) ppu.tick();
        assert(ppu.framebuffer()[0] == ppu.framebuffer()[1]);
    }
    {
        PPU ppu;
        ppu.writeRegister(0x2106, 0xF0);
        ppu.writeRegister(0x2106, 0x1F);
        ppu.writeRegister(0x2105, 0x01);
        assert(ppu.bgMode() == 1);
    }
    std::cout << "phase53 PPU mosaic Mode 7 accuracy ok\n";
    return 0;
}

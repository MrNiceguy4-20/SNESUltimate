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
static void writeM7(PPU& ppu, uint16_t reg, uint16_t value) {
    ppu.writeRegister(reg, static_cast<uint8_t>(value));
    ppu.writeRegister(reg, static_cast<uint8_t>(value >> 8));
}
static uint32_t renderPixel(uint8_t raw, uint8_t mainMask, bool directColor) {
    Bus bus;
    PPU ppu;
    ppu.attachBus(&bus);
    bus.attachPPU(&ppu);
    ppu.writeRegister(0x2100, 0x80);
    ppu.writeRegister(0x2105, 0x07);
    ppu.writeRegister(0x2133, 0x40);
    ppu.writeRegister(0x212C, mainMask);
    ppu.writeRegister(0x212D, 0x00);
    ppu.writeRegister(0x2130, directColor ? 0x01 : 0x00);
    writeM7(ppu, 0x211B, 0x0100);
    writeM7(ppu, 0x211C, 0x0000);
    writeM7(ppu, 0x211D, 0x0000);
    writeM7(ppu, 0x211E, 0x0100);
    writeM7(ppu, 0x211F, 0x0000);
    writeM7(ppu, 0x2120, 0x0000);
    writeM7(ppu, 0x210E, 0x1FFF);
    writeColor(ppu, 1, 0x03E0);
    writeColor(ppu, 0x81, 0x001F);
    ppu.writeRegister(0x2115, 0x80);
    ppu.writeRegister(0x2116, 0x00);
    ppu.writeRegister(0x2117, 0x00);
    ppu.writeRegister(0x2118, 0x00);
    ppu.writeRegister(0x2119, raw);
    ppu.writeRegister(0x2100, 0x0F);
    while (ppu.scanline() < 1) ppu.tick();
    for (int i = 0; i <= PPU::VISIBLE_DOT_START; ++i) ppu.tick();
    return ppu.framebuffer()[0];
}
int main() {
    assert(renderPixel(0x81, 0x03, false) == 0xFF00FF00u);
    assert(renderPixel(0x81, 0x01, false) == 0xFF0000FFu);
    assert(renderPixel(0x01, 0x02, false) == 0xFF00FF00u);
    assert(renderPixel(0x81, 0x02, true) == 0xFF00FF00u);
    std::cout << "PPU Mode 7 EXTBG duplicate-layer/direct-color accuracy ok\n";
    return 0;
}

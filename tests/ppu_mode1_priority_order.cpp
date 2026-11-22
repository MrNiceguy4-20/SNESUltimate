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
static void setupSolidTileAndBg(PPU& ppu, bool highPriority) {
    for (int row = 0; row < 8; ++row)
        setVramWord(ppu, static_cast<uint16_t>(row * 2), 0x00FF);
    for (int row = 0; row < 8; ++row)
        setVramWord(ppu, static_cast<uint16_t>(16 + row * 2), 0x0000);
    setVramWord(ppu, 0x1000, highPriority ? 0x2000 : 0x0000);
    ppu.writeRegister(0x2107, 0x08);
    ppu.writeRegister(0x210B, 0x00);
}
static void setupSprite(PPU& ppu, uint8_t priority) {
    ppu.writeRegister(0x2101, 0x00);
    ppu.writeRegister(0x2102, 0x00);
    ppu.writeRegister(0x2103, 0x00);
    ppu.writeRegister(0x2104, 0x00);
    ppu.writeRegister(0x2104, 0x00);
    ppu.writeRegister(0x2104, 0x00);
    ppu.writeRegister(0x2104, static_cast<uint8_t>((priority & 3) << 4));
}
static uint32_t renderFirstPixel(uint8_t bgPriority, uint8_t objPriority) {
    Bus bus;
    PPU ppu;
    ppu.attachBus(&bus);
    bus.attachPPU(&ppu);
    ppu.writeRegister(0x2100, 0x80);
    ppu.writeRegister(0x2105, 0x01);
    setupSolidTileAndBg(ppu, bgPriority != 0);
    setupSprite(ppu, objPriority);
    writeColor(ppu, 1, 0x001F);
    writeColor(ppu, 129, 0x03E0);
    ppu.writeRegister(0x212C, 0x11);
    ppu.writeRegister(0x2100, 0x0F);
    while (ppu.scanline() < 1) ppu.tick();
    for (int i = 0; i <= PPU::VISIBLE_DOT_START; ++i) ppu.tick();
    return ppu.framebuffer()[0];
}
static uint32_t renderBg3LowVsObj0WithPriorityBit() {
    Bus bus;
    PPU ppu;
    ppu.attachBus(&bus);
    bus.attachPPU(&ppu);
    ppu.writeRegister(0x2100, 0x80);
    ppu.writeRegister(0x2105, 0x09);
    for (int row = 0; row < 8; ++row)
        setVramWord(ppu, static_cast<uint16_t>(0x0010 + row * 2), 0x00FF);
    setVramWord(ppu, 0x1000, 0x0001);
    ppu.writeRegister(0x2109, 0x08);
    ppu.writeRegister(0x210C, 0x00);
    for (int row = 0; row < 8; ++row)
        setVramWord(ppu, static_cast<uint16_t>(row * 2), 0x00FF);
    for (int row = 0; row < 8; ++row)
        setVramWord(ppu, static_cast<uint16_t>(16 + row * 2), 0x0000);
    setupSprite(ppu, 0);
    writeColor(ppu, 1, 0x001F);
    writeColor(ppu, 129, 0x03E0);
    ppu.writeRegister(0x212C, 0x14);
    ppu.writeRegister(0x2100, 0x0F);
    while (ppu.scanline() < 1) ppu.tick();
    for (int i = 0; i <= PPU::VISIBLE_DOT_START; ++i) ppu.tick();
    return ppu.framebuffer()[0];
}
int main() {
    assert(renderFirstPixel(0, 1) == 0xFF0000FFu);
    assert(renderFirstPixel(0, 2) == 0xFF00FF00u);
    assert(renderBg3LowVsObj0WithPriorityBit() == 0xFF00FF00u);
    assert(renderFirstPixel(1, 2) == 0xFF0000FFu);
    assert(renderFirstPixel(1, 3) == 0xFF00FF00u);
    std::cout << "PPU Mode 1 BG/OBJ priority ordering ok\n";
    return 0;
}

#include "bus.hpp"
#include "ppu.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
static void setVramWord(PPU& ppu, uint16_t byteAddress, uint16_t value) {
    const uint16_t word = static_cast<uint16_t>(byteAddress >> 1);
    ppu.writeRegister(0x2115, 0x80);
    ppu.writeRegister(0x2116, static_cast<uint8_t>(word));
    ppu.writeRegister(0x2117, static_cast<uint8_t>(word >> 8));
    ppu.writeRegister(0x2118, static_cast<uint8_t>(value));
    ppu.writeRegister(0x2119, static_cast<uint8_t>(value >> 8));
}
static uint32_t renderMode3DirectColorWithLargeBg1() {
    Bus bus;
    PPU ppu;
    ppu.attachBus(&bus);
    bus.attachPPU(&ppu);
    ppu.writeRegister(0x2100, 0x80);
    ppu.writeRegister(0x2105, 0x13);
    ppu.writeRegister(0x2107, 0x08);
    ppu.writeRegister(0x210B, 0x00);
    setVramWord(ppu, 0x0000, 0x0080);
    setVramWord(ppu, 0x1000, 0x1000);
    ppu.writeRegister(0x210E, 0xFF);
    ppu.writeRegister(0x210E, 0x03);
    ppu.writeRegister(0x2130, 0x01);
    ppu.writeRegister(0x212C, 0x01);
    ppu.writeRegister(0x2100, 0x0F);
    while (ppu.scanline() < 1) ppu.tick();
    for (int i = 0; i <= PPU::VISIBLE_DOT_START; ++i) ppu.tick();
    return ppu.framebuffer()[0];
}
int main() {
    assert(renderMode3DirectColorWithLargeBg1() == 0xFF210021u);
    std::cout << "PPU direct-color mode masking/blue-bit accuracy ok\n";
    return 0;
}

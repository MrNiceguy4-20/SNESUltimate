#include "bus.hpp"
#include "ppu.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
static void setVram(PPU& ppu, uint16_t word) {
    ppu.writeRegister(0x2115, 0x80);
    ppu.writeRegister(0x2116, static_cast<uint8_t>(word));
    ppu.writeRegister(0x2117, static_cast<uint8_t>(word >> 8));
}
static void writeColor(PPU& ppu, uint8_t index, uint16_t color) {
    ppu.writeRegister(0x2121, index);
    ppu.writeRegister(0x2122, static_cast<uint8_t>(color & 0xFF));
    ppu.writeRegister(0x2122, static_cast<uint8_t>(color >> 8));
}
int main() {
    Bus bus;
    PPU ppu;
    ppu.attachBus(&bus);
    bus.attachPPU(&ppu);
    ppu.writeRegister(0x2100, 0x80);
    setVram(ppu, 0x0000);
    ppu.writeRegister(0x2118, 0x00);
    ppu.writeRegister(0x2119, 0x00);
    setVram(ppu, 0x1000);
    const uint8_t rowColors[8] = {1, 2, 3, 1, 2, 3, 1, 2};
    for (int row = 0; row < 8; ++row) {
        const uint8_t c = rowColors[row];
        ppu.writeRegister(0x2118, (c & 1) ? 0xFF : 0x00);
        ppu.writeRegister(0x2119, (c & 2) ? 0xFF : 0x00);
    }
    writeColor(ppu, 1, 0x001F);
    writeColor(ppu, 2, 0x03E0);
    writeColor(ppu, 3, 0x7C00);
    ppu.writeRegister(0x2105, 0x00);
    ppu.writeRegister(0x2107, 0x00);
    ppu.writeRegister(0x210B, 0x01);
    ppu.writeRegister(0x210E, 0xFF);
    ppu.writeRegister(0x210E, 0x03);
    ppu.writeRegister(0x2106, 0x11);
    ppu.writeRegister(0x212C, 0x01);
    ppu.writeRegister(0x2100, 0x0F);
    while (ppu.scanline() < 5) ppu.tick();
    const uint32_t* fb = ppu.framebuffer();
    const size_t stride = static_cast<size_t>(ppu.framebufferStride());
    assert(fb[0] != 0xFF000000u);
    assert(fb[0] == fb[stride]);
    assert(fb[2 * stride] == fb[3 * stride]);
    assert(fb[0] != fb[2 * stride]);
    std::cout << "PPU vertical mosaic block alignment ok\n";
    return 0;
}

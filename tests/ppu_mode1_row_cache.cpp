#include "ppu.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
namespace {
void setVmadd(PPU& ppu, uint16_t word) {
    ppu.writeRegister(0x2116, static_cast<uint8_t>(word));
    ppu.writeRegister(0x2117, static_cast<uint8_t>(word >> 8));
}
void writeWord(PPU& ppu, uint16_t value) {
    ppu.writeRegister(0x2118, static_cast<uint8_t>(value));
    ppu.writeRegister(0x2119, static_cast<uint8_t>(value >> 8));
}
void writeSolid4bppTile(PPU& ppu, uint16_t wordBase, uint8_t pixel) {
    setVmadd(ppu, wordBase);
    const uint16_t lowPlanes = static_cast<uint16_t>(((pixel & 1) ? 0x00FF : 0) |
                                                     ((pixel & 2) ? 0xFF00 : 0));
    const uint16_t highPlanes = static_cast<uint16_t>(((pixel & 4) ? 0x00FF : 0) |
                                                      ((pixel & 8) ? 0xFF00 : 0));
    for (int row = 0; row < 8; ++row) writeWord(ppu, lowPlanes);
    for (int row = 0; row < 8; ++row) writeWord(ppu, highPlanes);
}
void writeCgram(PPU& ppu, uint8_t index, uint16_t bgr555) {
    ppu.writeRegister(0x2121, index);
    ppu.writeRegister(0x2122, static_cast<uint8_t>(bgr555));
    ppu.writeRegister(0x2122, static_cast<uint8_t>(bgr555 >> 8));
}
}
int main() {
    PPU ppu;
    ppu.writeRegister(0x2100, 0x80);
    ppu.writeRegister(0x2115, 0x80);
    setVmadd(ppu, 0x0000);
    writeWord(ppu, 0x0000);
    writeSolid4bppTile(ppu, 0x1000, 1);
    writeSolid4bppTile(ppu, 0x2000, 2);
    writeCgram(ppu, 1, 0x001F);
    writeCgram(ppu, 2, 0x03E0);
    ppu.writeRegister(0x2105, 0x01);
    ppu.writeRegister(0x2107, 0x00);
    ppu.writeRegister(0x210B, 0x01);
    ppu.writeRegister(0x212C, 0x01);
    ppu.writeRegister(0x2100, 0x0F);
    while (ppu.scanline() < 1) ppu.tick();
    while (ppu.dot() <= PPU::VISIBLE_DOT_START + 3) ppu.tick();
    const uint32_t before = ppu.framebuffer()[0];
    ppu.writeRegister(0x210B, 0x02);
    while (ppu.dot() <= PPU::VISIBLE_DOT_START + 4) ppu.tick();
    const uint32_t after = ppu.framebuffer()[4];
    assert(before != after);
    assert(before == 0xFF0000FFu);
    assert(after == 0xFF00FF00u);
    std::puts("ppu Mode-1 row-cache invalidation regression ok");
}

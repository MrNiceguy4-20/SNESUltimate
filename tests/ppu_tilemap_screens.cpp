#include "bus.hpp"
#include "ppu.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
static void setVmadd(PPU& ppu, uint16_t word) {
    ppu.writeRegister(0x2116, static_cast<uint8_t>(word & 0xFF));
    ppu.writeRegister(0x2117, static_cast<uint8_t>((word >> 8) & 0xFF));
}
static void writeWord(PPU& ppu, uint16_t wordAddr, uint16_t value) {
    setVmadd(ppu, wordAddr);
    ppu.writeRegister(0x2118, static_cast<uint8_t>(value & 0xFF));
    ppu.writeRegister(0x2119, static_cast<uint8_t>((value >> 8) & 0xFF));
}
static void writeColor(PPU& ppu, uint8_t index, uint16_t color) {
    ppu.writeRegister(0x2121, index);
    ppu.writeRegister(0x2122, static_cast<uint8_t>(color & 0xFF));
    ppu.writeRegister(0x2122, static_cast<uint8_t>((color >> 8) & 0x7F));
}
static void fillTile2bpp(PPU& ppu, uint16_t wordBase) {
    ppu.writeRegister(0x2115, 0x80);
    setVmadd(ppu, wordBase);
    for (int row = 0; row < 8; ++row) {
        ppu.writeRegister(0x2118, 0xFF);
        ppu.writeRegister(0x2119, 0x00);
    }
}
int main() {
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.writeRegister(0x2100, 0x80);
        ppu.writeRegister(0x2105, 0x00);
        ppu.writeRegister(0x2107, 0x00);
        ppu.writeRegister(0x210B, 0x01);
        ppu.writeRegister(0x212C, 0x01);
        writeColor(ppu, 0, 0x0000);
        writeColor(ppu, 1, 0x001F);
        writeWord(ppu, 0x0000, 0x0000);
        fillTile2bpp(ppu, 0x1000);
        ppu.writeRegister(0x210B, 0x00);
        fillTile2bpp(ppu, 0x0000);
        ppu.writeRegister(0x2100, 0x0F);
        for (int i = 0; i < PPU::VISIBLE_DOT_START + 8; ++i)
            ppu.tick();
        assert(ppu.bgScreenConfig(0) == 0x00);
    }
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.writeRegister(0x2100, 0x80);
        ppu.writeRegister(0x2105, 0x00);
        ppu.writeRegister(0x2107, 0x01);
        assert((ppu.bgScreenConfig(0) & 3) == 1);
        writeWord(ppu, 0x0000, 0x00AA);
        writeWord(ppu, 0x0400, 0x00BB);
    }
    {
        PPU ppu;
        ppu.writeRegister(0x2107, 0x02);
        assert((ppu.bgScreenConfig(0) & 3) == 2);
    }
    {
        PPU ppu;
        ppu.writeRegister(0x2107, 0x03);
        assert((ppu.bgScreenConfig(0) & 3) == 3);
    }
    {
        PPU ppu;
        ppu.writeRegister(0x2107, 0x04);
        assert(ppu.bgScreenConfig(0) == 0x04);
        ppu.writeRegister(0x2107, 0xFC);
        assert(ppu.bgScreenConfig(0) == 0xFC);
    }
    std::cout << "phase59 PPU tilemap multi-screen accuracy ok\n";
    return 0;
}

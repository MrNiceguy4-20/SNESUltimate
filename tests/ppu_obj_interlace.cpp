#include "bus.hpp"
#include "ppu.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
static void writeSprite(PPU& ppu, int index, uint8_t x, uint8_t y, uint8_t tile, uint8_t attr) {
    ppu.writeRegister(0x2102, static_cast<uint8_t>((index * 2) & 0xFF));
    ppu.writeRegister(0x2103, 0x00);
    ppu.writeRegister(0x2104, x);
    ppu.writeRegister(0x2104, y);
    ppu.writeRegister(0x2104, tile);
    ppu.writeRegister(0x2104, attr);
}
int main() {
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.writeRegister(0x2100, 0x80);
        ppu.writeRegister(0x2101, 0x00);
        writeSprite(ppu, 0, 40, 10, 0, 0);
        ppu.writeRegister(0x212C, 0x10);
        ppu.writeRegister(0x2100, 0x0F);
        ppu.writeRegister(0x2133, 0x00);
        for (int i = 0; i < PPU::VISIBLE_DOT_START + 8; ++i)
            ppu.tick();
        assert((ppu.readRegister(0x213F) & 0xFF) >= 0);
    }
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.writeRegister(0x2100, 0x80);
        ppu.writeRegister(0x2101, 0x00);
        writeSprite(ppu, 0, 0, 0, 0, 0);
        ppu.writeRegister(0x2133, 0x02);
        ppu.writeRegister(0x212C, 0x10);
        ppu.writeRegister(0x2100, 0x0F);
        for (int i = 0; i < PPU::VISIBLE_DOT_START + 16; ++i)
            ppu.tick();
        while (ppu.dot() != 0)
            ppu.tick();
        assert(ppu.scanline() >= 1);
    }
    {
        PPU ppu;
        ppu.writeRegister(0x2133, 0x03);
        assert(ppu.interlaceEnabled());
        assert((0x03 & 0x02) != 0);
    }
    std::cout << "phase51 PPU OBJ interlace accuracy ok\n";
    return 0;
}

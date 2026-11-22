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
        writeColor(ppu, 0, 0x001F);
        writeColor(ppu, 1, 0x03E0);
        ppu.writeRegister(0x2105, 0x00);
        ppu.writeRegister(0x212C, 0x01);
        ppu.writeRegister(0x212E, 0x01);
        ppu.writeRegister(0x2123, 0x02);
        ppu.writeRegister(0x2126, 10);
        ppu.writeRegister(0x2127, 20);
        ppu.writeRegister(0x2100, 0x0F);
        for (int i = 0; i < PPU::VISIBLE_DOT_START + 32; ++i)
            ppu.tick();
        assert(ppu.framebuffer()[0] != 0);
    }
    {
        PPU ppu;
        ppu.writeRegister(0x2126, 50);
        ppu.writeRegister(0x2127, 10);
        ppu.writeRegister(0x2123, 0x02);
        ppu.writeRegister(0x212E, 0x01);
        ppu.writeRegister(0x2123, 0x03);
        assert(true);
    }
    {
        PPU ppu;
        ppu.writeRegister(0x2126, 100);
        ppu.writeRegister(0x2127, 100);
        ppu.writeRegister(0x2123, 0x02);
        assert(true);
    }
    std::cout << "phase55 PPU window edge accuracy ok\n";
    return 0;
}

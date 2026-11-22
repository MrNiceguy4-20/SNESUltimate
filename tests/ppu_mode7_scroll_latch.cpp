#include "bus.hpp"
#include "ppu.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
int main() {
    Bus bus;
    PPU ppu;
    ppu.attachBus(&bus);
    bus.attachPPU(&ppu);
    ppu.writeRegister(0x210F, 0x12);
    ppu.writeRegister(0x211B, 0x34);
    ppu.writeRegister(0x210D, 0x56);
    assert(ppu.bgHOffset(0) == 0x5612);
    assert(ppu.mode7HOffsetRaw() == 0x5634);
    ppu.writeRegister(0x210E, 0x78);
    assert(ppu.bgVOffset(0) == 0x7856);
    assert(ppu.mode7VOffsetRaw() == 0x7856);
    ppu.writeRegister(0x210D, 0xFF);
    ppu.writeRegister(0x210D, 0x1F);
    assert((ppu.mode7HOffsetRaw() & 0x1FFF) == 0x1FFF);
    {
        Bus rb;
        PPU rp;
        rp.attachBus(&rb);
        rb.attachPPU(&rp);
        rp.writeRegister(0x2100, 0x80);
        rp.writeRegister(0x2105, 0x07);
        rp.writeRegister(0x211A, 0x80);
        rp.writeRegister(0x212C, 0x01);
        auto color = [&](uint8_t index, uint16_t bgr) {
            rp.writeRegister(0x2121, index);
            rp.writeRegister(0x2122, static_cast<uint8_t>(bgr));
            rp.writeRegister(0x2122, static_cast<uint8_t>(bgr >> 8));
        };
        auto m7 = [&](uint16_t reg, uint16_t v) {
            rp.writeRegister(reg, static_cast<uint8_t>(v));
            rp.writeRegister(reg, static_cast<uint8_t>(v >> 8));
        };
        color(0, 0x0000);
        color(1, 0x001F);
        m7(0x211B, 0x0100); m7(0x211C, 0x0000);
        m7(0x211D, 0x0000); m7(0x211E, 0x0100);
        m7(0x211F, 0x0000); m7(0x2120, 0x0000);
        rp.writeRegister(0x2115, 0x80);
        rp.writeRegister(0x2116, 0x00);
        rp.writeRegister(0x2117, 0x00);
        rp.writeRegister(0x2118, 0x00);
        rp.writeRegister(0x2119, 0x01);
        rp.writeRegister(0x210D, 0xFF);
        rp.writeRegister(0x210D, 0x1F);
        rp.writeRegister(0x210E, 0xFF);
        rp.writeRegister(0x210E, 0x1F);
        rp.writeRegister(0x2100, 0x0F);
        while (rp.scanline() < 1) rp.tick();
        for (int i = 0; i <= PPU::VISIBLE_DOT_START + 1; ++i) rp.tick();
        assert(rp.framebuffer()[1] != 0xFF000000u);
    }
    std::cout << "Mode 7 dual scroll latch regression ok\n";
    return 0;
}

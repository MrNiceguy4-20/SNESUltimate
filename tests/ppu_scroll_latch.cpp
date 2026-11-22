#include "bus.hpp"
#include "ppu.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
int main() {
    {
        PPU ppu;
        ppu.writeRegister(0x210D, 0x12);
        assert(ppu.bgHOffset(0) == 0x1200);
        ppu.writeRegister(0x210D, 0x34);
        assert(ppu.bgHOffset(0) == 0x3412);
        ppu.writeRegister(0x2110, 0xAB);
        assert(ppu.bgVOffset(1) == 0xAB34);
        ppu.writeRegister(0x210F, 0x01);
        assert(ppu.bgHOffset(1) == 0x01AC);
    }
    {
        PPU ppu;
        ppu.writeRegister(0x210D, 0x15);
        ppu.writeRegister(0x210E, 0xA2);
        ppu.writeRegister(0x210F, 0x03);
        assert(ppu.bgHOffset(1) == 0x03A5);
        ppu.writeRegister(0x2111, 0x06);
        assert(ppu.bgHOffset(2) == 0x0603);
        ppu.writeRegister(0x2113, 0x02);
        assert(ppu.bgHOffset(3) == 0x0206);
    }
    {
        PPU ppu;
        ppu.writeRegister(0x210D, 0xFF);
        ppu.writeRegister(0x210D, 0x03);
        assert(ppu.bgHOffset(0) == 0x03FF);
        assert((ppu.bgHOffset(0) & 0x3FF) == 0x3FF);
    }
    {
        PPU ppu;
        ppu.writeRegister(0x210D, 0x00);
        ppu.writeRegister(0x210D, 0x01);
        ppu.writeRegister(0x210E, 0x00);
        ppu.writeRegister(0x210E, 0x02);
        assert(ppu.bgHOffset(0) == 0x0100);
        assert(ppu.bgVOffset(0) == 0x0200);
    }
    std::cout << "phase54 PPU scroll latch accuracy ok\n";
    return 0;
}

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
static uint16_t readWordNoRemap(PPU& ppu, uint16_t wordAddr) {
    ppu.writeRegister(0x2115, 0x80);
    setVmadd(ppu, wordAddr);
    const uint8_t lo = ppu.readRegister(0x2139);
    const uint8_t hi = ppu.readRegister(0x213A);
    return static_cast<uint16_t>(lo | (static_cast<uint16_t>(hi) << 8));
}
int main() {
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.writeRegister(0x2100, 0x80);
        ppu.writeRegister(0x2115, 0x80);
        writeWord(ppu, 0x0000, 0x1111);
        writeWord(ppu, 0x0001, 0x2222);
        writeWord(ppu, 0x0002, 0x3333);
        assert(readWordNoRemap(ppu, 0x0000) == 0x1111);
        assert(readWordNoRemap(ppu, 0x0001) == 0x2222);
        assert(readWordNoRemap(ppu, 0x0002) == 0x3333);
    }
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.writeRegister(0x2100, 0x80);
        ppu.writeRegister(0x2115, 0x84);
        writeWord(ppu, 0x0001, 0xA5A5);
        assert(readWordNoRemap(ppu, 0x0008) == 0xA5A5);
        ppu.writeRegister(0x2115, 0x84);
        writeWord(ppu, 0x0020, 0x5A5A);
        assert(readWordNoRemap(ppu, 0x0001) == 0x5A5A);
    }
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.writeRegister(0x2100, 0x80);
        ppu.writeRegister(0x2115, 0x88);
        writeWord(ppu, 0x0001, 0xBEEF);
        assert(readWordNoRemap(ppu, 0x0008) == 0xBEEF);
        ppu.writeRegister(0x2115, 0x88);
        writeWord(ppu, 0x0040, 0xCAFE);
        assert(readWordNoRemap(ppu, 0x0001) == 0xCAFE);
    }
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.writeRegister(0x2100, 0x80);
        ppu.writeRegister(0x2115, 0x8C);
        writeWord(ppu, 0x0001, 0x1234);
        assert(readWordNoRemap(ppu, 0x0008) == 0x1234);
    }
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.writeRegister(0x2100, 0x80);
        ppu.writeRegister(0x2115, 0x80);
        setVmadd(ppu, 0x0010);
        ppu.writeRegister(0x2118, 0x00);
        ppu.writeRegister(0x2119, 0x00);
        ppu.writeRegister(0x2118, 0xAA);
        ppu.writeRegister(0x2119, 0xBB);
        assert(readWordNoRemap(ppu, 0x0011) == 0xBBAA);
        ppu.writeRegister(0x2115, 0x81);
        setVmadd(ppu, 0x0100);
        ppu.writeRegister(0x2118, 0x00);
        ppu.writeRegister(0x2119, 0x00);
        ppu.writeRegister(0x2118, 0xCC);
        ppu.writeRegister(0x2119, 0xDD);
        assert(readWordNoRemap(ppu, 0x0120) == 0xDDCC);
        ppu.writeRegister(0x2115, 0x82);
        setVmadd(ppu, 0x0200);
        ppu.writeRegister(0x2118, 0x00);
        ppu.writeRegister(0x2119, 0x00);
        ppu.writeRegister(0x2118, 0xEE);
        ppu.writeRegister(0x2119, 0xFF);
        assert(readWordNoRemap(ppu, 0x0280) == 0xFFEE);
    }
    std::cout << "phase57 PPU VRAM remap accuracy ok\n";
    return 0;
}

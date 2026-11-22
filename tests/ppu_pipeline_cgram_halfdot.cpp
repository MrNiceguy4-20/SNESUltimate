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
        writeColor(ppu, 5, 0x03E0);
        ppu.writeRegister(0x2121, 5);
        ppu.writeRegister(0x2100, 0x0F);
        for (int i = 0; i < 32; ++i) ppu.tick();
        assert(ppu.scanline() == 0);
        assert(ppu.dot() == 32);
        assert(!ppu.hblank());
        assert(!ppu.vblank());
        ppu.writeRegister(0x2122, 0x00);
        ppu.writeRegister(0x2122, 0x7C);
        assert(ppu.cgramColor(0) == 0x7C00);
        assert(ppu.cgramColor(5) == 0x03E0);
    }
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.writeRegister(0x2100, 0x80);
        writeColor(ppu, 0, 0x001F);
        writeColor(ppu, 7, 0x7C00);
        ppu.writeRegister(0x2121, 7);
        ppu.writeRegister(0x2100, 0x0F);
        uint64_t clock = 0;
        for (int i = 0; i < 40; ++i) {
            clock += static_cast<uint64_t>(ppu.masterClocksThisDot());
            bus.setMasterClock(clock);
            ppu.tick();
        }
        assert(ppu.dot() == 40);
        assert(ppu.masterClocksThisDot() == 4);
        bus.setMasterClock(clock);
        assert(ppu.masterClockPhaseInDot() == 0);
        ppu.writeRegister(0x2122, 0xE0);
        ppu.writeRegister(0x2122, 0x03);
        assert(ppu.cgramColor(0) == 0x03E0);
        assert(ppu.cgramColor(7) == 0x7C00);
        ppu.writeRegister(0x2100, 0x80);
        writeColor(ppu, 0, 0x001F);
        ppu.writeRegister(0x2121, 7);
        ppu.writeRegister(0x2100, 0x0F);
        bus.setMasterClock(clock + 2);
        assert(ppu.dot() == 40);
        assert(ppu.masterClockPhaseInDot() == 2);
        ppu.writeRegister(0x2122, 0x00);
        ppu.writeRegister(0x2122, 0x7C);
        assert(ppu.cgramColor(0) == 0x7C00);
        assert(ppu.cgramColor(7) == 0x7C00);
    }
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        uint64_t clock = 0;
        for (int i = 0; i < 323; ++i) {
            clock += static_cast<uint64_t>(ppu.masterClocksThisDot());
            bus.setMasterClock(clock);
            ppu.tick();
        }
        assert(ppu.dot() == 323);
        assert(ppu.masterClocksThisDot() == 6);
        bus.setMasterClock(clock + 2);
        assert(ppu.masterClockPhaseInDot() == 2);
        bus.setMasterClock(clock + 3);
        assert(ppu.masterClockPhaseInDot() == 3);
        bus.setMasterClock(clock + 5);
        assert(ppu.masterClockPhaseInDot() == 5);
    }
    {
        Bus bus;
        PPU ppu;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.writeRegister(0x2100, 0x80);
        writeColor(ppu, 0, 0x001F);
        writeColor(ppu, 9, 0x03E0);
        ppu.writeRegister(0x2121, 9);
        ppu.writeRegister(0x2122, 0x00);
        ppu.writeRegister(0x2122, 0x7C);
        assert(ppu.cgramColor(9) == 0x7C00);
        assert(ppu.cgramColor(0) == 0x001F);
    }
    std::cout << "phase65 PPU pipeline/CGRAM master-clock half-dot accuracy ok\n";
    return 0;
}

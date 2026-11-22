#include "bus.hpp"
#include "ppu.hpp"
#include <iostream>
static int fails = 0;
static void check(bool ok, const char* msg) { if (!ok) { ++fails; std::cerr << "FAIL: " << msg << "\n"; } }
int main() {
    Bus bus;
    PPU ppu;
    bus.attachPPU(&ppu);
    ppu.attachBus(&bus);
    bus.power();
    ppu.power();
    bus.write(0x7E1234, 0x5A);
    bus.write(0x002100, 0x80);
    bus.write(0x002115, 0x80);
    bus.write(0x002116, 0x00); bus.write(0x002117, 0x00);
    bus.write(0x002118, 0xA5); bus.write(0x002119, 0x5A);
    bus.write(0x002121, 0x03); bus.write(0x002122, 0x34); bus.write(0x002122, 0x12);
    bus.write(0x002102, 0x00); bus.write(0x002103, 0x00);
    bus.write(0x002104, 0x66); bus.write(0x002104, 0x77);
    bus.reset();
    ppu.reset();
    check(bus.read(0x7E1234) == 0x5A, "warm reset preserves S-WRAM");
    check(ppu.vramByte(0) == 0xA5 && ppu.vramByte(1) == 0x5A, "warm reset preserves VRAM");
    check(ppu.cgramColor(3) == 0x1234, "warm reset preserves CGRAM");
    check(ppu.oamByte(0) == 0x66 && ppu.oamByte(1) == 0x77, "warm reset preserves OAM");
    bus.power();
    ppu.power();
    check(bus.read(0x7E1234) == 0x00, "cold power initializes S-WRAM deterministically");
    check(ppu.vramByte(0) == 0x00 && ppu.vramByte(1) == 0x00, "cold power initializes VRAM deterministically");
    check(ppu.cgramColor(3) == 0x0000, "cold power initializes CGRAM deterministically");
    check(ppu.oamByte(0) == 0x00 && ppu.oamByte(1) == 0x00, "cold power initializes OAM deterministically");
    if (fails) return 1;
    std::cout << "phase4 power/reset memory-state accuracy ok\n";
    return 0;
}

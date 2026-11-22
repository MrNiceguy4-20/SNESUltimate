#include "bus.hpp"
#include "ppu.hpp"
#include <cstdint>
#include <iostream>
static int fails = 0;
static void check(bool ok, const char* msg) { if (!ok) { ++fails; std::cerr << "FAIL: " << msg << "\n"; } }
int main() {
    Bus bus;
    PPU ppu;
    bus.attachPPU(&ppu);
    ppu.attachBus(&bus);
    bus.reset();
    ppu.reset();
    bus.write(0x002100, 0x80);
    bus.write(0x002102, 0x00);
    bus.write(0x002103, 0x00);
    bus.write(0x002104, 0x10);
    bus.write(0x002104, 0x00);
    bus.write(0x002102, 0x00);
    bus.write(0x002103, 0x00);
    check(bus.read(0x002138) == 0x10, "OAM read seeds PPU1 output bus");
    bus.write(0x7E0000, 0x00);
    check((bus.read(0x00213E) & 0x1F) == 0x11, "STAT77 bit4 comes from PPU1 bus, not intervening CPU write");
    bus.write(0x002121, 0x00);
    bus.write(0x002122, 0x80);
    bus.write(0x002122, 0x00);
    bus.write(0x002121, 0x00);
    check(bus.read(0x00213B) == 0x80, "CGRAM low read seeds PPU2 output bus");
    bus.write(0x7E0001, 0x00);
    check(bus.read(0x00213B) == 0x80, "CGRAM high D7 comes from PPU2 bus");
    bus.write(0x7E0010, 0x20);
    check(bus.read(0x7E0010) == 0x20, "external read seeds 5A22 read bus");
    bus.write(0x7E0011, 0x00);
    check((bus.read(0x004212) & 0x3E) == 0x20, "5A22 internal open bits ignore intervening write");
    ppu.setPpu2Revision(PPU::Ppu2Revision::PPU2);
    check((bus.read(0x00213F) & 0x0F) == 1, "PPU2 original reports version 1");
    ppu.setPpu2Revision(PPU::Ppu2Revision::PPU2A);
    check((bus.read(0x00213F) & 0x0F) == 2, "PPU2 A reports version 2");
    ppu.setPpu2Revision(PPU::Ppu2Revision::PPU2B);
    check((bus.read(0x00213F) & 0x0F) == 3, "PPU2 B reports version 3");
    if (fails) return 1;
    std::cout << "phase4 split open-bus/revision accuracy ok\n";
    return 0;
}

#include "bus.hpp"
#include "ppu.hpp"
#include <cassert>
#include <iostream>
int main() {
    Bus bus;
    PPU ppu;
    bus.attachPPU(&ppu);
    ppu.attachBus(&bus);
    bus.reset();
    ppu.reset();
    bus.write(0x7E0000, 0xA5);
    (void)bus.read(0x7E0000);
    bus.write(0x4200, 0x80);
    bus.write(0x7E0001, 0x5A);
    (void)bus.read(0x7E0001);
    assert(bus.read(0x004200) == 0x5A);
    bus.write(0x2181, 0x34);
    bus.write(0x7E0002, 0xC3);
    (void)bus.read(0x7E0002);
    assert(bus.read(0x002181) == 0xC3);
    assert(bus.read(0x002182) == 0xC3);
    assert(bus.read(0x002183) == 0xC3);
    bus.write(0x2100, 0x8F);
    bus.write(0x7E0003, 0x96);
    (void)bus.read(0x7E0003);
    assert(bus.read(0x002100) == 0x96);
    assert(bus.read(0x00212C) == 0x96);
    std::cout << "base open-bus accuracy ok\n";
    return 0;
}

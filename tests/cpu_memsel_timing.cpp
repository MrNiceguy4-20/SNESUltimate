#include "bus.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
int main() {
    Bus bus;
    bus.reset();
    assert(bus.accessMasterClocks(0x7E0000) == 8);
    assert(bus.accessMasterClocks(0x7FFFFF) == 8);
    assert(bus.accessMasterClocks(0x000000) == 8);
    assert(bus.accessMasterClocks(0x001FFF) == 8);
    assert(bus.accessMasterClocks(0x800000) == 8);
    assert(bus.accessMasterClocks(0x002100) == 6);
    assert(bus.accessMasterClocks(0x00213F) == 6);
    assert(bus.accessMasterClocks(0x004016) == 12);
    assert(bus.accessMasterClocks(0x00401F) == 12);
    assert(bus.accessMasterClocks(0x004200) == 6);
    assert(bus.accessMasterClocks(0x00420D) == 6);
    bus.write(0x420D, 0x00);
    assert(!bus.fastRomEnabled());
    assert(bus.accessMasterClocks(0x808000) == 8);
    assert(bus.accessMasterClocks(0xC00000) == 8);
    bus.write(0x420D, 0x01);
    assert(bus.fastRomEnabled());
    assert(bus.accessMasterClocks(0x808000) == 6);
    assert(bus.accessMasterClocks(0xC00000) == 6);
    assert(bus.accessMasterClocks(0xFFFFFF) == 6);
    assert(bus.accessMasterClocks(0x008000) == 8);
    assert(bus.accessMasterClocks(0x808000) == 6);
    bus.write(0x420D, 0xFE);
    assert(!bus.fastRomEnabled());
    bus.write(0x420D, 0xFF);
    assert(bus.fastRomEnabled());
    std::cout << "phase62 MEMSEL / FastROM timing accuracy ok\n";
    return 0;
}

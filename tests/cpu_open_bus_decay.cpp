#include "bus.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
int main() {
    Bus bus;
    bus.write(0x7E0000, 0xA5);
    bus.setMasterClock(1000000);
    assert(bus.openBus() == 0xA5);
    bus.configureOpenBusDecay(true, 100, 0x00);
    bus.write(0x7E0000, 0xF0);
    bus.setMasterClock(bus.masterClock() + 99);
    assert(bus.openBus() == 0xF0);
    bus.setMasterClock(bus.masterClock() + 1);
    assert(bus.openBus() == 0x00);
    bus.write(0x7E0001, 0x5A);
    bus.setMasterClock(bus.masterClock() + 50);
    assert(bus.openBus() == 0x5A);
    bus.configureOpenBusDecay(true, 10, 0xFF);
    bus.write(0x7E0002, 0x00);
    bus.setMasterClock(bus.masterClock() + 10);
    assert(bus.openBus() == 0xFF);
    std::cout << "phase5 CPU open-bus decay infrastructure ok\n";
    return 0;
}

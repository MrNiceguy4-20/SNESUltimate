#include "bus.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
static uint8_t rdnmiVersion(Bus& bus) {
    bus.write(0x7E0000, 0x70);
    return static_cast<uint8_t>(bus.read(0x4210) & 0x0F);
}
int main() {
    Bus bus;
    assert(bus.scpuRevision() == Bus::SCpuRevision::SCPUB);
    assert(rdnmiVersion(bus) == 2);
    bus.setSCpuRevision(Bus::SCpuRevision::SCPU);
    assert(bus.scpuVersion() == 1);
    assert(rdnmiVersion(bus) == 1);
    bus.setSCpuRevision(Bus::SCpuRevision::SCPUA);
    assert(bus.scpuVersion() == 2);
    assert(rdnmiVersion(bus) == 2);
    bus.setSCpuRevision(Bus::SCpuRevision::SCPUB);
    assert(bus.scpuVersion() == 2);
    assert(rdnmiVersion(bus) == 2);
    std::cout << "phase3 S-CPU revision identity accuracy ok\n";
    return 0;
}

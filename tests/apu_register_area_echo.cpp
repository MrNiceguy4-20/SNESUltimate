#include "apu.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
namespace {
void writeDSP(APU& apu, uint8_t address, uint8_t value) {
    apu.write(0x00F2, address);
    apu.write(0x00F3, value);
}
void enterStoppedClocking(APU& apu) {
    apu.write(0x00F1, 0x00);
    apu.write(0xFFC0, 0xFF);
    assert(apu.tick() == 2);
}
void runToCycle(APU& apu, uint64_t target) {
    while (apu.cycles() < target) {
        assert(apu.tick() == 1);
    }
    assert(apu.cycles() == target);
}
}
int main() {
    {
        APU apu;
        apu.write(0x00F8, 0xA5);
        apu.write(0x00F9, 0x5A);
        writeDSP(apu, 0x6D, 0x00);
        writeDSP(apu, 0x7D, 0x01);
        writeDSP(apu, 0x6C, 0x00);
        enterStoppedClocking(apu);
        runToCycle(apu, 62u * 32u + 30u);
        assert(apu.read(0x00F8) == 0xA5);
        assert(apu.read(0x00F9) == 0x5A);
    }
    std::cout << "apu_register_area_echo: PASS\n";
    return 0;
}

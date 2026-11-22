#include "apu.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
namespace {
void installNops(APU& apu) {
    apu.write(0x00F1, 0x00);
    for (uint32_t a = 0xFFC0; a <= 0xFFFF; ++a) apu.write(static_cast<uint16_t>(a), 0x00);
}
void runCycles(APU& apu, unsigned cycles) {
    assert((cycles & 1u) == 0);
    for (unsigned c = 0; c < cycles; c += 2) assert(apu.tick() == 2);
}
}
int main() {
    {
        APU apu;
        apu.write(0x2345, 0x5A);
        apu.write(0xFFC8, 0xA5);
        apu.reset();
        assert(apu.read(0x2345) == 0x5A);
        apu.write(0x00F1, 0x00);
        assert(apu.read(0xFFC8) == 0xA5);
    }
    {
        APU apu;
        installNops(apu);
        apu.write(0x00FC, 3);
        apu.write(0x00F1, 0x04);
        runCycles(apu, 16 * 3);
        assert(apu.read(0x00FF) == 1);
        runCycles(apu, 16 * 3);
        apu.reset();
        assert(apu.read(0x00FF) == 0);
        installNops(apu);
        apu.write(0x00F1, 0x04);
        runCycles(apu, 16 * 2);
        assert(apu.read(0x00FF) == 0);
        runCycles(apu, 16);
        assert(apu.read(0x00FF) == 1);
    }
    {
        APU apu;
        apu.writePort(0, 0x44);
        apu.write(0x00F4, 0x88);
        assert(apu.read(0x00F4) == 0x44);
        assert(apu.readPort(0) == 0x88);
        apu.reset();
        assert(apu.read(0x00F4) == 0);
        assert(apu.readPort(0) == 0);
    }
    {
        APU apu;
        apu.write(0x3456, 0x77);
        apu.write(0x00FC, 5);
        apu.power();
        assert(apu.read(0x3456) == 0);
        installNops(apu);
        apu.write(0x00F1, 0x04);
        runCycles(apu, 16 * 5);
        assert(apu.read(0x00FF) == 0);
    }
    std::cout << "apu_reset_power_accuracy: PASS\n";
    return 0;
}

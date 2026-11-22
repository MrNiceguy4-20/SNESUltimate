#include "apu.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
namespace {
void installNops(APU& apu) {
    apu.write(0x00F1, 0x00);
    for (uint32_t a = 0xFFC0; a <= 0xFFFF; ++a) {
        apu.write(static_cast<uint16_t>(a), 0x00);
    }
}
void runCycles(APU& apu, unsigned cycles) {
    assert((cycles & 1u) == 0);
    for (unsigned c = 0; c < cycles; c += 2) {
        assert(apu.tick() == 2);
    }
}
}
int main() {
    {
        APU apu;
        installNops(apu);
        apu.write(0x00FC, 1);
        runCycles(apu, 14);
        apu.write(0x00F1, 0x04);
        runCycles(apu, 2);
        assert(apu.read(0x00FF) == 1);
        assert(apu.read(0x00FF) == 0);
    }
    {
        APU apu;
        installNops(apu);
        apu.write(0x00FC, 2);
        runCycles(apu, 14);
        apu.write(0x00F1, 0x04);
        runCycles(apu, 16);
        apu.write(0x00F1, 0x00);
        runCycles(apu, 16);
        apu.write(0x00F1, 0x04);
        runCycles(apu, 2);
        assert(apu.read(0x00FF) == 0);
        runCycles(apu, 16);
        assert(apu.read(0x00FF) == 1);
    }
    {
        APU apu;
        installNops(apu);
        apu.write(0x00FC, 3);
        apu.write(0x00F1, 0x04);
        runCycles(apu, 32);
        apu.write(0x00FC, 1);
        runCycles(apu, 16);
        assert(apu.read(0x00FF) == 0);
    }
    {
        APU apu;
        installNops(apu);
        apu.write(0x00FC, 0);
        apu.write(0x00F1, 0x04);
        runCycles(apu, 16 * 255);
        assert(apu.read(0x00FF) == 0);
        runCycles(apu, 16);
        assert(apu.read(0x00FF) == 1);
    }
    std::cout << "apu_timer_accuracy: PASS\n";
    return 0;
}

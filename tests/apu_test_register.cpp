#include "apu.hpp"
#include <cassert>
#include <cstdint>
#include <initializer_list>
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
    for (unsigned c = 0; c < cycles; c += 2) assert(apu.tick() == 2);
}
void load(APU& apu, std::initializer_list<uint8_t> bytes) {
    apu.write(0x00F1, 0x00);
    uint16_t address = 0xFFC0;
    for (uint8_t b : bytes) apu.write(address++, b);
}
}
int main() {
    {
        APU apu;
        apu.write(0x2000, 0x11);
        assert(apu.read(0x2000) == 0x11);
        apu.write(0x00F0, 0x08);
        apu.write(0x2000, 0x22);
        assert(apu.read(0x2000) == 0x11);
        const uint8_t oldBacking = apu.flatTestPeek(0x00F8);
        apu.write(0x00F8, 0x33);
        assert(apu.read(0x00F8) == 0x33);
        assert(apu.flatTestPeek(0x00F8) == oldBacking);
        apu.write(0x00F0, 0x0A);
        apu.write(0x2000, 0x44);
        assert(apu.read(0x2000) == 0x44);
    }
    {
        APU apu;
        apu.write(0x2345, 0xA6);
        apu.write(0x00F8, 0x12);
        const uint8_t backingF8 = apu.flatTestPeek(0x00F8);
        apu.write(0x00F0, 0x0E);
        assert(apu.read(0x2345) == 0x5A);
        assert(apu.read(0x00F8) == 0x12);
        apu.write(0x00F8, 0x34);
        assert(apu.read(0x00F8) == 0x34);
        assert(apu.flatTestPeek(0x00F8) == backingF8);
        assert(apu.read(0xFFC0) == 0xCD);
    }
    {
        APU apu;
        apu.write(0x00F8, 0x5C);
        apu.write(0x00F9, 0xA3);
        assert(apu.flatTestPeek(0x00F8) == 0x5C);
        assert(apu.flatTestPeek(0x00F9) == 0xA3);
        apu.write(0x00F0, 0x08);
        apu.write(0x00F8, 0x66);
        apu.write(0x00F9, 0x99);
        assert(apu.read(0x00F8) == 0x66);
        assert(apu.read(0x00F9) == 0x99);
        assert(apu.flatTestPeek(0x00F8) == 0x5C);
        assert(apu.flatTestPeek(0x00F9) == 0xA3);
    }
    {
        APU apu;
        assert(apu.flatTestPeek(0x00F4) == 0x00);
        apu.writePort(0, 0x12);
        assert(apu.read(0x00F4) == 0x12);
        assert(apu.flatTestPeek(0x00F4) == 0x00);
        apu.write(0x00F4, 0x34);
        assert(apu.readPort(0) == 0x34);
        assert(apu.read(0x00F4) == 0x12);
        assert(apu.flatTestPeek(0x00F4) == 0x34);
        apu.write(0x00F0, 0x08);
        apu.write(0x00F4, 0x56);
        assert(apu.readPort(0) == 0x56);
        assert(apu.flatTestPeek(0x00F4) == 0x34);
    }
    {
        APU apu;
        load(apu, {0x40});
        assert(apu.tick() == 2);
        assert((apu.psw() & 0x20) != 0);
        apu.write(0x00F0, 0x08);
        apu.write(0x2345, 0x5A);
        assert(apu.read(0x2345) == 0x5A);
    }
    {
        APU apu;
        installNops(apu);
        apu.write(0x00FC, 1);
        apu.write(0x00F1, 0x04);
        apu.write(0x00F0, 0x02);
        runCycles(apu, 32);
        assert(apu.read(0x00FF) == 0);
        apu.write(0x00F0, 0x0A);
        runCycles(apu, 16);
        assert(apu.read(0x00FF) == 1);
    }
    std::cout << "apu_test_register: PASS\n";
    return 0;
}

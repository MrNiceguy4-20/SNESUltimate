#include "apu.hpp"
#include <array>
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
}
int main() {
    constexpr std::array<uint8_t, 16> denominator = {
        3, 5, 9, 17, 4, 6, 10, 18, 6, 8, 12, 20, 10, 12, 16, 24
    };
    for (uint8_t speed = 0; speed < 16; ++speed) {
        APU apu;
        installNops(apu);
        apu.write(0x00F0, static_cast<uint8_t>((speed << 4) | 0x0A));
        unsigned elapsed = 0;
        elapsed += apu.tick();
        elapsed += apu.tick();
        elapsed += apu.tick();
        assert(elapsed == static_cast<unsigned>(2u * denominator[speed]));
        assert(apu.pc() == 0xFFC3);
        assert(apu.cycles() == elapsed);
    }
    {
        APU apu;
        installNops(apu);
        apu.write(0x00F0, 0x1A);
        assert(apu.tick() == 3);
        assert(apu.tick() == 3);
        assert(apu.tick() == 4);
        assert(apu.tick() == 3);
        assert(apu.tick() == 3);
    }
    {
        APU apu;
        installNops(apu);
        apu.write(0x00FC, 1);
        apu.write(0x00F1, 0x04);
        apu.write(0x00F0, 0x1A);
        unsigned elapsed = 0;
        for (int i = 0; i < 5; ++i) elapsed += apu.tick();
        assert(elapsed == 16);
        assert(apu.read(0x00FF) == 1);
    }
    {
        APU apu;
        apu.write(0x00F1, 0x00);
        apu.write(0xFFC0, 0xFF);
        apu.write(0x00F0, 0xFA);
        assert(apu.tick() == 16);
        assert(apu.stopped());
        assert(apu.tick() == 1);
        assert(apu.tick() == 1);
    }
    std::cout << "apu_test_waitstate_accuracy: PASS\n";
    return 0;
}

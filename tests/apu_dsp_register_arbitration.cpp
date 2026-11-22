#include "apu.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
namespace {
void installNops(APU& apu) {
    apu.write(0x00F1, 0x00);
}
void installLongNopsAligned(APU& apu) {
    apu.write(0x00F1, 0x00);
    apu.write(0xFFC0, 0x5F); apu.write(0xFFC1, 0x00); apu.write(0xFFC2, 0x02);
    apu.write(0x0200, 0x5F); apu.write(0x0201, 0x00); apu.write(0x0202, 0x03);
    unsigned used = apu.tick();
    used += apu.tick();
    assert(used == 6);
    while (used < 32) used += apu.tick();
    assert(used == 32);
}
void dspWrite(APU& apu, uint8_t address, uint8_t value) {
    apu.write(0x00F2, address);
    apu.write(0x00F3, value);
}
uint8_t dspRead(APU& apu, uint8_t address) {
    apu.write(0x00F2, address);
    return apu.read(0x00F3);
}
void runClocks(APU& apu, unsigned clocks) {
    unsigned used = 0;
    while (used < clocks) used += apu.tick();
    assert(used == clocks);
}
void runSample(APU& apu) { runClocks(apu, 32); }
void setupVoice0(APU& apu, uint16_t block, bool loop) {
    constexpr uint16_t dir = 0x1000;
    apu.write(dir + 0, static_cast<uint8_t>(block));
    apu.write(dir + 1, static_cast<uint8_t>(block >> 8));
    apu.write(dir + 2, static_cast<uint8_t>(block));
    apu.write(dir + 3, static_cast<uint8_t>(block >> 8));
    apu.write(block, static_cast<uint8_t>(0xC1 | (loop ? 0x02 : 0x00)));
    for (int i = 1; i <= 8; ++i) {
        apu.write(static_cast<uint16_t>(block + i), 0x77);
    }
    dspWrite(apu, 0x5D, 0x10);
    dspWrite(apu, 0x04, 0x00);
    dspWrite(apu, 0x02, 0x00);
    dspWrite(apu, 0x03, 0x10);
    dspWrite(apu, 0x05, 0x00);
    dspWrite(apu, 0x07, 0x7F);
    dspWrite(apu, 0x6C, 0x20);
}
}
int main() {
    {
        APU apu;
        installNops(apu);
        runClocks(apu, 2);
        dspWrite(apu, 0x09, 0x5A);
        runClocks(apu, 2);
        assert(dspRead(apu, 0x09) == 0x5A);
    }
    {
        APU apu;
        installNops(apu);
        runClocks(apu, 4);
        dspWrite(apu, 0x08, 0x66);
        runClocks(apu, 2);
        assert(dspRead(apu, 0x08) == 0x66);
    }
    {
        APU apu;
        installLongNopsAligned(apu);
        setupVoice0(apu, 0x2200, false);
        dspWrite(apu, 0x4C, 0x01);
        bool ended = false;
        for (int sample = 0; sample < 40 && !ended; ++sample) {
            runSample(apu);
            ended = (dspRead(apu, 0x7C) & 0x01) != 0;
        }
        assert(ended);
        runClocks(apu, 2);
        dspWrite(apu, 0x7C, 0xFF);
        assert(dspRead(apu, 0x7C) == 0);
        runClocks(apu, 2);
        assert(dspRead(apu, 0x7C) == 0);
    }
    {
        APU apu;
        installLongNopsAligned(apu);
        setupVoice0(apu, 0x2400, true);
        dspWrite(apu, 0x4C, 0x01);
        bool audible = false;
        for (int sample = 0; sample < 16 && !audible; ++sample) {
            runSample(apu);
            audible = dspRead(apu, 0x08) != 0;
        }
        assert(audible);
        dspWrite(apu, 0x7C, 0x00);
        assert((dspRead(apu, 0x7C) & 0x01) == 0);
        dspWrite(apu, 0x6C, 0xE0);
        bool ended = false;
        for (int sample = 0; sample < 32 && !ended; ++sample) {
            runSample(apu);
            ended = (dspRead(apu, 0x7C) & 0x01) != 0;
        }
        assert(ended);
        assert(dspRead(apu, 0x08) == 0);
    }
    std::cout << "apu_dsp_register_arbitration: PASS\n";
    return 0;
}

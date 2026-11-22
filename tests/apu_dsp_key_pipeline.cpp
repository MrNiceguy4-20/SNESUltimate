#include "apu.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
namespace {
void installLoop(APU& apu) {
    apu.write(0x00F1, 0x00);
    apu.write(0xFFC0, 0x2F);
    apu.write(0xFFC1, 0xFE);
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
    unsigned cycles = 0;
    while (cycles < clocks) cycles += apu.tick();
    assert(cycles == clocks);
}
void runSample(APU& apu) { runClocks(apu, 32); }
void setupVoice(APU& apu, uint16_t block, bool loop) {
    constexpr uint16_t dir = 0x1000;
    apu.write(dir + 0, static_cast<uint8_t>(block));
    apu.write(dir + 1, static_cast<uint8_t>(block >> 8));
    apu.write(dir + 2, static_cast<uint8_t>(block));
    apu.write(dir + 3, static_cast<uint8_t>(block >> 8));
    apu.write(block, static_cast<uint8_t>(0xC1 | (loop ? 0x02 : 0x00)));
    for (int i = 1; i <= 8; ++i) apu.write(static_cast<uint16_t>(block + i), 0x77);
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
        installLoop(apu);
        setupVoice(apu, 0x2000, true);
        dspWrite(apu, 0x4C, 0x01);
        for (int sample = 0; sample < 6; ++sample) {
            runSample(apu);
            assert(dspRead(apu, 0x08) == 0);
            assert(dspRead(apu, 0x09) == 0);
        }
        runSample(apu);
        runSample(apu);
        runClocks(apu, 8);
        assert(dspRead(apu, 0x08) == 0x7F);
        assert(dspRead(apu, 0x09) != 0);
    }
    {
        APU apu;
        installLoop(apu);
        setupVoice(apu, 0x2100, true);
        dspWrite(apu, 0x5C, 0x01);
        dspWrite(apu, 0x4C, 0x01);
        for (int sample = 0; sample < 8; ++sample) runSample(apu);
        assert(dspRead(apu, 0x08) == 0);
        assert(dspRead(apu, 0x09) == 0);
    }
    {
        APU apu;
        installLoop(apu);
        setupVoice(apu, 0x2200, false);
        dspWrite(apu, 0x4C, 0x01);
        bool saw_endx = false;
        for (int sample = 0; sample < 22; ++sample) {
            runSample(apu);
            if (dspRead(apu, 0x7C) & 0x01) {
                saw_endx = true;
                break;
            }
        }
        assert(saw_endx);
        dspWrite(apu, 0x4C, 0x01);
        bool cleared = false;
        for (int sample = 0; sample < 4; ++sample) {
            runSample(apu);
            if ((dspRead(apu, 0x7C) & 0x01) == 0) {
                cleared = true;
                break;
            }
        }
        assert(cleared);
    }
    std::cout << "apu_dsp_key_pipeline: PASS\n";
    return 0;
}

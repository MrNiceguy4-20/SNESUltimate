#include "apu.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
namespace {
void installNops(APU& apu) {
    apu.write(0x00F1, 0x00);
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
void makeSample(APU& apu, uint16_t directory, uint8_t source,
                uint16_t block, uint8_t packed) {
    const uint16_t entry = static_cast<uint16_t>(directory + source * 4);
    apu.write(entry + 0, static_cast<uint8_t>(block));
    apu.write(entry + 1, static_cast<uint8_t>(block >> 8));
    apu.write(entry + 2, static_cast<uint8_t>(block));
    apu.write(entry + 3, static_cast<uint8_t>(block >> 8));
    apu.write(block, 0xC3);
    for (int i = 1; i <= 8; ++i) apu.write(static_cast<uint16_t>(block + i), packed);
}
void setupVoice(APU& apu, int voice, uint8_t source) {
    const uint8_t base = static_cast<uint8_t>(voice << 4);
    dspWrite(apu, base + 4, source);
    dspWrite(apu, base + 2, 0x00);
    dspWrite(apu, base + 3, 0x10);
    dspWrite(apu, base + 5, 0x00);
    dspWrite(apu, base + 7, 0x7F);
}
}
int main() {
    {
        APU apu;
        installNops(apu);
        makeSample(apu, 0x1000, 0, 0x2000, 0x77);
        makeSample(apu, 0x1000, 1, 0x2100, 0x77);
        dspWrite(apu, 0x5D, 0x10);
        setupVoice(apu, 0, 0);
        setupVoice(apu, 1, 1);
        dspWrite(apu, 0x6C, 0x20);
        dspWrite(apu, 0x4C, 0x01);
        dspWrite(apu, 0x4C, 0x02);
        for (int i = 0; i < 8; ++i) runSample(apu);
        runClocks(apu, 8);
        assert(dspRead(apu, 0x08) == 0x00);
        assert(dspRead(apu, 0x18) == 0x7F);
    }
    {
        APU apu;
        installNops(apu);
        makeSample(apu, 0x1000, 0, 0x2200, 0x77);
        makeSample(apu, 0x1100, 0, 0x2300, 0x88);
        setupVoice(apu, 0, 0);
        dspWrite(apu, 0x6C, 0x20);
        dspWrite(apu, 0x5D, 0x10);
        runClocks(apu, 28);
        runClocks(apu, 2);
        dspWrite(apu, 0x5D, 0x11);
        dspWrite(apu, 0x4C, 0x01);
        runClocks(apu, 34);
        for (int i = 0; i < 6; ++i) runSample(apu);
        runClocks(apu, 4);
        const int8_t outx = static_cast<int8_t>(dspRead(apu, 0x09));
        assert(outx > 0);
    }
    {
        APU apu;
        installNops(apu);
        dspWrite(apu, 0x08, 0x55);
        dspWrite(apu, 0x09, 0xAA);
        assert(dspRead(apu, 0x08) == 0x55);
        assert(dspRead(apu, 0x09) == 0xAA);
        runSample(apu);
        assert(dspRead(apu, 0x08) == 0x00);
        assert(dspRead(apu, 0x09) == 0x00);
    }
    std::cout << "apu_dsp_phase_latches: PASS\n";
    return 0;
}

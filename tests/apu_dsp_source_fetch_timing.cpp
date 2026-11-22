#include "apu.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
namespace {
void installNops(APU& apu) { apu.write(0x00F1, 0x00); }
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
void writeBlock(APU& apu, uint16_t block, uint8_t packed) {
    apu.write(block, 0xC3);
    for (int i = 1; i <= 8; ++i) apu.write(static_cast<uint16_t>(block + i), packed);
}
void writeDirectory(APU& apu, uint16_t directory, uint8_t source, uint16_t block) {
    const uint16_t entry = static_cast<uint16_t>(directory + source * 4);
    apu.write(entry + 0, static_cast<uint8_t>(block));
    apu.write(entry + 1, static_cast<uint8_t>(block >> 8));
    apu.write(entry + 2, static_cast<uint8_t>(block));
    apu.write(entry + 3, static_cast<uint8_t>(block >> 8));
}
void setupVoice0(APU& apu, uint8_t source) {
    dspWrite(apu, 0x04, source);
    dspWrite(apu, 0x02, 0x00);
    dspWrite(apu, 0x03, 0x10);
    dspWrite(apu, 0x05, 0x00);
    dspWrite(apu, 0x07, 0x7F);
    dspWrite(apu, 0x00, 0x7F);
    dspWrite(apu, 0x01, 0x7F);
}
void startVoice0(APU& apu) {
    dspWrite(apu, 0x6C, 0x20);
    dspWrite(apu, 0x5D, 0x10);
    runClocks(apu, 30);
    dspWrite(apu, 0x4C, 0x01);
    runClocks(apu, 34);
}
int8_t finishToFirstOutx(APU& apu) {
    runClocks(apu, 10);
    for (int i = 0; i < 5; ++i) runSample(apu);
    runClocks(apu, 4);
    return static_cast<int8_t>(dspRead(apu, 0x09));
}
}
int main() {
    {
        APU apu;
        installNops(apu);
        writeBlock(apu, 0x2000, 0x77);
        writeBlock(apu, 0x2100, 0x99);
        writeDirectory(apu, 0x1000, 0, 0x2000);
        writeDirectory(apu, 0x1000, 1, 0x2100);
        setupVoice0(apu, 0);
        startVoice0(apu);
        runClocks(apu, 18);
        dspWrite(apu, 0x04, 1);
        runClocks(apu, 4);
        assert(finishToFirstOutx(apu) > 0);
    }
    {
        APU apu;
        installNops(apu);
        writeBlock(apu, 0x2200, 0x77);
        writeBlock(apu, 0x2300, 0x99);
        writeDirectory(apu, 0x1000, 0, 0x2200);
        setupVoice0(apu, 0);
        startVoice0(apu);
        runClocks(apu, 18);
        writeDirectory(apu, 0x1000, 0, 0x2300);
        runClocks(apu, 4);
        assert(finishToFirstOutx(apu) < 0);
    }
    std::cout << "apu_dsp_source_fetch_timing: PASS\n";
    return 0;
}

#include "apu.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>
namespace {
void installNops(APU& apu) {
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
void makeLoopingZeroSample(APU& apu, uint8_t srcn, uint16_t block) {
    constexpr uint16_t dir = 0x1000;
    const uint16_t entry = static_cast<uint16_t>(dir + srcn * 4);
    apu.write(entry + 0, static_cast<uint8_t>(block));
    apu.write(entry + 1, static_cast<uint8_t>(block >> 8));
    apu.write(entry + 2, static_cast<uint8_t>(block));
    apu.write(entry + 3, static_cast<uint8_t>(block >> 8));
    apu.write(block, 0x03);
    for (int i = 1; i <= 8; ++i) apu.write(static_cast<uint16_t>(block + i), 0);
    dspWrite(apu, 0x5D, 0x10);
}
}
int main() {
    {
        APU apu;
        installNops(apu);
        constexpr uint16_t dir = 0x1000;
        constexpr uint16_t block = 0x2000;
        apu.write(dir + 0, 0x00); apu.write(dir + 1, 0x20);
        apu.write(dir + 2, 0x00); apu.write(dir + 3, 0x20);
        apu.write(block, 0xD3);
        for (int i = 1; i <= 8; ++i) apu.write(static_cast<uint16_t>(block + i), 0x88);
        dspWrite(apu, 0x5D, 0x10);
        dspWrite(apu, 0x04, 0x00);
        dspWrite(apu, 0x02, 0x00); dspWrite(apu, 0x03, 0x00);
        dspWrite(apu, 0x05, 0x00); dspWrite(apu, 0x07, 0x7F);
        dspWrite(apu, 0x6C, 0x20);
        dspWrite(apu, 0x4C, 0x01);
        for (int i = 0; i < 9; ++i) runSample(apu);
        runClocks(apu, 4);
        assert(dspRead(apu, 0x09) == 0xF0);
    }
    {
        APU apu;
        installNops(apu);
        makeLoopingZeroSample(apu, 0, 0x2100);
        dspWrite(apu, 0x6C, 0x20);
        runSample(apu);
        dspWrite(apu, 0x04, 0x00);
        dspWrite(apu, 0x05, 0x00);
        dspWrite(apu, 0x07, static_cast<uint8_t>(0xC0 | 29));
        dspWrite(apu, 0x4C, 0x01);
        for (int i = 0; i < 5; ++i) {
            runSample(apu);
            assert(dspRead(apu, 0x08) == 0x00);
        }
        bool advanced = false;
        for (int i = 0; i < 4; ++i) {
            runSample(apu);
            if (dspRead(apu, 0x08) != 0) advanced = true;
        }
        assert(advanced);
    }
    {
        APU apu;
        installNops(apu);
        dspWrite(apu, 0x6D, 0x30);
        dspWrite(apu, 0x7D, 0x00);
        for (int t = 0; t < 7; ++t) dspWrite(apu, static_cast<uint8_t>(0x0F + t * 0x10), 0);
        dspWrite(apu, 0x7F, 0x7F);
        dspWrite(apu, 0x2C, 0x7F); dspWrite(apu, 0x3C, 0x00);
        dspWrite(apu, 0x0C, 0x00); dspWrite(apu, 0x1C, 0x00);
        dspWrite(apu, 0x6C, 0x20);
        apu.write(0x3000, 0x00); apu.write(0x3001, 0x40);
        apu.write(0x3002, 0x00); apu.write(0x3003, 0x00);
        apu.write(0x3004, 0x00); apu.write(0x3005, 0x00);
        runSample(apu);
        runSample(apu);
        runSample(apu);
        const std::vector<int16_t> audio = apu.takeAudioSamples();
        assert(audio.size() == 6);
        assert(audio[0] == 0);
        assert(audio[2] != 0);
        assert(audio[4] == audio[2]);
        assert(audio[1] == 0 && audio[3] == 0 && audio[5] == 0);
    }
    {
        APU apu;
        installNops(apu);
        constexpr uint16_t dir = 0x1000;
        constexpr uint16_t block = 0x2200;
        apu.write(dir + 0, static_cast<uint8_t>(block));
        apu.write(dir + 1, static_cast<uint8_t>(block >> 8));
        apu.write(dir + 2, static_cast<uint8_t>(block));
        apu.write(dir + 3, static_cast<uint8_t>(block >> 8));
        apu.write(block + 0, 0xC3);
        apu.write(block + 1, 0x07);
        apu.write(block + 2, 0x80);
        for (int i = 3; i <= 8; ++i) apu.write(static_cast<uint16_t>(block + i), 0);
        dspWrite(apu, 0x5D, 0x10);
        dspWrite(apu, 0x04, 0x00);
        dspWrite(apu, 0x02, 0x00); dspWrite(apu, 0x03, 0x08);
        dspWrite(apu, 0x05, 0x00); dspWrite(apu, 0x07, 0x7F);
        dspWrite(apu, 0x6C, 0x20);
        dspWrite(apu, 0x4C, 0x01);
        for (int i = 0; i < 9; ++i) runSample(apu);
        runClocks(apu, 4);
        assert(dspRead(apu, 0x09) == 0xF8);
    }
    {
        APU apu;
        installNops(apu);
        constexpr uint16_t dir = 0x1000;
        constexpr uint16_t block0 = 0x2300;
        constexpr uint16_t block1 = block0 + 9;
        apu.write(dir + 0, static_cast<uint8_t>(block0));
        apu.write(dir + 1, static_cast<uint8_t>(block0 >> 8));
        apu.write(dir + 2, static_cast<uint8_t>(block1));
        apu.write(dir + 3, static_cast<uint8_t>(block1 >> 8));
        apu.write(block0 + 0, 0x00);
        for (int i = 1; i <= 8; ++i) apu.write(static_cast<uint16_t>(block0 + i), 0);
        apu.write(block1 + 0, 0xC3);
        for (int i = 1; i <= 8; ++i) apu.write(static_cast<uint16_t>(block1 + i), 0x77);
        dspWrite(apu, 0x5D, 0x10);
        dspWrite(apu, 0x04, 0x00);
        dspWrite(apu, 0x02, 0x00); dspWrite(apu, 0x03, 0x10);
        dspWrite(apu, 0x05, 0x00); dspWrite(apu, 0x07, 0x7F);
        dspWrite(apu, 0x6C, 0x20);
        dspWrite(apu, 0x4C, 0x01);
        for (int sample = 0; sample < 22; ++sample) runSample(apu);
        runClocks(apu, 4);
        assert(dspRead(apu, 0x09) != 0x00);
    }
    std::cout << "apu_dsp_accuracy: PASS\n";
    return 0;
}

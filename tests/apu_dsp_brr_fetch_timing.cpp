#include <array>
#include <cstdint>
#include <cstddef>
#include <deque>
#include <vector>
#define private public
#include "apu.hpp"
#undef private
#include <cassert>
#include <iostream>
namespace {
void installNops(APU& apu) { apu.write(0x00F1, 0x00); }
void dspWrite(APU& apu, uint8_t address, uint8_t value) {
    apu.write(0x00F2, address);
    apu.write(0x00F3, value);
}
void runClocks(APU& apu, unsigned clocks) {
    unsigned used = 0;
    while (used < clocks) used += apu.tick();
    assert(used == clocks);
}
void writeDirectory(APU& apu, uint16_t directory, uint8_t source,
                    uint16_t start, uint16_t loop) {
    const uint16_t entry = static_cast<uint16_t>(directory + source * 4);
    apu.write(entry + 0, static_cast<uint8_t>(start));
    apu.write(entry + 1, static_cast<uint8_t>(start >> 8));
    apu.write(entry + 2, static_cast<uint8_t>(loop));
    apu.write(entry + 3, static_cast<uint8_t>(loop >> 8));
}
void setupVoice0(APU& apu, uint16_t start, uint16_t loop) {
    writeDirectory(apu, 0x1000, 0, start, loop);
    dspWrite(apu, 0x04, 0x00);
    dspWrite(apu, 0x02, 0x00);
    dspWrite(apu, 0x03, 0x10);
    dspWrite(apu, 0x05, 0x00);
    dspWrite(apu, 0x07, 0x7F);
    dspWrite(apu, 0x5D, 0x10);
    dspWrite(apu, 0x6C, 0x20);
    runClocks(apu, 30);
    dspWrite(apu, 0x4C, 0x01);
    runClocks(apu, 34);
    assert(apu.dsp_phase_ == 0);
}
}
int main() {
    constexpr uint16_t block = 0x2000;
    constexpr uint16_t loop  = 0x2400;
    APU apu;
    installNops(apu);
    apu.write(block + 0, 0xC3);
    apu.write(block + 1, 0x12);
    apu.write(block + 2, 0x34);
    apu.write(block + 3, 0x56);
    apu.write(block + 4, 0x78);
    apu.write(block + 5, 0x9A);
    apu.write(block + 6, 0xBC);
    apu.write(block + 7, 0xDE);
    apu.write(block + 8, 0xF0);
    apu.write(loop, 0x00);
    for (int i = 1; i <= 8; ++i) apu.write(loop + i, 0x00);
    setupVoice0(apu, block, loop);
    runClocks(apu, 32);
    runClocks(apu, 26);
    auto& v = apu.voices_[0];
    assert(v.brr_v3_valid);
    assert(v.brr_header_latch == 0xC3);
    assert(v.brr_byte_latch == 0x12);
    apu.write(block + 0, 0x00);
    apu.write(block + 1, 0xAA);
    apu.write(block + 2, 0x55);
    runClocks(apu, 6);
    assert(!v.brr_v3_valid);
    assert(v.brr_staging[0] == 0xC3);
    assert(v.brr_staging[1] == 0x12);
    assert(v.brr_staging[2] == 0x55);
    assert(v.brr_offset == 3);
    apu.write(block + 0, 0xC3);
    runClocks(apu, 32 * 4);
    assert(v.brr_initialized);
    assert(v.end_flag);
    assert(v.loop_flag);
    assert(v.brr_address == loop);
    assert(v.brr_offset == 1);
    std::cout << "apu_dsp_brr_fetch_timing: PASS\n";
    return 0;
}

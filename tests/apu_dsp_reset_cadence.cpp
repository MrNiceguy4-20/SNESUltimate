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
int main() {
    APU apu;
    assert(apu.dsp_phase_ == 0);
    assert(apu.dsp_every_other_sample_);
    assert(apu.dsp_registers_[0x6C] == 0xE0);
    assert(apu.noise_lfsr_ == 0x4000);
    assert(apu.dsp_counter_ == 0);
    apu.writeDSP(0x4C, 0x01);
    apu.writeDSP(0x5C, 0x02);
    apu.advanceDSP(29);
    assert(apu.dsp_phase_ == 29);
    apu.advanceDSP(1);
    assert(!apu.dsp_every_other_sample_);
    assert(apu.dsp_phase_ == 30);
    apu.advanceDSP(1);
    assert(apu.dsp_kon_ == 0x00);
    assert(apu.dsp_koff_ == 0x00);
    apu.advanceDSP(30);
    assert(apu.dsp_phase_ == 29);
    apu.advanceDSP(1);
    assert(apu.dsp_every_other_sample_);
    apu.advanceDSP(1);
    assert(apu.dsp_kon_ == 0x01);
    assert(apu.dsp_koff_ == 0x02);
    apu.advanceDSP(30);
    apu.advanceDSP(1);
    apu.advanceDSP(31);
    apu.advanceDSP(1);
    assert((apu.dsp_new_kon_ & 0x01) == 0);
    std::cout << "apu_dsp_reset_cadence: PASS\n";
    return 0;
}

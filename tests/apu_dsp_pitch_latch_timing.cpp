#include <cassert>
#include <cstdint>
#include <iostream>
#define private public
#include "apu.hpp"
#undef private
namespace {
void configureVoice0(APU& apu) {
    auto& v = apu.voices_[0];
    v.active = true;
    v.keyon_delay = 0;
    v.brr_initialized = true;
    v.brr_lookahead_ready = true;
    v.decoded.fill(0x2000);
    v.envelope = 0x7F0;
    v.envelope_state = APU::EnvelopeState::Sustain;
    apu.dsp_registers_[0x05] = 0x00;
    apu.dsp_registers_[0x07] = 0x7F;
}
}
int main() {
    {
        APU apu;
        configureVoice0(apu);
        auto& v = apu.voices_[0];
        apu.dsp_phase_ = 21;
        apu.dsp_registers_[0x02] = 0x34;
        apu.dsp_registers_[0x03] = 0x01;
        apu.advanceDSP(1);
        assert(v.pitch_low_latch == 0x34);
        assert(!v.pitch_latch_valid);
        apu.dsp_registers_[0x02] = 0xAA;
        apu.dsp_registers_[0x03] = 0x02;
        apu.advanceDSP(1);
        assert(v.pitch_latch_valid);
        assert(v.pitch_latch == 0x0234);
        apu.dsp_registers_[0x03] = 0x03;
        apu.advanceDSP(9);
        assert(v.pitch_counter == 0x0234);
    }
    {
        APU apu;
        configureVoice0(apu);
        auto& v = apu.voices_[0];
        apu.dsp_phase_ = 21;
        apu.dsp_registers_[0x02] = 0x00;
        apu.dsp_registers_[0x03] = 0x01;
        apu.advanceDSP(11);
        assert(v.pitch_counter == 0x0100);
        apu.dsp_registers_[0x02] = 0x78;
        apu.dsp_registers_[0x03] = 0x04;
        apu.advanceDSP(21);
        assert(v.pitch_latch == 0x0100);
        apu.advanceDSP(2);
        assert(v.pitch_latch == 0x0478);
    }
    {
        APU apu;
        auto& v = apu.voices_[1];
        apu.dsp_phase_ = 0;
        apu.dsp_registers_[0x12] = 0x56;
        apu.dsp_registers_[0x13] = 0x03;
        apu.advanceDSP(1);
        assert(v.pitch_low_latch == 0x56);
        apu.dsp_registers_[0x12] = 0x99;
        apu.advanceDSP(1);
        assert(v.pitch_latch == 0x0356);
    }
    std::cout << "apu_dsp_pitch_latch_timing: PASS\n";
    return 0;
}

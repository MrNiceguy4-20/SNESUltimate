#include <cassert>
#include <cstdint>
#include <iostream>
#define private public
#include "apu.hpp"
#undef private
namespace {
void prepareVoice(APU& apu, int index) {
    auto& v = apu.voices_[index];
    v.active = true;
    v.keyon_delay = 0;
    v.brr_initialized = true;
    v.brr_lookahead_ready = true;
    v.decoded.fill(0x2000);
    v.sample_index = 0;
    v.pitch_counter = 0;
    v.envelope = 0;
    v.hidden_envelope = 0;
    v.envelope_state = APU::EnvelopeState::Attack;
    const uint8_t base = static_cast<uint8_t>(index << 4);
    apu.dsp_registers_[base + 2] = 0;
    apu.dsp_registers_[base + 3] = 0;
    apu.dsp_registers_[0x6C] = 0x20;
}
}
int main() {
    {
        APU apu;
        prepareVoice(apu, 1);
        auto& v = apu.voices_[1];
        constexpr uint8_t base = 0x10;
        apu.dsp_registers_[base + 5] = 0x00;
        apu.dsp_registers_[base + 7] = 0x7F;
        apu.dsp_phase_ = 0;
        apu.advanceDSP(1);
        assert(v.adsr0_latch == 0x00);
        apu.dsp_registers_[base + 5] = 0x8F;
        apu.advanceDSP(1);
        assert(v.envelope == 0x7F0);
        v.envelope = 0;
        v.hidden_envelope = 0;
        apu.advanceDSP(30);
        assert(apu.dsp_phase_ == 0);
        apu.advanceDSP(1);
        assert(v.adsr0_latch == 0x8F);
        apu.advanceDSP(1);
        assert(v.envelope == 0x400);
    }
    {
        APU apu;
        prepareVoice(apu, 0);
        auto& v = apu.voices_[0];
        v.adsr0_latch = 0x00;
        v.envelope = 0x5F0;
        v.hidden_envelope = 0x610;
        v.envelope_state = APU::EnvelopeState::Sustain;
        apu.dsp_registers_[0x07] = 0xFF;
        apu.advanceEnvelope(0);
        assert(v.envelope == 0x5F8);
        assert(v.hidden_envelope == 0x5F8);
    }
    std::cout << "apu_dsp_envelope_latch: PASS\n";
    return 0;
}

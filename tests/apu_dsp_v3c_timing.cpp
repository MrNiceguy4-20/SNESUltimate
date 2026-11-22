#include <cassert>
#include <cstdint>
#include <iostream>
#define private public
#include "apu.hpp"
#undef private
namespace {
void configureVoice(APU& apu, int index, int envelope) {
    auto& v = apu.voices_[index];
    v.active = true;
    v.keyon_delay = 0;
    v.brr_initialized = true;
    v.brr_lookahead_ready = true;
    v.decoded.fill(0x2000);
    v.envelope = envelope;
    v.envelope_state = APU::EnvelopeState::Sustain;
    const uint8_t base = static_cast<uint8_t>(index << 4);
    apu.dsp_registers_[base + 2] = 0x00;
    apu.dsp_registers_[base + 3] = 0x10;
    apu.dsp_registers_[base + 5] = 0x00;
    apu.dsp_registers_[base + 7] = 0x7F;
    apu.dsp_registers_[0x6C] = 0x20;
}
}
int main() {
    {
        APU apu;
        configureVoice(apu, 1, 0x7F0);
        auto& v = apu.voices_[1];
        apu.dsp_phase_ = 0;
        apu.advanceDSP(1);
        assert(v.output == 0);
        assert(v.sample_index == 0 && v.pitch_counter == 0);
        apu.advanceDSP(1);
        assert(v.output != 0);
        assert(v.pipeline_outx != 0);
        assert(v.sample_index == 0 && v.pitch_counter == 0);
        apu.advanceDSP(1);
        assert(v.sample_index == 1);
        assert(v.pitch_counter == 0);
    }
    {
        APU apu;
        configureVoice(apu, 1, 0x000);
        auto& v = apu.voices_[1];
        apu.dsp_phase_ = 0;
        apu.advanceDSP(2);
        assert(v.output == 0);
        assert(v.pipeline_envx == 0);
        assert(v.envelope == 0x7F0);
        apu.advanceDSP(31);
        assert(v.output == 0);
        apu.advanceDSP(1);
        assert(v.output != 0);
        assert(v.pipeline_envx == 0x7F);
    }
    std::cout << "apu_dsp_v3c_timing: PASS\n";
    return 0;
}

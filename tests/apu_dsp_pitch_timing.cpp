#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#define private public
#include "apu.hpp"
#undef private
namespace {
void configureActiveVoice(APU& apu, int index, uint16_t pitch) {
    auto& v = apu.voices_[index];
    v.active = true;
    v.keyon_delay = 0;
    v.brr_initialized = true;
    v.brr_lookahead_ready = true;
    v.sample_index = 0;
    v.pitch_counter = 0;
    v.envelope = 0x7F0;
    v.envelope_state = APU::EnvelopeState::Sustain;
    v.end_flag = false;
    v.loop_flag = false;
    v.next_end_flag = false;
    v.next_loop_flag = false;
    v.decoded.fill(0x2000);
    const uint8_t base = static_cast<uint8_t>(index << 4);
    apu.dsp_registers_[base + 2] = static_cast<uint8_t>(pitch);
    apu.dsp_registers_[base + 3] = static_cast<uint8_t>((pitch >> 8) & 0x3F);
    apu.dsp_registers_[base + 5] = 0x00;
    apu.dsp_registers_[base + 7] = 0x7F;
}
}
int main() {
    {
        APU apu;
        configureActiveVoice(apu, 1, 0x3FFF);
        apu.dsp_pmon_ = 0x02;
        apu.dsp_non_ = 0x00;
        apu.voiceSample(1, 32767);
        apu.advanceVoicePitch(1);
        const auto& v = apu.voices_[1];
        assert(v.sample_index == 3);
        assert(v.pitch_counter == 0x0FFF);
    }
    {
        APU apu;
        configureActiveVoice(apu, 1, 0x3FFF);
        auto& v = apu.voices_[1];
        v.sample_index = 15;
        for (int i = 16; i < 32; ++i) v.decoded[i] = 0x3000;
        apu.dsp_pmon_ = 0x02;
        apu.dsp_non_ = 0x00;
        apu.voiceSample(1, 32767);
        apu.advanceVoicePitch(1);
        assert(v.sample_index == 2);
        assert(v.pitch_counter == 0x0FFF);
        assert(v.decoded[0] == 0x3000);
    }
    {
        APU apu;
        configureActiveVoice(apu, 1, 0x0800);
        apu.dsp_pmon_ = 0x02;
        apu.dsp_non_ = 0x02;
        apu.voiceSample(1, 32767);
        apu.advanceVoicePitch(1);
        const auto& v = apu.voices_[1];
        assert(v.sample_index == 0);
        assert(v.pitch_counter == 0x0800);
    }
    std::cout << "apu_dsp_pitch_timing: PASS\n";
    return 0;
}

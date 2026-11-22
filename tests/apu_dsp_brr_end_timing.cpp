#include <cassert>
#include <cstdint>
#include <iostream>
#define private public
#include "apu.hpp"
#undef private
namespace {
void prepareVoice0(APU& apu, uint8_t header) {
    auto& v = apu.voices_[0];
    v.active = true;
    v.keyon_delay = 0;
    v.brr_initialized = true;
    v.fetch_v2_valid = true;
    v.brr_lookahead_ready = false;
    v.brr_address = 0x2000;
    v.brr_offset = 1;
    v.decoded.fill(0x2000);
    v.sample_index = 0;
    v.pitch_counter = 0;
    v.envelope = 0x600;
    v.hidden_envelope = 0x600;
    v.envelope_state = APU::EnvelopeState::Sustain;
    apu.ram_[0x2000] = header;
    apu.ram_[0x2001] = 0x11;
    apu.ram_[0x2002] = 0x22;
    apu.dsp_registers_[0x02] = 0;
    apu.dsp_registers_[0x03] = 0;
    apu.dsp_registers_[0x05] = 0;
    apu.dsp_registers_[0x07] = 0x7F;
    apu.dsp_registers_[0x6C] = 0x20;
    apu.dsp_phase_ = 25;
}
}
int main() {
    {
        APU apu;
        prepareVoice0(apu, 0x01);
        apu.advanceDSP(1);
        assert(apu.voices_[0].brr_v3_valid);
        assert((apu.voices_[0].brr_header_latch & 3) == 1);
        apu.ram_[0x2000] = 0x00;
        apu.advanceDSP(5);
        const auto& v = apu.voices_[0];
        assert(v.output != 0);
        assert(v.envelope == 0);
        assert(v.envelope_state == APU::EnvelopeState::Release);
        assert(v.pipeline_envx == 0x60);
    }
    {
        APU apu;
        prepareVoice0(apu, 0x00);
        apu.advanceDSP(1);
        apu.ram_[0x2000] = 0x01;
        apu.advanceDSP(5);
        const auto& v = apu.voices_[0];
        assert(v.output != 0);
        assert(v.envelope == 0x7F0);
        assert(v.envelope_state != APU::EnvelopeState::Release);
    }
    std::cout << "apu_dsp_brr_end_timing: PASS\n";
    return 0;
}

#include <cassert>
#include <cstdint>
#include <iostream>
#define private public
#include "apu.hpp"
#undef private
namespace {
void configureVoice(APU& apu, int index) {
    auto& v = apu.voices_[index];
    v.active = true;
    v.keyon_delay = 0;
    v.brr_initialized = true;
    v.brr_lookahead_ready = true;
    v.decoded.fill(0x2000);
    v.envelope = 0x7F0;
    v.envelope_state = APU::EnvelopeState::Sustain;
    const uint8_t base = static_cast<uint8_t>(index << 4);
    apu.dsp_registers_[base + 2] = 0;
    apu.dsp_registers_[base + 3] = 0;
    apu.dsp_registers_[base + 5] = 0;
    apu.dsp_registers_[base + 7] = 0x7F;
}
}
int main() {
    {
        APU apu;
        configureVoice(apu, 1);
        apu.dsp_phase_ = 0;
        apu.dsp_registers_[0x10] = 0x40;
        apu.dsp_registers_[0x11] = 0x40;
        apu.advanceDSP(2);
        assert(apu.voices_[1].output != 0);
        apu.advanceDSP(1);
        assert(apu.dsp_main_out_[0] != 0);
        apu.dsp_registers_[0x10] = 0;
        apu.dsp_registers_[0x11] = 0;
        apu.advanceDSP(1);
        assert(apu.dsp_main_out_[0] != 0);
        assert(apu.dsp_main_out_[1] == 0);
    }
    {
        APU apu;
        apu.dsp_esa_ = 0x30;
        apu.echo_offset_ = 0;
        apu.ram_[0x3000] = 0x00;
        apu.ram_[0x3001] = 0x40;
        for (int tap = 0; tap < 8; ++tap)
            apu.dsp_registers_[0x0F + tap * 0x10] = 0;
        apu.dsp_registers_[0x7F] = 0x40;
        for (uint8_t p = 22; p <= 25; ++p) apu.processEchoPhase(p);
        assert(apu.dsp_echo_in_[0] == 0x2000);
    }
    {
        APU apu;
        apu.echo_pointer_ = 0x3200;
        apu.echo_length_ = 4;
        apu.echo_offset_ = 0;
        apu.dsp_echo_enabled_ = 0x00;
        apu.dsp_echo_out_[0] = 0x1234;
        apu.dsp_echo_out_[1] = 0x5678;
        apu.dsp_registers_[0x6C] = 0x20;
        apu.processEchoPhase(29);
        assert(apu.ram_[0x3200] == 0x34 && apu.ram_[0x3201] == 0x12);
        apu.processEchoPhase(30);
        assert(apu.ram_[0x3202] == 0x00 && apu.ram_[0x3203] == 0x00);
    }
    {
        APU apu;
        apu.dsp_main_out_[0] = 0x2000;
        apu.dsp_main_out_[1] = -0x2000;
        apu.dsp_registers_[0x0C] = 0x7F;
        apu.dsp_registers_[0x1C] = 0x7F;
        apu.dsp_registers_[0x2C] = 0;
        apu.dsp_registers_[0x3C] = 0;
        apu.dsp_registers_[0x6C] = 0;
        apu.processEchoPhase(26);
        assert(apu.audio_queue_.empty());
        apu.processEchoPhase(27);
        assert(apu.audio_queue_.size() == 2);
        assert(apu.audio_queue_[0] > 0 && apu.audio_queue_[1] < 0);
    }
    std::cout << "apu_dsp_echo_pipeline_timing: PASS\n";
    return 0;
}

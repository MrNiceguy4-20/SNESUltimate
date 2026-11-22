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
    auto& v = apu.voices_[0];
    constexpr uint16_t block = 0x2000;
    v.active = true;
    v.fetch_v2_valid = true;
    v.brr_address = block;
    v.brr_offset = 7;
    v.brr_decode_destination = 0;
    v.brr_decode_allowed = true;
    apu.ram_[block + 0] = 0x03;
    apu.ram_[block + 7] = 0x12;
    apu.ram_[block + 8] = 0x34;
    apu.processVoiceBRRPhase(25);
    assert(v.brr_v3_valid);
    assert(!v.pending_endx_set);
    assert(apu.dsp_registers_[0x7C] == 0);
    apu.processVoiceBRRPhase(31);
    assert(!v.brr_v3_valid);
    assert(v.pending_endx_set);
    assert(v.brr_offset == 1);
    assert(apu.dsp_registers_[0x7C] == 0);
    apu.processVoiceWritebackPhase(0);
    assert(!v.pending_endx_set);
    assert((apu.dsp_endx_buf_ & 0x01) != 0);
    assert((apu.dsp_registers_[0x7C] & 0x01) == 0);
    apu.processVoiceWritebackPhase(2);
    assert((apu.dsp_registers_[0x7C] & 0x01) != 0);
    v.pending_endx_set = false;
    v.active = true;
    v.brr_initialized = true;
    v.keyon_delay = 0;
    v.end_flag = true;
    v.loop_flag = true;
    v.sample_index = 15;
    v.pitch_counter = 0;
    v.effective_pitch = 0x1000;
    apu.advanceVoicePitch(0);
    assert(!v.pending_endx_set);
    apu.dsp_registers_[0x7C] = 0;
    v.pending_endx_set = true;
    v.pending_kon_clear = true;
    apu.processVoiceWritebackPhase(0);
    assert((apu.dsp_endx_buf_ & 0x01) == 0);
    std::cout << "apu_dsp_endx_v4_timing: PASS\n";
    return 0;
}

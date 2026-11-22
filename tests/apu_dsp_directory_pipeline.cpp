#include <cassert>
#include <cstdint>
#include <iostream>
#define private public
#include "apu.hpp"
#undef private
int main() {
    {
        APU apu;
        apu.dsp_dir_ = 0x10;
        apu.dsp_srcn_ = 3;
        apu.dsp_registers_[0x04] = 5;
        apu.dsp_registers_[0x14] = 6;
        apu.dsp_phase_ = 17;
        apu.advanceDSP(1);
        assert(apu.dsp_dir_addr_ == 0x100C);
        assert(apu.dsp_srcn_ == 5);
        apu.advanceDSP(3);
        assert(apu.dsp_dir_addr_ == 0x1014);
        assert(apu.dsp_srcn_ == 6);
    }
    {
        APU apu;
        auto& v = apu.voices_[0];
        apu.dsp_dir_ = 0x10;
        apu.dsp_registers_[0x5D] = 0x10;
        apu.dsp_srcn_ = 0;
        apu.dsp_dir_addr_ = 0x1000;
        for (int i = 0; i < 8; ++i) apu.dsp_registers_[i * 0x10 + 4] = 0;
        v.active = true;
        v.keyon_delay = 0;
        v.fetch_v2_valid = true;
        apu.ram_[0x1002] = 0x00;
        apu.ram_[0x1003] = 0x30;
        apu.dsp_phase_ = 21;
        apu.advanceDSP(1);
        assert(v.loop_address == 0x3000);
        apu.ram_[0x1002] = 0x00;
        apu.ram_[0x1003] = 0x40;
        apu.advanceDSP(32);
        assert(v.loop_address == 0x4000);
        v.brr_address = 0x2000;
        v.brr_offset = 7;
        v.brr_decode_destination = 0;
        v.brr_staging.fill(0);
        v.brr_staging[0] = 0x03;
        v.brr_initialized = false;
        apu.ram_[0x2000] = 0x03;
        apu.ram_[0x2007] = 0x11;
        apu.ram_[0x2008] = 0x22;
        apu.processVoiceBRRPhase(25);
        apu.processVoiceBRRPhase(31);
        assert(v.brr_address == 0x4000);
    }
    std::cout << "apu_dsp_directory_pipeline: PASS\n";
    return 0;
}

#include "apu.hpp"
#include "bus.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace {
constexpr std::array<uint8_t, 16> TEST_CPU_RATE_DENOMINATOR = {
    3, 5, 9, 17, 4, 6, 10, 18, 6, 8, 12, 20, 10, 12, 16, 24
};
constexpr std::array<uint8_t, 64> IPL_ROM = {
    0xCD,0xEF,0xBD,0xE8,0x00,0xC6,0x1D,0xD0,0xFC,0x8F,0xAA,0xF4,0x8F,0xBB,0xF5,0x78,
    0xCC,0xF4,0xD0,0xFB,0x2F,0x19,0xEB,0xF4,0xD0,0xFC,0x7E,0xF4,0xD0,0x0B,0xE4,0xF5,
    0xCB,0xF4,0xD7,0x00,0xFC,0xD0,0xF3,0xAB,0x01,0x10,0xEF,0x7E,0xF4,0x10,0xEB,0xBA,
    0xF6,0xDA,0x00,0xBA,0xF4,0xC4,0xF4,0xDD,0x5D,0xD0,0xDB,0x1F,0x00,0x00,0xC0,0xFF
};
constexpr bool spc700HasOneByteDummyFetch(uint8_t opcode) {
    if ((opcode & 0x0F) == 0x01) return true;
    switch (opcode) {
    case 0x00: case 0x06: case 0x0D: case 0x0F:
    case 0x19: case 0x1C: case 0x1D:
    case 0x20: case 0x26: case 0x2D:
    case 0x39: case 0x3C: case 0x3D:
    case 0x40: case 0x46: case 0x4D:
    case 0x59: case 0x5C: case 0x5D:
    case 0x60: case 0x66: case 0x6D: case 0x6F:
    case 0x79: case 0x7C: case 0x7D: case 0x7F:
    case 0x80: case 0x86: case 0x8E:
    case 0x99: case 0x9C: case 0x9D: case 0x9E: case 0x9F:
    case 0xA0: case 0xA6: case 0xAE: case 0xAF:
    case 0xB9: case 0xBC: case 0xBD: case 0xBE: case 0xBF:
    case 0xC0: case 0xC6: case 0xCE: case 0xCF:
    case 0xDC: case 0xDD: case 0xDF:
    case 0xE0: case 0xE6: case 0xED: case 0xEE: case 0xEF:
    case 0xFC: case 0xFD: case 0xFF:
        return true;
    default:
        return false;
    }
}
}
APU::APU() { power(); }
APU::~APU() = default;
void APU::power() {
    ram_.fill(0);
    for (auto& timer : timers_) timer.target = 0;
    reset();
}
void APU::reset() {
    const std::array<uint8_t, 3> timer_targets = {
        timers_[0].target, timers_[1].target, timers_[2].target
    };
    cpu_to_apu_.fill(0);
    apu_to_cpu_.fill(0);
    dsp_registers_.fill(0);
    dsp_registers_[0x6C] = 0xE0;
    timers_.fill(Timer{});
    for (int i = 0; i < 3; ++i) timers_[i].target = timer_targets[static_cast<size_t>(i)];
    voices_.fill(Voice{});
    audio_queue_.clear();
    dsp_phase_ = 0;
    dsp_new_kon_ = 0;
    dsp_kon_ = 0;
    dsp_koff_ = 0;
    dsp_every_other_sample_ = true;
    dsp_pmon_ = dsp_non_ = dsp_eon_ = dsp_dir_ = dsp_esa_ = 0;
    dsp_srcn_ = 0;
    dsp_dir_addr_ = 0;
    dsp_echo_enabled_ = dsp_registers_[0x6C];
    dsp_endx_buf_ = dsp_envx_buf_ = dsp_outx_buf_ = 0;
    noise_lfsr_ = 0x4000;
    dsp_counter_ = 0;
    noise_sample_ = 0;
    dsp_voice_output_latch_ = 0;
    echo_history_ = {};
    echo_history_pos_ = 0;
    echo_offset_ = 0;
    echo_length_ = 0;
    echo_pointer_ = 0;
    dsp_main_out_ = {};
    dsp_echo_out_ = {};
    dsp_echo_in_ = {};
    dsp_output_left_ = 0;
    pc_ = 0xFFC0;
    a_ = x_ = y_ = sp_ = psw_ = 0;
    control_ = 0xB0;
    test_ = 0x0A;
    dsp_address_ = 0;
    ram_f8_ = ram_f9_ = 0;
    cycles_ = 0;
    cpu_wait_fraction_ = 0;
    pending_port_write_count_ = 0;
    pending_port_read_count_ = 0;
    pending_movw_low_ = 0;
    staged_port_write_count_ = 0;
    scheduled_instruction_timing_valid_ = false;
    executing_instruction_ = false;
    sleeping_ = false;
    stopped_ = false;
}
uint8_t APU::readPort(uint8_t port) {
    if (bus_) synchronizePorts(bus_->masterClock());
    return apu_to_cpu_[port & 0x03];
}
void APU::writePort(uint8_t port, uint8_t value) {
    cpu_to_apu_[port & 0x03] = value;
}
void APU::beginScheduledInstruction(uint64_t startTimeFp, uint64_t masterClocksPerApuCycleFp) {
    scheduled_instruction_start_fp_ = startTimeFp;
    scheduled_master_clocks_per_apu_cycle_fp_ = masterClocksPerApuCycleFp;
    scheduled_instruction_timing_valid_ = true;
}
uint64_t APU::nextPortEventMasterClock() const {
    uint64_t next = std::numeric_limits<uint64_t>::max();
    if (pending_port_write_count_) next = pending_port_writes_[0].due_master_clock;
    if (pending_port_read_count_ && pending_port_reads_[0].due_master_clock < next) next = pending_port_reads_[0].due_master_clock;
    return next;
}
void APU::commitPortWrite(uint8_t port, uint8_t value, uint64_t spcCycle) {
    apu_to_cpu_[port & 0x03] = value;
    if (port_write_observer_) {
        port_write_observer_(port & 0x03, value, (control_ & 0x80) != 0, spcCycle);
    }
}
void APU::commitPortRead(const PendingPortRead& event) {
    const uint8_t value = cpu_to_apu_[event.port & 0x03];
    switch (event.kind) {
    case PendingPortReadKind::LoadA: a_ = value; setNZ(a_); break;
    case PendingPortReadKind::LoadX: x_ = value; setNZ(x_); break;
    case PendingPortReadKind::LoadY: y_ = value; setNZ(y_); break;
    case PendingPortReadKind::CompareY: compare(y_, value); break;
    case PendingPortReadKind::MovwLow: pending_movw_low_ = value; break;
    case PendingPortReadKind::MovwHigh: {
        const uint16_t word = static_cast<uint16_t>(pending_movw_low_ | (static_cast<uint16_t>(value) << 8));
        a_ = static_cast<uint8_t>(word);
        y_ = static_cast<uint8_t>(word >> 8);
        setFlag(Z, word == 0);
        setFlag(N, (word & 0x8000) != 0);
        break;
    }
    case PendingPortReadKind::None: break;
    }
}
void APU::schedulePortRead(uint8_t port, PendingPortReadKind kind, uint8_t logicalCycle, uint8_t testSpeedAtStart, uint8_t waitFractionAtStart) {
    if (!scheduled_instruction_timing_valid_ || pending_port_read_count_ >= pending_port_reads_.size()) {
        PendingPortRead immediate{};
        immediate.port = port;
        immediate.kind = kind;
        commitPortRead(immediate);
        return;
    }
    constexpr uint64_t FP_ONE = uint64_t{1} << 16;
    const uint8_t denom = TEST_CPU_RATE_DENOMINATOR[testSpeedAtStart];
    const uint16_t thirdsBefore = static_cast<uint16_t>(logicalCycle - 1) * denom + waitFractionAtStart;
    const uint64_t elapsedBefore = thirdsBefore / 3u;
    const uint64_t dueFp = scheduled_instruction_start_fp_ + elapsedBefore * scheduled_master_clocks_per_apu_cycle_fp_;
    const uint64_t dueMaster = (dueFp + FP_ONE - 1) >> 16;
    pending_port_reads_[pending_port_read_count_++] = {dueMaster, cycles_ + elapsedBefore, static_cast<uint8_t>(port & 0x03), kind};
}
void APU::synchronizePorts(uint64_t masterClock) {
    for (;;) {
        const uint64_t nextWrite = pending_port_write_count_ ? pending_port_writes_[0].due_master_clock : std::numeric_limits<uint64_t>::max();
        const uint64_t nextRead = pending_port_read_count_ ? pending_port_reads_[0].due_master_clock : std::numeric_limits<uint64_t>::max();
        const uint64_t next = nextWrite < nextRead ? nextWrite : nextRead;
        if (next > masterClock) break;
        if (nextRead <= nextWrite) {
            const PendingPortRead event = pending_port_reads_[0];
            for (uint8_t i = 1; i < pending_port_read_count_; ++i) pending_port_reads_[i - 1] = pending_port_reads_[i];
            --pending_port_read_count_;
            commitPortRead(event);
        } else {
            const PendingPortWrite event = pending_port_writes_[0];
            for (uint8_t i = 1; i < pending_port_write_count_; ++i) pending_port_writes_[i - 1] = pending_port_writes_[i];
            --pending_port_write_count_;
            commitPortWrite(event.port, event.value, event.spc_cycle);
        }
    }
}
void APU::scheduleStagedPortWrites(uint8_t used, uint8_t testSpeedAtStart, uint8_t waitFractionAtStart) {
    if (!staged_port_write_count_) return;
    if (!scheduled_instruction_timing_valid_) {
        for (uint8_t i = 0; i < staged_port_write_count_; ++i)
            commitPortWrite(staged_port_writes_[i].port, staged_port_writes_[i].value, cycles_ + used);
        staged_port_write_count_ = 0;
        return;
    }
    constexpr uint64_t FP_ONE = uint64_t{1} << 16;
    const uint8_t denom = TEST_CPU_RATE_DENOMINATOR[testSpeedAtStart];
    for (uint8_t i = 0; i < staged_port_write_count_; ++i) {
        const uint8_t logicalCycle = static_cast<uint8_t>(used - staged_port_write_count_ + i + 1);
        const uint16_t thirdsBefore = static_cast<uint16_t>(logicalCycle - 1) * denom + waitFractionAtStart;
        const uint64_t elapsedBefore = thirdsBefore / 3u;
        const uint64_t dueFp = scheduled_instruction_start_fp_ + elapsedBefore * scheduled_master_clocks_per_apu_cycle_fp_;
        const uint64_t dueMaster = (dueFp + FP_ONE - 1) >> 16;
        if (pending_port_write_count_ < pending_port_writes_.size()) {
            pending_port_writes_[pending_port_write_count_++] = {dueMaster, cycles_ + elapsedBefore, staged_port_writes_[i].port, staged_port_writes_[i].value};
        } else {
            commitPortWrite(staged_port_writes_[i].port, staged_port_writes_[i].value, cycles_ + elapsedBefore);
        }
    }
    staged_port_write_count_ = 0;
    scheduled_instruction_timing_valid_ = false;
}
void APU::setStateForTesting(const State& state) {
    pc_ = state.pc;
    a_ = state.a;
    x_ = state.x;
    y_ = state.y;
    sp_ = state.sp;
    psw_ = state.psw;
    sleeping_ = false;
    stopped_ = false;
    cycles_ = 0;
    cpu_wait_fraction_ = 0;
    pending_port_write_count_ = 0;
    pending_port_read_count_ = 0;
    pending_movw_low_ = 0;
    staged_port_write_count_ = 0;
    scheduled_instruction_timing_valid_ = false;
    executing_instruction_ = false;
}
uint8_t APU::read(uint16_t address) {
    return readInternal(address);
}
void APU::write(uint16_t address, uint8_t value) {
    writeInternal(address, value);
}
uint8_t APU::readInternal(uint16_t address) {
    if (flat_test_memory_) {
        const uint8_t value = ram_[address];
        return value;
    }
    if (address >= 0xFFC0 && (control_ & 0x80) != 0) {
        return IPL_ROM[address - 0xFFC0];
    }
    if (address < 0x00F0 || address > 0x00FF) {
        if ((test_ & 0x04) != 0) return 0x5A;
        return ram_[address];
    }
    switch (address) {
    case 0x00F0:
    case 0x00F1:
    case 0x00FA:
    case 0x00FB:
    case 0x00FC:
        return 0;
    case 0x00F2:
        return static_cast<uint8_t>(dsp_address_ & 0x7F);
    case 0x00F3:
        return readDSP(dsp_address_ & 0x7F);
    case 0x00F4: case 0x00F5: case 0x00F6: case 0x00F7:
        return cpu_to_apu_[address - 0x00F4];
    case 0x00F8:
        return ram_f8_;
    case 0x00F9:
        return ram_f9_;
    case 0x00FD: case 0x00FE: case 0x00FF: {
        Timer& timer = timers_[address - 0x00FD];
        const uint8_t value = timer.output & 0x0F;
        timer.output = 0;
        return value;
    }
    default:
        return 0;
    }
}
uint8_t APU::readBackingRamForDummyCycle(uint16_t address) {
    const uint8_t value = (test_ & 0x04) != 0 ? 0x5A : ram_[address];
    return value;
}
void APU::writeInternal(uint16_t address, uint8_t value) {
    if (flat_test_memory_) {
        ram_[address] = value;
        return;
    }
    const bool register_window = address >= 0x00F0 && address <= 0x00FF;
    const bool ram_writes_enabled = (test_ & 0x02) != 0 && (test_ & 0x04) == 0;
    if (ram_writes_enabled) ram_[address] = value;
    if (!register_window) {
        return;
    }
    switch (address) {
    case 0x00F0:
        if (!flag(P)) test_ = value;
        break;
    case 0x00F1:
        writeControl(value);
        break;
    case 0x00F2:
        dsp_address_ = value;
        break;
    case 0x00F3:
        if ((dsp_address_ & 0x80) == 0) {
            writeDSP(dsp_address_ & 0x7F, value);
        }
        break;
    case 0x00F4: case 0x00F5: case 0x00F6: case 0x00F7: {
        const uint8_t port = static_cast<uint8_t>(address - 0x00F4);
        if (executing_instruction_ && staged_port_write_count_ < staged_port_writes_.size()) {
            staged_port_writes_[staged_port_write_count_++] = {port, value};
        } else {
            commitPortWrite(port, value, cycles_);
        }
        break;
    }
    case 0x00F8:
        ram_f8_ = value;
        break;
    case 0x00F9:
        ram_f9_ = value;
        break;
    case 0x00FA: case 0x00FB: case 0x00FC:
        timers_[address - 0x00FA].target = value;
        break;
    case 0x00FD: case 0x00FE: case 0x00FF:
        break;
    default:
        break;
    }
}
void APU::writeControl(uint8_t value) {
    const uint8_t old = control_;
    control_ = value;
    if (value & 0x10) {
        cpu_to_apu_[0] = 0;
        cpu_to_apu_[1] = 0;
    }
    if (value & 0x20) {
        cpu_to_apu_[2] = 0;
        cpu_to_apu_[3] = 0;
    }
    for (int i = 0; i < 3; ++i) {
        const bool was_enabled = (old & (1u << i)) != 0;
        const bool enabled = (value & (1u << i)) != 0;
        if (!was_enabled && enabled) {
            timers_[i].stage = 0;
            timers_[i].output = 0;
        }
        timers_[i].enabled = enabled;
    }
}
void APU::takeAudioSamples(std::vector<int16_t>& out) {
    audio_queue_.drainTo(out);
}
std::vector<int16_t> APU::takeAudioSamples() {
    std::vector<int16_t> out;
    out.reserve(audio_queue_.size());
    takeAudioSamples(out);
    return out;
}
int16_t APU::clamp16(int value) const {
    return static_cast<int16_t>(std::clamp(value, -32768, 32767));
}
uint8_t APU::readDSP(uint8_t address) const {
    return dsp_registers_[address & 0x7F];
}
void APU::writeDSP(uint8_t address, uint8_t value) {
    address &= 0x7F;
    if ((address & 0x0F) == 0x08) {
        dsp_envx_buf_ = value;
    } else if ((address & 0x0F) == 0x09) {
        dsp_outx_buf_ = value;
    }
    if (address == 0x7C) {
        dsp_endx_buf_ = 0;
        dsp_registers_[0x7C] = 0;
        return;
    }
    dsp_registers_[address] = value;
    if (address == 0x4C) {
        dsp_new_kon_ = value;
    }
}
void APU::keyOnVoice(int index) {
    Voice& v = voices_[index];
    v.decoded.fill(0);
    v.sample_index = 0;
    v.pitch_counter = 0;
    v.pitch_latch = 0;
    v.effective_pitch = 0;
    v.pitch_low_latch = 0;
    v.adsr0_latch = 0;
    v.pitch_latch_valid = false;
    v.previous1 = v.previous2 = 0;
    v.envelope = 0;
    v.hidden_envelope = 0;
    v.output = 0;
    v.pipeline_envx = 0;
    v.pipeline_outx = 0;
    v.pending_endx_set = false;
    v.pending_kon_clear = true;
    v.active = true;
    v.keyon_delay = 5;
    v.end_flag = v.loop_flag = false;
    v.next_end_flag = v.next_loop_flag = false;
    v.fetch_v1_valid = false;
    v.fetch_v2_valid = false;
    v.brr_initialized = false;
    v.brr_offset = 1;
    v.brr_header_latch = 0;
    v.brr_byte_latch = 0;
    v.brr_v3_valid = false;
    v.brr_decode_allowed = false;
    v.brr_staging.fill(0);
    v.brr_staging_mask = 0;
    v.brr_decode_destination = 0;
    v.brr_lookahead_ready = false;
    v.kon_source_captured = false;
    v.envelope_state = EnvelopeState::Attack;
}
void APU::decodeBRRGroup(Voice& v, size_t destination, uint8_t header,
                         uint8_t first, uint8_t second) {
    const int range = header >> 4;
    const int filter = (header >> 2) & 3;
    const uint16_t packed = static_cast<uint16_t>((static_cast<uint16_t>(first) << 8) | second);
    for (int n = 0; n < 4; ++n) {
        int nibble = static_cast<int>((packed >> (12 - n * 4)) & 0x0F);
        if (nibble & 8) nibble -= 16;
        const int scaled = nibble * (1 << range);
        int sample = scaled / 2;
        if (scaled < 0 && (scaled % 2) != 0) --sample;
        if (range >= 13) sample = nibble < 0 ? -0x800 : 0;
        const int p1 = static_cast<int>(v.previous1);
        const int p2 = static_cast<int>(v.previous2) >> 1;
        switch (filter) {
        case 1:
            sample += p1 >> 1;
            sample += (-p1) >> 5;
            break;
        case 2:
            sample += p1; sample -= p2; sample += p2 >> 4; sample += (p1 * -3) >> 6;
            break;
        case 3:
            sample += p1; sample -= p2; sample += (p1 * -13) >> 7; sample += (p2 * 3) >> 4;
            break;
        default:
            break;
        }
        sample = std::clamp(sample, -32768, 32767);
        const int16_t decoded = static_cast<int16_t>(static_cast<int16_t>(sample) * 2);
        v.decoded[destination + static_cast<size_t>(n)] = decoded;
        v.previous2 = v.previous1;
        v.previous1 = decoded;
    }
}
int16_t APU::gaussianInterpolate(const Voice& v) const {
    static constexpr std::array<int16_t, 512> gauss = {
           0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
           1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   2,   2,   2,   2,   2,
           2,   2,   3,   3,   3,   3,   3,   4,   4,   4,   4,   4,   5,   5,   5,   5,
           6,   6,   6,   6,   7,   7,   7,   8,   8,   8,   9,   9,   9,  10,  10,  10,
          11,  11,  11,  12,  12,  13,  13,  14,  14,  15,  15,  15,  16,  16,  17,  17,
          18,  19,  19,  20,  20,  21,  21,  22,  23,  23,  24,  24,  25,  26,  27,  27,
          28,  29,  29,  30,  31,  32,  32,  33,  34,  35,  36,  36,  37,  38,  39,  40,
          41,  42,  43,  44,  45,  46,  47,  48,  49,  50,  51,  52,  53,  54,  55,  56,
          58,  59,  60,  61,  62,  64,  65,  66,  67,  69,  70,  71,  73,  74,  76,  77,
          78,  80,  81,  83,  84,  86,  87,  89,  90,  92,  94,  95,  97,  99, 100, 102,
         104, 106, 107, 109, 111, 113, 115, 117, 118, 120, 122, 124, 126, 128, 130, 132,
         134, 137, 139, 141, 143, 145, 147, 150, 152, 154, 156, 159, 161, 163, 166, 168,
         171, 173, 175, 178, 180, 183, 186, 188, 191, 193, 196, 199, 201, 204, 207, 210,
         212, 215, 218, 221, 224, 227, 230, 233, 236, 239, 242, 245, 248, 251, 254, 257,
         260, 263, 267, 270, 273, 276, 280, 283, 286, 290, 293, 297, 300, 304, 307, 311,
         314, 318, 321, 325, 328, 332, 336, 339, 343, 347, 351, 354, 358, 362, 366, 370,
         374, 378, 381, 385, 389, 393, 397, 401, 405, 410, 414, 418, 422, 426, 430, 434,
         439, 443, 447, 451, 456, 460, 464, 469, 473, 477, 482, 486, 491, 495, 499, 504,
         508, 513, 517, 522, 527, 531, 536, 540, 545, 550, 554, 559, 563, 568, 573, 577,
         582, 587, 592, 596, 601, 606, 611, 615, 620, 625, 630, 635, 640, 644, 649, 654,
         659, 664, 669, 674, 678, 683, 688, 693, 698, 703, 708, 713, 718, 723, 728, 732,
         737, 742, 747, 752, 757, 762, 767, 772, 777, 782, 787, 792, 797, 802, 806, 811,
         816, 821, 826, 831, 836, 841, 846, 851, 855, 860, 865, 870, 875, 880, 884, 889,
         894, 899, 904, 908, 913, 918, 923, 927, 932, 937, 941, 946, 951, 955, 960, 965,
         969, 974, 978, 983, 988, 992, 997,1001,1005,1010,1014,1019,1023,1027,1032,1036,
        1040,1045,1049,1053,1057,1061,1066,1070,1074,1078,1082,1086,1090,1094,1098,1102,
        1106,1109,1113,1117,1121,1125,1128,1132,1136,1139,1143,1146,1150,1153,1157,1160,
        1164,1167,1170,1174,1177,1180,1183,1186,1190,1193,1196,1199,1202,1205,1207,1210,
        1213,1216,1219,1221,1224,1227,1229,1232,1234,1237,1239,1241,1244,1246,1248,1251,
        1253,1255,1257,1259,1261,1263,1265,1267,1269,1270,1272,1274,1275,1277,1279,1280,
        1282,1283,1284,1286,1287,1288,1290,1291,1292,1293,1294,1295,1296,1297,1297,1298,
        1299,1300,1300,1301,1302,1302,1303,1303,1303,1304,1304,1304,1304,1304,1305,1305
    };
    const int offset = static_cast<int>((v.pitch_counter >> 4) & 0xFF);
    const size_t i = v.sample_index;
    int out = (static_cast<int>(gauss[255 - offset]) * v.decoded[i + 0]) >> 11;
    out += (static_cast<int>(gauss[511 - offset]) * v.decoded[i + 1]) >> 11;
    out += (static_cast<int>(gauss[256 + offset]) * v.decoded[i + 2]) >> 11;
    out = static_cast<int16_t>(out);
    out += (static_cast<int>(gauss[offset]) * v.decoded[i + 3]) >> 11;
    return static_cast<int16_t>(clamp16(out) & ~1);
}
bool APU::dspCounterPoll(uint8_t rate) const {
    static constexpr std::array<uint16_t, 32> periods = {
        30721, 2048, 1536, 1280, 1024, 768, 640, 512,
        384, 320, 256, 192, 160, 128, 96, 80,
        64, 48, 40, 32, 24, 20, 16, 12,
        10, 8, 6, 5, 4, 3, 2, 1
    };
    static constexpr std::array<uint16_t, 32> offsets = {
        1, 0, 1040, 536, 0, 1040, 536, 0,
        1040, 536, 0, 1040, 536, 0, 1040, 536,
        0, 1040, 536, 0, 1040, 536, 0, 1040,
        536, 0, 1040, 536, 0, 1040, 0, 0
    };
    if (rate == 0) return false;
    return ((static_cast<uint32_t>(dsp_counter_) + offsets[rate]) % periods[rate]) == 0;
}
void APU::advanceEnvelope(int index) {
    Voice& v = voices_[index];
    if (!v.active) return;
    const uint8_t base = static_cast<uint8_t>(index << 4);
    if (v.envelope_state == EnvelopeState::Release) {
        v.envelope = std::max(0, v.envelope - 8);
        v.hidden_envelope = v.envelope;
        return;
    }
    const uint8_t adsr0 = v.adsr0_latch;
    const uint8_t adsr1 = dsp_registers_[base + 6];
    const uint8_t gain = dsp_registers_[base + 7];
    int next = v.envelope;
    uint8_t rate = 31;
    if (adsr0 & 0x80) {
        if (v.envelope_state == EnvelopeState::Attack) {
            rate = static_cast<uint8_t>(((adsr0 & 0x0F) << 1) + 1);
            next += rate < 31 ? 0x20 : 0x400;
        } else {
            next--;
            next -= next >> 8;
            rate = v.envelope_state == EnvelopeState::Decay
                ? static_cast<uint8_t>(0x10 + ((adsr0 >> 3) & 0x0E))
                : static_cast<uint8_t>(adsr1 & 0x1F);
        }
    } else {
        const uint8_t mode = gain >> 5;
        if (mode < 4) {
            next = static_cast<int>(gain) << 4;
            rate = 31;
        } else {
            rate = gain & 0x1F;
            switch (mode) {
            case 4: next -= 0x20; break;
            case 5: next--; next -= next >> 8; break;
            case 6: next += 0x20; break;
            case 7:
                next += (static_cast<unsigned>(v.hidden_envelope) >= 0x600u) ? 0x08 : 0x20;
                break;
            default: break;
            }
        }
    }
    if (v.envelope_state == EnvelopeState::Decay &&
        (next >> 8) == static_cast<int>(adsr1 >> 5)) {
        v.envelope_state = EnvelopeState::Sustain;
    }
    v.hidden_envelope = next;
    if (static_cast<unsigned>(next) > 0x7FFu) {
        next = next < 0 ? 0 : 0x7FF;
        if (v.envelope_state == EnvelopeState::Attack) {
            v.envelope_state = EnvelopeState::Decay;
        }
    }
    if (dspCounterPoll(rate)) v.envelope = next;
}
int16_t APU::voiceSample(int index, int16_t modulation_source) {
    Voice& v = voices_[index];
    const uint8_t base = static_cast<uint8_t>(index << 4);
    if (!v.active) {
        v.output = 0;
        v.effective_pitch = 0;
        v.pipeline_envx = 0;
        v.pipeline_outx = 0;
        return 0;
    }
    if (v.keyon_delay != 0) {
        --v.keyon_delay;
        v.brr_decode_allowed = (v.keyon_delay & 3) != 0;
        v.output = 0;
        v.effective_pitch = 0;
        v.pipeline_envx = 0;
        v.pipeline_outx = 0;
        return 0;
    }
    v.brr_decode_allowed = true;
    if (!v.brr_initialized) {
        v.output = 0;
        v.effective_pitch = 0;
        v.pipeline_envx = 0;
        v.pipeline_outx = 0;
        return 0;
    }
    uint32_t pitch = v.pitch_latch_valid
        ? static_cast<uint32_t>(v.pitch_latch)
        : static_cast<uint32_t>(dsp_registers_[base + 2] | ((dsp_registers_[base + 3] & 0x3F) << 8));
    if (index > 0 && (dsp_pmon_ & (1u << index)) &&
        (dsp_non_ & (1u << index)) == 0) {
        const int mod = (static_cast<int>(modulation_source) >> 5) * static_cast<int>(pitch) >> 10;
        pitch = static_cast<uint32_t>(std::clamp(static_cast<int>(pitch) + mod, 0, 0x3FFF));
    }
    v.effective_pitch = static_cast<uint16_t>(pitch);
    int sample = (dsp_non_ & (1u << index)) ? noise_sample_ : gaussianInterpolate(v);
    sample = (sample * v.envelope) >> 11;
    v.output = static_cast<int16_t>(clamp16(sample) & ~1);
    v.pipeline_envx = static_cast<uint8_t>((v.envelope >> 4) & 0x7F);
    v.pipeline_outx = static_cast<uint8_t>(static_cast<int8_t>(v.output >> 8));
    return v.output;
}
void APU::advanceVoicePitch(int index) {
    Voice& v = voices_[index];
    if (!v.active || v.keyon_delay != 0 || !v.brr_initialized) return;
    v.pitch_counter += v.effective_pitch;
    while (v.pitch_counter >= 0x1000 && v.active) {
        v.pitch_counter -= 0x1000;
        ++v.sample_index;
        if (v.sample_index >= 16) {
            v.sample_index = 0;
            if (v.end_flag) {
                if (!v.loop_flag) {
                    v.envelope_state = EnvelopeState::Release;
                    continue;
                }
            }
            std::copy_n(v.decoded.begin() + 16, 16, v.decoded.begin());
            std::fill(v.decoded.begin() + 16, v.decoded.end(), 0);
            v.end_flag = v.next_end_flag;
            v.loop_flag = v.next_loop_flag;
            v.brr_lookahead_ready = false;
        }
    }
}
void APU::processVoiceSamplePhase(uint8_t phase) {
    int v3c = -1;
    if (phase == 30) v3c = 0;
    else if (phase >= 1 && phase <= 19 && (phase % 3) == 1) v3c = static_cast<int>((phase + 2) / 3);
    if (v3c >= 0) {
        const int i = v3c;
        Voice& v = voices_[i];
        const int16_t modulation = i == 0 ? 0 : dsp_voice_output_latch_;
        dsp_voice_output_latch_ = voiceSample(i, modulation);
        const bool end_without_loop = v.brr_v3_valid && ((v.brr_header_latch & 0x03) == 0x01);
        if ((dsp_registers_[0x6C] & 0x80) || end_without_loop) {
            v.envelope_state = EnvelopeState::Release;
            v.envelope = 0;
        }
        if (dsp_every_other_sample_) {
            if (dsp_koff_ & (1u << i)) v.envelope_state = EnvelopeState::Release;
            if (dsp_kon_ & (1u << i)) keyOnVoice(i);
        }
        if (v.active && v.keyon_delay == 0 && v.brr_initialized) advanceEnvelope(i);
    }
    int v4 = -1;
    if (phase == 31) v4 = 0;
    else if (phase >= 2 && phase <= 20 && (phase % 3) == 2) v4 = static_cast<int>((phase + 1) / 3);
    if (v4 >= 0) {
        advanceVoicePitch(v4);
        addVoiceOutput(v4, 0);
    }
}
void APU::processVoiceFetchPhase(uint8_t phase) {
    int v1 = -1;
    if (phase == 17) v1 = 0;
    else if (phase == 20) v1 = 1;
    else if (phase == 31) v1 = 2;
    else if (phase >= 2 && phase <= 14 && (phase % 3) == 2) v1 = 3 + static_cast<int>((phase - 2) / 3);
    if (v1 >= 0) {
        Voice& v = voices_[v1];
        const uint8_t base = static_cast<uint8_t>(v1 << 4);
        dsp_dir_addr_ = static_cast<uint16_t>((static_cast<uint16_t>(dsp_dir_) << 8) |
            (static_cast<uint16_t>(dsp_srcn_) << 2));
        dsp_srcn_ = dsp_registers_[base + 4];
        v.fetch_srcn = dsp_srcn_;
        v.fetch_v1_valid = true;
    }
    int v2 = -1;
    if (phase == 21) v2 = 0;
    else if (phase <= 18 && (phase % 3) == 0) v2 = 1 + static_cast<int>(phase / 3);
    if (v2 >= 0) {
        Voice& v = voices_[v2];
        const uint8_t base = static_cast<uint8_t>(v2 << 4);
        v.pitch_low_latch = dsp_registers_[base + 2];
        v.adsr0_latch = dsp_registers_[base + 5];
        v.pitch_latch_valid = false;
        v.fetch_dir_addr = dsp_dir_addr_;
        const uint16_t entry = dsp_dir_addr_;
        const bool starting = v.active && v.keyon_delay != 0;
        const uint16_t pointer_entry = static_cast<uint16_t>(entry + (starting ? 0 : 2));
        const uint16_t pointer = static_cast<uint16_t>(ram_[pointer_entry] |
            (static_cast<uint16_t>(ram_[static_cast<uint16_t>(pointer_entry + 1)]) << 8));
        if (starting) {
            if (!v.kon_source_captured) {
                v.fetch_start_addr = pointer;
                v.fetch_v2_valid = true;
                v.kon_source_captured = true;
                v.brr_address = pointer;
                v.brr_offset = 1;
                v.brr_staging_mask = 0;
                v.brr_decode_destination = 0;
                v.brr_lookahead_ready = false;
            }
        } else {
            v.fetch_loop_addr = pointer;
            v.loop_address = pointer;
        }
    }
}
void APU::processVoicePitchPhase(uint8_t phase) {
    int i = -1;
    if (phase == 22) i = 0;
    else if (phase >= 1 && phase <= 19 && (phase % 3) == 1) i = static_cast<int>((phase + 2) / 3);
    if (i < 0) return;
    Voice& v = voices_[i];
    const uint8_t base = static_cast<uint8_t>(i << 4);
    v.pitch_latch = static_cast<uint16_t>(v.pitch_low_latch |
        (static_cast<uint16_t>(dsp_registers_[base + 3] & 0x3F) << 8));
    v.pitch_latch_valid = true;
}
void APU::processVoiceBRRPhase(uint8_t phase) {
    int i = -1;
    bool isV3 = false;
    if (phase == 25) { i = 0; isV3 = true; }
    else if (phase >= 1 && phase <= 19 && (phase % 3) == 1) { i = static_cast<int>((phase + 2) / 3); isV3 = true; }
    else if (phase == 31) { i = 0; }
    else if (phase >= 2 && phase <= 20 && (phase % 3) == 2) { i = static_cast<int>((phase + 1) / 3); }
    if (i < 0) return;
    Voice& v = voices_[i];
    if (!v.active || !v.fetch_v2_valid) return;
    if (v.brr_decode_destination == 16 && v.brr_lookahead_ready) return;
    if (isV3) {
        v.brr_header_latch = ram_[v.brr_address];
        v.brr_byte_latch = ram_[static_cast<uint16_t>(v.brr_address + v.brr_offset)];
        v.brr_v3_valid = true;
        return;
    }
    if (!v.brr_v3_valid) return;
    if (!v.brr_decode_allowed) {
        v.brr_v3_valid = false;
        return;
    }
    const uint8_t second = ram_[static_cast<uint16_t>(v.brr_address + v.brr_offset + 1)];
    v.brr_staging[0] = v.brr_header_latch;
    v.brr_staging[v.brr_offset] = v.brr_byte_latch;
    v.brr_staging[v.brr_offset + 1] = second;
    const uint8_t group = static_cast<uint8_t>((v.brr_offset - 1) >> 1);
    v.brr_staging_mask = static_cast<uint8_t>(v.brr_staging_mask | (1u << group));
    const size_t destination = v.brr_decode_destination + static_cast<size_t>(group) * 4;
    decodeBRRGroup(v, destination, v.brr_header_latch, v.brr_byte_latch, second);
    if (v.brr_decode_destination == 0) v.brr_initialized = true;
    v.brr_v3_valid = false;
    v.brr_offset = static_cast<uint8_t>(v.brr_offset + 2);
    if (v.brr_offset >= 9) {
        const bool end = (v.brr_header_latch & 0x01) != 0;
        const bool loop = (v.brr_header_latch & 0x02) != 0;
        if (end) v.pending_endx_set = true;
        if (v.brr_decode_destination == 0) {
            v.end_flag = end;
            v.loop_flag = loop;
            v.brr_decode_destination = 16;
        } else {
            v.next_end_flag = end;
            v.next_loop_flag = loop;
            v.brr_lookahead_ready = true;
        }
        const uint16_t completed = v.brr_address;
        v.brr_address = end ? v.loop_address : static_cast<uint16_t>(completed + 9);
        v.brr_offset = 1;
        v.brr_staging_mask = 0;
        v.brr_staging.fill(0);
    }
}
void APU::processVoiceWritebackPhase(uint8_t phase) {
    auto runStage = [this](int i, uint8_t rel) {
        Voice& v = voices_[i];
        const uint8_t base = static_cast<uint8_t>(i << 4);
        switch (rel) {
        case 0: {
            addVoiceOutput(i, 1);
            uint8_t endx = dsp_registers_[0x7C];
            if (v.pending_endx_set) endx = static_cast<uint8_t>(endx | (1u << i));
            if (v.pending_kon_clear) endx = static_cast<uint8_t>(endx & ~(1u << i));
            dsp_endx_buf_ = endx;
            v.pending_endx_set = false;
            v.pending_kon_clear = false;
            break;
        }
        case 1: dsp_outx_buf_ = v.pipeline_outx; break;
        case 2:
            dsp_registers_[0x7C] = dsp_endx_buf_;
            dsp_envx_buf_ = v.pipeline_envx;
            break;
        case 3: dsp_registers_[base + 9] = dsp_outx_buf_; break;
        case 4: dsp_registers_[base + 8] = dsp_envx_buf_; break;
        default: break;
        }
    };
    if (phase <= 25) {
        const int current = static_cast<int>(phase / 3);
        const uint8_t rem = static_cast<uint8_t>(phase % 3);
        if (rem <= 1 && current > 0 && current - 1 < 8)
            runStage(current - 1, static_cast<uint8_t>(3 + rem));
        if (current < 8 && rem <= 2)
            runStage(current, rem);
    }
}
void APU::addVoiceOutput(int index, int channel) {
    const Voice& v = voices_[index];
    const uint8_t base = static_cast<uint8_t>(index << 4);
    const int volume = static_cast<int8_t>(dsp_registers_[base + channel]);
    const int amp = (static_cast<int>(v.output) * volume) >> 7;
    dsp_main_out_[channel] = clamp16(dsp_main_out_[channel] + amp);
    if (dsp_eon_ & (1u << index)) {
        dsp_echo_out_[channel] = clamp16(dsp_echo_out_[channel] + amp);
    }
}
void APU::processEchoPhase(uint8_t phase) {
    auto readS16 = [&](uint16_t address) -> int16_t {
        return static_cast<int16_t>(static_cast<uint16_t>(ram_[address]) |
            (static_cast<uint16_t>(ram_[static_cast<uint16_t>(address + 1)]) << 8));
    };
    auto firTerm = [&](int tap, int channel) -> int {
        const int idx = (echo_history_pos_ + tap + 1) & 7;
        const int coeff = static_cast<int8_t>(dsp_registers_[static_cast<uint8_t>(0x0F + (tap << 4))]);
        return (static_cast<int>(echo_history_[channel][idx]) * coeff) >> 6;
    };
    auto echoOutput = [&](int channel) -> int {
        const int mvol = static_cast<int8_t>(dsp_registers_[channel ? 0x1C : 0x0C]);
        const int evol = static_cast<int8_t>(dsp_registers_[channel ? 0x3C : 0x2C]);
        const int main_term = static_cast<int16_t>((dsp_main_out_[channel] * mvol) >> 7);
        const int echo_term = static_cast<int16_t>((dsp_echo_in_[channel] * evol) >> 7);
        return clamp16(main_term + echo_term);
    };
    auto echoWrite = [&](int channel) {
        if ((dsp_echo_enabled_ & 0x20) == 0 && (test_ & 0x02) != 0) {
            const int16_t sample = static_cast<int16_t>(dsp_echo_out_[channel]);
            const uint16_t address = static_cast<uint16_t>(echo_pointer_ + channel * 2);
            const uint16_t next = static_cast<uint16_t>(address + 1);
            auto writeEchoByte = [&](uint16_t target, uint8_t data) {
                if (target == 0x00F8 || target == 0x00F9) return;
                ram_[target] = data;
            };
            writeEchoByte(address, static_cast<uint8_t>(sample));
            writeEchoByte(next, static_cast<uint8_t>(static_cast<uint16_t>(sample) >> 8));
        }
        dsp_echo_out_[channel] = 0;
    };
    switch (phase) {
    case 22: {
        echo_history_pos_ = static_cast<uint8_t>((echo_history_pos_ + 1) & 7);
        echo_pointer_ = static_cast<uint16_t>((static_cast<uint16_t>(dsp_esa_) << 8) + echo_offset_);
        echo_history_[0][echo_history_pos_] = static_cast<int16_t>(readS16(echo_pointer_) >> 1);
        dsp_echo_in_[0] = firTerm(0, 0);
        dsp_echo_in_[1] = firTerm(0, 1);
        break;
    }
    case 23:
        dsp_echo_in_[0] += firTerm(1, 0) + firTerm(2, 0);
        dsp_echo_in_[1] += firTerm(1, 1) + firTerm(2, 1);
        echo_history_[1][echo_history_pos_] = static_cast<int16_t>(
            readS16(static_cast<uint16_t>(echo_pointer_ + 2)) >> 1);
        break;
    case 24:
        dsp_echo_in_[0] += firTerm(3, 0) + firTerm(4, 0) + firTerm(5, 0);
        dsp_echo_in_[1] += firTerm(3, 1) + firTerm(4, 1) + firTerm(5, 1);
        break;
    case 25: {
        int left = dsp_echo_in_[0] + firTerm(6, 0);
        int right = dsp_echo_in_[1] + firTerm(6, 1);
        left = static_cast<int16_t>(left);
        right = static_cast<int16_t>(right);
        left += static_cast<int16_t>(firTerm(7, 0));
        right += static_cast<int16_t>(firTerm(7, 1));
        dsp_echo_in_[0] = clamp16(left) & ~1;
        dsp_echo_in_[1] = clamp16(right) & ~1;
        break;
    }
    case 26: {
        dsp_output_left_ = static_cast<int16_t>(echoOutput(0));
        const int feedback = static_cast<int8_t>(dsp_registers_[0x0D]);
        int left = dsp_echo_out_[0] + static_cast<int16_t>((dsp_echo_in_[0] * feedback) >> 7);
        int right = dsp_echo_out_[1] + static_cast<int16_t>((dsp_echo_in_[1] * feedback) >> 7);
        dsp_echo_out_[0] = clamp16(left) & ~1;
        dsp_echo_out_[1] = clamp16(right) & ~1;
        break;
    }
    case 27: {
        int left = dsp_output_left_;
        int right = echoOutput(1);
        dsp_main_out_[0] = 0;
        dsp_main_out_[1] = 0;
        if (dsp_registers_[0x6C] & 0x40) left = right = 0;
        audio_queue_.pushStereo(static_cast<int16_t>(clamp16(left)),
                                static_cast<int16_t>(clamp16(right)));
        break;
    }
    case 28:
        dsp_echo_enabled_ = dsp_registers_[0x6C];
        break;
    case 29: {
        dsp_esa_ = dsp_registers_[0x6D];
        if (echo_offset_ == 0) {
            echo_length_ = static_cast<uint16_t>((dsp_registers_[0x7D] & 0x0F) * 0x800);
        }
        echo_offset_ = static_cast<uint16_t>(echo_offset_ + 4);
        if (echo_length_ == 0 || echo_offset_ >= echo_length_) echo_offset_ = 0;
        echoWrite(0);
        dsp_echo_enabled_ = dsp_registers_[0x6C];
        break;
    }
    case 30:
        echoWrite(1);
        break;
    default:
        break;
    }
}
void APU::advanceDSP(uint8_t spc_cycles) {
    for (uint8_t clock = 0; clock < spc_cycles; ++clock) {
        processVoiceFetchPhase(dsp_phase_);
        processVoicePitchPhase(dsp_phase_);
        processVoiceBRRPhase(dsp_phase_);
        switch (dsp_phase_) {
        case 27:
            dsp_pmon_ = static_cast<uint8_t>(dsp_registers_[0x2D] & 0xFE);
            break;
        case 28:
            dsp_non_ = dsp_registers_[0x3D];
            dsp_eon_ = dsp_registers_[0x4D];
            dsp_dir_ = dsp_registers_[0x5D];
            break;
        case 29:
            dsp_every_other_sample_ = !dsp_every_other_sample_;
            if (dsp_every_other_sample_) {
                dsp_new_kon_ = static_cast<uint8_t>(dsp_new_kon_ & ~dsp_kon_);
            }
            break;
        case 30:
            if (dsp_every_other_sample_) {
                dsp_kon_ = dsp_new_kon_;
                dsp_koff_ = dsp_registers_[0x5C];
            }
            dsp_counter_ = dsp_counter_ == 0 ? 30719 : static_cast<uint16_t>(dsp_counter_ - 1);
            if (dspCounterPoll(dsp_registers_[0x6C] & 0x1F)) {
                const int feedback = (static_cast<int>(noise_lfsr_) << 13) ^
                                     (static_cast<int>(noise_lfsr_) << 14);
                noise_lfsr_ = static_cast<uint16_t>(((feedback & 0x4000) ^
                                                    (noise_lfsr_ >> 1)) & 0x7FFF);
            }
            noise_sample_ = static_cast<int16_t>(static_cast<uint16_t>(noise_lfsr_ << 1));
            break;
        default:
            break;
        }
        processVoiceSamplePhase(dsp_phase_);
        processVoiceWritebackPhase(dsp_phase_);
        processEchoPhase(dsp_phase_);
        dsp_phase_ = static_cast<uint8_t>((dsp_phase_ + 1) & 31);
    }
}
void APU::advanceTimers(uint8_t spc_cycles) {
    if ((test_ & 0x09) != 0x08) return;
    for (int i = 0; i < 3; ++i) {
        Timer& timer = timers_[i];
        const uint16_t divisor = i == 2 ? 16 : 128;
        timer.prescaler = static_cast<uint16_t>(timer.prescaler + spc_cycles);
        while (timer.prescaler >= divisor) {
            timer.prescaler = static_cast<uint16_t>(timer.prescaler - divisor);
            if (!timer.enabled) {
                continue;
            }
            timer.stage = static_cast<uint16_t>((timer.stage + 1) & 0xFF);
            if (static_cast<uint8_t>(timer.stage) == timer.target) {
                timer.stage = 0;
                timer.output = static_cast<uint8_t>((timer.output + 1) & 0x0F);
            }
        }
    }
}
uint8_t APU::fetch8() {
    return readInternal(pc_++);
}
uint16_t APU::fetch16() {
    const uint16_t lo = fetch8();
    const uint16_t hi = fetch8();
    return static_cast<uint16_t>(lo | (hi << 8));
}
uint16_t APU::dpAddress(uint8_t offset) const {
    return static_cast<uint16_t>((flag(P) ? 0x0100 : 0x0000) | offset);
}
uint16_t APU::read16Dp(uint8_t offset) {
    const uint8_t lo = readInternal(dpAddress(offset));
    const uint8_t hi = readInternal(dpAddress(static_cast<uint8_t>(offset + 1)));
    return static_cast<uint16_t>(lo | (static_cast<uint16_t>(hi) << 8));
}
void APU::push(uint8_t value) {
    writeInternal(static_cast<uint16_t>(0x0100 | sp_), value);
    --sp_;
}
uint8_t APU::pull() {
    ++sp_;
    return readInternal(static_cast<uint16_t>(0x0100 | sp_));
}
void APU::setFlag(PswFlag f, bool value) {
    if (value) psw_ |= f;
    else psw_ &= static_cast<uint8_t>(~f);
}
void APU::setNZ(uint8_t value) {
    setFlag(Z, value == 0);
    setFlag(N, (value & 0x80) != 0);
}
void APU::compare(uint8_t lhs, uint8_t rhs) {
    const uint8_t result = static_cast<uint8_t>(lhs - rhs);
    setFlag(C, lhs >= rhs);
    setNZ(result);
}
void APU::branch(bool take) {
    const int8_t displacement = static_cast<int8_t>(fetch8());
    if (take) {
        pc_ = static_cast<uint16_t>(pc_ + displacement);
    }
}
uint8_t APU::tick() {
    if (stopped_ || sleeping_) {
        advanceTimers(1);
        advanceDSP(1);
        ++cycles_;
        return 1;
    }
    const uint8_t test_speed_at_start = static_cast<uint8_t>((test_ >> 4) & 0x0F);
    const uint8_t wait_fraction_at_start = cpu_wait_fraction_;
    staged_port_write_count_ = 0;
    executing_instruction_ = true;
    const uint8_t opcode = fetch8();
    uint8_t used = 2;
    if (spc700HasOneByteDummyFetch(opcode)) {
        (void)readInternal(pc_);
    }
    auto dp = [&](uint8_t off) -> uint16_t { return dpAddress(off); };
    auto dpx = [&](uint8_t off) -> uint16_t { return dpAddress(static_cast<uint8_t>(off + x_)); };
    auto dpy = [&](uint8_t off) -> uint16_t { return dpAddress(static_cast<uint8_t>(off + y_)); };
    auto readDpPtrX = [&](uint8_t off) -> uint16_t {
        return read16Dp(static_cast<uint8_t>(off + x_));
    };
    auto readDpPtrY = [&](uint8_t off) -> uint16_t {
        const uint8_t lo = readInternal(dp(off));
        const uint8_t high_off = static_cast<uint8_t>(off + 1);
        const uint8_t hi = readInternal(dp(high_off));
        (void)readInternal(dp(high_off));
        return static_cast<uint16_t>(static_cast<uint16_t>(lo | (static_cast<uint16_t>(hi) << 8)) + y_);
    };
    auto read16 = [&](uint16_t address) -> uint16_t {
        const uint8_t lo = readInternal(address);
        const uint8_t hi = readInternal(static_cast<uint16_t>(address + 1));
        return static_cast<uint16_t>(lo | (static_cast<uint16_t>(hi) << 8));
    };
    auto setNZ16 = [&](uint16_t value) {
        setFlag(Z, value == 0);
        setFlag(N, (value & 0x8000) != 0);
    };
    auto doBranch = [&](bool condition) -> uint8_t {
        const int8_t rel = static_cast<int8_t>(fetch8());
        if (condition) {
            pc_ = static_cast<uint16_t>(pc_ + rel);
            return 4;
        }
        return 2;
    };
    auto adc8 = [&](uint8_t lhs, uint8_t rhs) -> uint8_t {
        const uint16_t carry = flag(C) ? 1 : 0;
        const uint16_t sum = static_cast<uint16_t>(static_cast<uint16_t>(lhs) + static_cast<uint16_t>(rhs) + carry);
        const uint8_t result = static_cast<uint8_t>(sum);
        setFlag(C, sum > 0xFF);
        setFlag(H, ((lhs & 0x0F) + (rhs & 0x0F) + carry) > 0x0F);
        setFlag(V, ((~(lhs ^ rhs) & (lhs ^ result)) & 0x80) != 0);
        setNZ(result);
        return result;
    };
    auto sbc8 = [&](uint8_t lhs, uint8_t rhs) -> uint8_t {
        const int borrow = flag(C) ? 0 : 1;
        const int diff = static_cast<int>(lhs) - static_cast<int>(rhs) - borrow;
        const uint8_t result = static_cast<uint8_t>(diff);
        setFlag(C, diff >= 0);
        setFlag(H, static_cast<int>(lhs & 0x0F) - static_cast<int>(rhs & 0x0F) - borrow >= 0);
        setFlag(V, (((lhs ^ rhs) & (lhs ^ result)) & 0x80) != 0);
        setNZ(result);
        return result;
    };
    enum class Alu { Or, And, Eor, Cmp, Adc, Sbc };
    auto alu = [&](Alu op, uint8_t lhs, uint8_t rhs) -> uint8_t {
        switch (op) {
        case Alu::Or: { const uint8_t r = static_cast<uint8_t>(lhs | rhs); setNZ(r); return r; }
        case Alu::And:{ const uint8_t r = static_cast<uint8_t>(lhs & rhs); setNZ(r); return r; }
        case Alu::Eor:{ const uint8_t r = static_cast<uint8_t>(lhs ^ rhs); setNZ(r); return r; }
        case Alu::Cmp: compare(lhs, rhs); return lhs;
        case Alu::Adc: return adc8(lhs, rhs);
        case Alu::Sbc: return sbc8(lhs, rhs);
        }
        return lhs;
    };
    auto rmw = [&](int op, uint8_t value) -> uint8_t {
        uint8_t result = value;
        switch (op) {
        case 0:
            setFlag(C, (value & 0x80) != 0); result = static_cast<uint8_t>(value << 1); break;
        case 1: {
            const bool old_c = flag(C); setFlag(C, (value & 0x80) != 0);
            result = static_cast<uint8_t>((value << 1) | (old_c ? 1 : 0)); break;
        }
        case 2:
            setFlag(C, (value & 0x01) != 0); result = static_cast<uint8_t>(value >> 1); break;
        case 3: {
            const bool old_c = flag(C); setFlag(C, (value & 0x01) != 0);
            result = static_cast<uint8_t>((value >> 1) | (old_c ? 0x80 : 0)); break;
        }
        case 4: result = static_cast<uint8_t>(value - 1); break;
        case 5: result = static_cast<uint8_t>(value + 1); break;
        }
        setNZ(result);
        return result;
    };
    if ((opcode & 0x0F) == 0x01) {
        const uint16_t vector = static_cast<uint16_t>(0xFFDE - ((opcode >> 4) * 2));
        push(static_cast<uint8_t>(pc_ >> 8));
        push(static_cast<uint8_t>(pc_));
        pc_ = read16(vector);
        used = 8;
    }
    else if ((opcode & 0x0F) == 0x02) {
        const uint8_t off = fetch8();
        const uint8_t bit = static_cast<uint8_t>((opcode >> 5) & 7);
        uint8_t value = readInternal(dp(off));
        if (opcode & 0x10) value = static_cast<uint8_t>(value & ~(1u << bit));
        else value = static_cast<uint8_t>(value | (1u << bit));
        writeInternal(dp(off), value);
        used = 4;
    }
    else if ((opcode & 0x0F) == 0x03) {
        const uint8_t off = fetch8();
        const uint8_t bit = static_cast<uint8_t>((opcode >> 5) & 7);
        const bool set = (readInternal(dp(off)) & (1u << bit)) != 0;
        const int8_t rel = static_cast<int8_t>(fetch8());
        const bool take = (opcode & 0x10) ? !set : set;
        if (take) pc_ = static_cast<uint16_t>(pc_ + rel);
        used = take ? 7 : 5;
    }
    else if ((opcode & 0xE0) <= 0xA0 && ((opcode & 0x0F) >= 0x04) && ((opcode & 0x0F) <= 0x09)) {
        const Alu op = static_cast<Alu>((opcode >> 5) & 7);
        const uint8_t low = opcode & 0x0F;
        const bool indexed = (opcode & 0x10) != 0;
        if (low == 0x04) {
            const uint8_t off = fetch8();
            const uint16_t addr = indexed ? dpx(off) : dp(off);
            a_ = alu(op, a_, readInternal(addr));
            used = indexed ? 4 : 3;
        } else if (low == 0x05) {
            const uint16_t base = fetch16();
            a_ = alu(op, a_, readInternal(static_cast<uint16_t>(base + (indexed ? x_ : 0))));
            used = indexed ? 5 : 4;
        } else if (low == 0x06) {
            if (!indexed) {
                a_ = alu(op, a_, readInternal(dp(x_)));
                used = 3;
            } else {
                const uint16_t base = fetch16();
                a_ = alu(op, a_, readInternal(static_cast<uint16_t>(base + y_)));
                used = 5;
            }
        } else if (low == 0x07) {
            const uint8_t off = fetch8();
            const uint16_t addr = indexed ? readDpPtrY(off) : readDpPtrX(off);
            a_ = alu(op, a_, readInternal(addr));
            used = 6;
        } else if (low == 0x08) {
            if (!indexed) {
                a_ = alu(op, a_, fetch8());
                used = 2;
            } else {
                const uint8_t imm = fetch8();
                const uint8_t off = fetch8();
                const uint16_t addr = dp(off);
                const uint8_t old = readInternal(addr);
                const uint8_t result = alu(op, old, imm);
                if (op != Alu::Cmp) writeInternal(addr, result);
                used = 5;
            }
        } else {
            if (!indexed) {
                const uint8_t src = fetch8();
                const uint8_t rhs = readInternal(dp(src));
                const uint8_t dst = fetch8();
                const uint16_t addr = dp(dst);
                const uint8_t old = readInternal(addr);
                const uint8_t result = alu(op, old, rhs);
                if (op != Alu::Cmp) writeInternal(addr, result);
                used = 6;
            } else {
                const uint8_t rhs = readInternal(dp(y_));
                const uint16_t ax = dp(x_);
                const uint8_t old = readInternal(ax);
                const uint8_t result = alu(op, old, rhs);
                if (op != Alu::Cmp) writeInternal(ax, result);
                used = 5;
            }
        }
    }
    else if ((opcode & 0xE0) <= 0xA0 && (((opcode & 0x0F) == 0x0B) || ((opcode & 0x0F) == 0x0C))) {
        const int op = (opcode >> 5) & 7;
        if (op <= 5) {
            const bool indexed = (opcode & 0x10) != 0;
            if ((opcode & 0x0F) == 0x0B) {
                const uint8_t off = fetch8();
                const uint16_t addr = indexed ? dpx(off) : dp(off);
                writeInternal(addr, rmw(op, readInternal(addr)));
                used = indexed ? 5 : 4;
            } else if (indexed) {
                a_ = rmw(op, a_);
                used = 2;
            } else {
                const uint16_t addr = fetch16();
                writeInternal(addr, rmw(op, readInternal(addr)));
                used = 5;
            }
        }
    }
    else switch (opcode) {
    case 0x00: used = 2; break;
    case 0x0A: case 0x2A: case 0x4A: case 0x6A:
    case 0x8A: case 0xAA: case 0xCA: case 0xEA: {
        const uint16_t spec = fetch16();
        const uint16_t addr = spec & 0x1FFF;
        const uint8_t bit = static_cast<uint8_t>((spec >> 13) & 7);
        const bool b = (readInternal(addr) & (1u << bit)) != 0;
        switch (opcode) {
        case 0x0A: setFlag(C, flag(C) | b); used = 5; break;
        case 0x2A: setFlag(C, flag(C) | !b); used = 5; break;
        case 0x4A: setFlag(C, flag(C) & b); used = 4; break;
        case 0x6A: setFlag(C, flag(C) & !b); used = 4; break;
        case 0x8A: setFlag(C, flag(C) ^ b); used = 5; break;
        case 0xAA: setFlag(C, b); used = 4; break;
        case 0xCA: {
            uint8_t v = readInternal(addr);
            if (flag(C)) v |= static_cast<uint8_t>(1u << bit);
            else v &= static_cast<uint8_t>(~(1u << bit));
            writeInternal(addr, v); used = 6; break;
        }
        case 0xEA: {
            writeInternal(addr, static_cast<uint8_t>(readInternal(addr) ^ (1u << bit)));
            used = 5; break;
        }
        }
        break;
    }
    case 0x0D: push(psw_); used = 4; break;
    case 0x2D: push(a_); used = 4; break;
    case 0x4D: push(x_); used = 4; break;
    case 0x6D: push(y_); used = 4; break;
    case 0x8E: psw_ = pull(); used = 4; break;
    case 0xAE: a_ = pull(); used = 4; break;
    case 0xCE: x_ = pull(); used = 4; break;
    case 0xEE: y_ = pull(); used = 4; break;
    case 0x0E: case 0x4E: {
        const uint16_t addr = fetch16();
        const uint8_t value = readInternal(addr);
        const uint8_t diff = static_cast<uint8_t>(a_ - value);
        setNZ(diff);
        (void)readInternal(addr);
        writeInternal(addr, opcode == 0x0E ? static_cast<uint8_t>(value | a_)
                                           : static_cast<uint8_t>(value & ~a_));
        used = 6; break;
    }
    case 0x0F: {
        push(static_cast<uint8_t>(pc_ >> 8));
        push(static_cast<uint8_t>(pc_));
        push(psw_);
        setFlag(B, true);
        setFlag(I, false);
        pc_ = read16(0xFFDE);
        used = 8; break;
    }
    case 0x10: used = doBranch(!flag(N)); break;
    case 0x30: used = doBranch(flag(N)); break;
    case 0x50: used = doBranch(!flag(V)); break;
    case 0x70: used = doBranch(flag(V)); break;
    case 0x90: used = doBranch(!flag(C)); break;
    case 0xB0: used = doBranch(flag(C)); break;
    case 0xD0: used = doBranch(!flag(Z)); break;
    case 0xF0: used = doBranch(flag(Z)); break;
    case 0x2F: { const int8_t rel = static_cast<int8_t>(fetch8()); pc_ = static_cast<uint16_t>(pc_ + rel); used = 4; break; }
    case 0x1A: case 0x3A: {
        const uint8_t off = fetch8();
        const uint16_t low_addr = dp(off);
        const uint16_t high_addr = dp(static_cast<uint8_t>(off + 1));
        const int adjust = opcode == 0x1A ? -1 : 1;
        uint16_t value = static_cast<uint16_t>(static_cast<int>(readInternal(low_addr)) + adjust);
        writeInternal(low_addr, static_cast<uint8_t>(value));
        value = static_cast<uint16_t>(value + (static_cast<uint16_t>(readInternal(high_addr)) << 8));
        writeInternal(high_addr, static_cast<uint8_t>(value >> 8));
        setNZ16(value); used = 6; break;
    }
    case 0x1D: --x_; setNZ(x_); used = 2; break;
    case 0x3D: ++x_; setNZ(x_); used = 2; break;
    case 0xDC: --y_; setNZ(y_); used = 2; break;
    case 0xFC: ++y_; setNZ(y_); used = 2; break;
    case 0x1E: compare(x_, readInternal(fetch16())); used = 4; break;
    case 0x3E: compare(x_, readInternal(dp(fetch8()))); used = 3; break;
    case 0x5E: compare(y_, readInternal(fetch16())); used = 4; break;
    case 0x7E: {
        const uint16_t address = dp(fetch8());
        if (scheduled_instruction_timing_valid_ && address >= 0x00F4 && address <= 0x00F7)
            schedulePortRead(static_cast<uint8_t>(address - 0x00F4), PendingPortReadKind::CompareY, 3, test_speed_at_start, wait_fraction_at_start);
        else compare(y_, readInternal(address));
        used = 3; break;
    }
    case 0x1F: { const uint16_t base = static_cast<uint16_t>(fetch16() + x_); pc_ = read16(base); used = 6; break; }
    case 0x5F: pc_ = fetch16(); used = 3; break;
    case 0x20: setFlag(P, false); used = 2; break;
    case 0x40: setFlag(P, true); used = 2; break;
    case 0x60: setFlag(C, false); used = 2; break;
    case 0x80: setFlag(C, true); used = 2; break;
    case 0xA0: setFlag(I, true); used = 3; break;
    case 0xC0: setFlag(I, false); used = 3; break;
    case 0xE0: setFlag(V, false); setFlag(H, false); used = 2; break;
    case 0xED: setFlag(C, !flag(C)); used = 3; break;
    case 0x2E: case 0xDE: {
        const uint8_t off = fetch8();
        const uint8_t value = readInternal(opcode == 0xDE ? dpx(off) : dp(off));
        const int8_t rel = static_cast<int8_t>(fetch8());
        const bool take = a_ != value;
        if (take) pc_ = static_cast<uint16_t>(pc_ + rel);
        used = static_cast<uint8_t>((opcode == 0xDE ? 6 : 5) + (take ? 2 : 0));
        break;
    }
    case 0x6E: {
        const uint8_t off = fetch8();
        const uint16_t addr = dp(off);
        const uint8_t value = static_cast<uint8_t>(readInternal(addr) - 1);
        writeInternal(addr, value);
        const int8_t rel = static_cast<int8_t>(fetch8());
        const bool take = value != 0;
        if (take) pc_ = static_cast<uint16_t>(pc_ + rel);
        used = take ? 7 : 5; break;
    }
    case 0xFE: {
        (void)readInternal(pc_);
        const int8_t rel = static_cast<int8_t>(fetch8());
        --y_;
        const bool take = y_ != 0;
        if (take) pc_ = static_cast<uint16_t>(pc_ + rel);
        used = take ? 6 : 4; break;
    }
    case 0x3F: {
        const uint16_t target = fetch16();
        push(static_cast<uint8_t>(pc_ >> 8)); push(static_cast<uint8_t>(pc_));
        pc_ = target; used = 8; break;
    }
    case 0x4F: {
        const uint8_t low = fetch8();
        push(static_cast<uint8_t>(pc_ >> 8)); push(static_cast<uint8_t>(pc_));
        pc_ = static_cast<uint16_t>(0xFF00 | low); used = 6; break;
    }
    case 0x6F: { const uint8_t lo = pull(), hi = pull(); pc_ = static_cast<uint16_t>(lo | (hi << 8)); used = 5; break; }
    case 0x7F: { psw_ = pull(); const uint8_t lo = pull(), hi = pull(); pc_ = static_cast<uint16_t>(lo | (hi << 8)); used = 6; break; }
    case 0x5A: {
        const uint16_t rhs = read16Dp(fetch8());
        const uint16_t lhs = static_cast<uint16_t>((y_ << 8) | a_);
        const uint16_t result = static_cast<uint16_t>(lhs - rhs);
        setFlag(C, lhs >= rhs); setNZ16(result); used = 4; break;
    }
    case 0x7A: {
        const uint16_t rhs = read16Dp(fetch8());
        const uint16_t lhs = static_cast<uint16_t>((y_ << 8) | a_);
        const uint32_t sum = static_cast<uint32_t>(lhs) + rhs;
        const uint16_t result = static_cast<uint16_t>(sum);
        setFlag(C, sum > 0xFFFF);
        setFlag(H, ((lhs & 0x0FFF) + (rhs & 0x0FFF)) > 0x0FFF);
        setFlag(V, ((~(lhs ^ rhs) & (lhs ^ result)) & 0x8000) != 0);
        a_ = static_cast<uint8_t>(result); y_ = static_cast<uint8_t>(result >> 8); setNZ16(result); used = 5; break;
    }
    case 0x9A: {
        const uint16_t rhs = read16Dp(fetch8());
        const uint16_t lhs = static_cast<uint16_t>((y_ << 8) | a_);
        const int32_t diff = static_cast<int32_t>(lhs) - rhs;
        const uint16_t result = static_cast<uint16_t>(diff);
        setFlag(C, diff >= 0);
        setFlag(H, (lhs & 0x0FFF) >= (rhs & 0x0FFF));
        setFlag(V, (((lhs ^ rhs) & (lhs ^ result)) & 0x8000) != 0);
        a_ = static_cast<uint8_t>(result); y_ = static_cast<uint8_t>(result >> 8); setNZ16(result); used = 5; break;
    }
    case 0x8D: y_ = fetch8(); setNZ(y_); used = 2; break;
    case 0xAD: compare(y_, fetch8()); used = 2; break;
    case 0xCD: x_ = fetch8(); setNZ(x_); used = 2; break;
    case 0xC8: compare(x_, fetch8()); used = 2; break;
    case 0xE8: a_ = fetch8(); setNZ(a_); used = 2; break;
    case 0x8F: { const uint8_t imm = fetch8(), off = fetch8(); (void)readInternal(dp(off)); writeInternal(dp(off), imm); used = 5; break; }
    case 0x9D: x_ = sp_; setNZ(x_); used = 2; break;
    case 0xBD: sp_ = x_; used = 2; break;
    case 0x5D: x_ = a_; setNZ(x_); used = 2; break;
    case 0x7D: a_ = x_; setNZ(a_); used = 2; break;
    case 0xDD: a_ = y_; setNZ(a_); used = 2; break;
    case 0xFD: y_ = a_; setNZ(y_); used = 2; break;
    case 0x9E: {
        setFlag(H, (x_ & 0x0F) <= (y_ & 0x0F));
        setFlag(V, y_ >= x_);
        uint16_t ya = static_cast<uint16_t>((y_ << 8) | a_);
        if (static_cast<uint16_t>(y_) < static_cast<uint16_t>(x_) * 2u) {
            if (x_ != 0) {
                a_ = static_cast<uint8_t>(ya / x_);
                y_ = static_cast<uint8_t>(ya % x_);
            } else {
                a_ = static_cast<uint8_t>(0xFF - y_);
                y_ = a_;
            }
        } else {
            const uint16_t denom = static_cast<uint16_t>(256 - x_);
            const uint16_t temp = static_cast<uint16_t>(ya - (static_cast<uint16_t>(x_) << 9));
            a_ = static_cast<uint8_t>(255 - (denom ? (temp / denom) : 0));
            y_ = static_cast<uint8_t>(x_ + (denom ? (temp % denom) : 0));
        }
        setNZ(a_); used = 12; break;
    }
    case 0xCF: { const uint16_t product = static_cast<uint16_t>(y_) * a_; a_ = static_cast<uint8_t>(product); y_ = static_cast<uint8_t>(product >> 8); setNZ(y_); used = 9; break; }
    case 0x9F: a_ = static_cast<uint8_t>((a_ >> 4) | (a_ << 4)); setNZ(a_); used = 5; break;
    case 0xDF: {
        if (a_ > 0x99 || flag(C)) { a_ = static_cast<uint8_t>(a_ + 0x60); setFlag(C, true); }
        if ((a_ & 0x0F) > 9 || flag(H)) a_ = static_cast<uint8_t>(a_ + 0x06);
        setNZ(a_); used = 3; break;
    }
    case 0xBE: {
        if (a_ > 0x99 || !flag(C)) { a_ = static_cast<uint8_t>(a_ - 0x60); setFlag(C, false); }
        if ((a_ & 0x0F) > 9 || !flag(H)) a_ = static_cast<uint8_t>(a_ - 0x06);
        setNZ(a_); used = 3; break;
    }
    case 0xE4: {
        const uint16_t address = dp(fetch8());
        if (scheduled_instruction_timing_valid_ && address >= 0x00F4 && address <= 0x00F7)
            schedulePortRead(static_cast<uint8_t>(address - 0x00F4), PendingPortReadKind::LoadA, 3, test_speed_at_start, wait_fraction_at_start);
        else { a_ = readInternal(address); setNZ(a_); }
        used = 3; break;
    }
    case 0xF4: a_ = readInternal(dpx(fetch8())); setNZ(a_); used = 4; break;
    case 0xE5: a_ = readInternal(fetch16()); setNZ(a_); used = 4; break;
    case 0xF5: { const uint16_t b = fetch16(); a_ = readInternal(static_cast<uint16_t>(b + x_)); setNZ(a_); used = 5; break; }
    case 0xF6: { const uint16_t b = fetch16(); a_ = readInternal(static_cast<uint16_t>(b + y_)); setNZ(a_); used = 5; break; }
    case 0xE6: a_ = readInternal(dp(x_)); setNZ(a_); used = 3; break;
    case 0xE7: a_ = readInternal(readDpPtrX(fetch8())); setNZ(a_); used = 6; break;
    case 0xF7: a_ = readInternal(readDpPtrY(fetch8())); setNZ(a_); used = 6; break;
    case 0xBF: a_ = readInternal(dp(x_)); ++x_; setNZ(a_); used = 4; break;
    case 0xF8: {
        const uint16_t address = dp(fetch8());
        if (scheduled_instruction_timing_valid_ && address >= 0x00F4 && address <= 0x00F7)
            schedulePortRead(static_cast<uint8_t>(address - 0x00F4), PendingPortReadKind::LoadX, 3, test_speed_at_start, wait_fraction_at_start);
        else { x_ = readInternal(address); setNZ(x_); }
        used = 3; break;
    }
    case 0xF9: x_ = readInternal(dpy(fetch8())); setNZ(x_); used = 4; break;
    case 0xE9: x_ = readInternal(fetch16()); setNZ(x_); used = 4; break;
    case 0xEB: {
        const uint16_t address = dp(fetch8());
        if (scheduled_instruction_timing_valid_ && address >= 0x00F4 && address <= 0x00F7)
            schedulePortRead(static_cast<uint8_t>(address - 0x00F4), PendingPortReadKind::LoadY, 3, test_speed_at_start, wait_fraction_at_start);
        else { y_ = readInternal(address); setNZ(y_); }
        used = 3; break;
    }
    case 0xFB: y_ = readInternal(dpx(fetch8())); setNZ(y_); used = 4; break;
    case 0xEC: y_ = readInternal(fetch16()); setNZ(y_); used = 4; break;
    case 0xC4: { const uint16_t a=dp(fetch8()); (void)readInternal(a); writeInternal(a,a_); used=4; break; }
    case 0xD4: { const uint16_t a=dpx(fetch8()); (void)readInternal(a); writeInternal(a,a_); used=5; break; }
    case 0xC5: { const uint16_t a=fetch16(); (void)readInternal(a); writeInternal(a,a_); used=5; break; }
    case 0xD5: { const uint16_t b=fetch16(); const uint16_t a=static_cast<uint16_t>(b+x_); (void)readInternal(a); writeInternal(a,a_); used=6; break; }
    case 0xD6: { const uint16_t b=fetch16(); const uint16_t a=static_cast<uint16_t>(b+y_); (void)readInternal(a); writeInternal(a,a_); used=6; break; }
    case 0xC6: { const uint16_t a=dp(x_); (void)readInternal(a); writeInternal(a,a_); used=4; break; }
    case 0xC7: { const uint16_t a=readDpPtrX(fetch8()); (void)readInternal(a); writeInternal(a,a_); used=7; break; }
    case 0xD7: { const uint16_t a=readDpPtrY(fetch8()); (void)readInternal(a); writeInternal(a,a_); used=7; break; }
    case 0xAF: {
        const uint16_t address = dp(x_);
        (void)readBackingRamForDummyCycle(address);
        writeInternal(address, a_);
        ++x_;
        used = 4;
        break;
    }
    case 0xD8: { const uint16_t a=dp(fetch8()); (void)readInternal(a); writeInternal(a,x_); used=4; break; }
    case 0xD9: { const uint16_t a=dpy(fetch8()); (void)readInternal(a); writeInternal(a,x_); used=5; break; }
    case 0xC9: { const uint16_t a=fetch16(); (void)readInternal(a); writeInternal(a,x_); used=5; break; }
    case 0xCB: { const uint16_t a=dp(fetch8()); (void)readInternal(a); writeInternal(a,y_); used=4; break; }
    case 0xDB: { const uint16_t a=dpx(fetch8()); (void)readInternal(a); writeInternal(a,y_); used=5; break; }
    case 0xCC: { const uint16_t a=fetch16(); (void)readInternal(a); writeInternal(a,y_); used=5; break; }
    case 0xFA: {
        const uint8_t src = fetch8();
        const uint8_t value = readInternal(dp(src));
        const uint8_t dst = fetch8();
        writeInternal(dp(dst), value);
        used = 5;
        break;
    }
    case 0xBA: {
        const uint8_t off = fetch8();
        const uint16_t lowAddress = dp(off);
        const uint16_t highAddress = dp(static_cast<uint8_t>(off + 1));
        if (scheduled_instruction_timing_valid_ && lowAddress >= 0x00F4 && lowAddress <= 0x00F7 && highAddress >= 0x00F4 && highAddress <= 0x00F7) {
            schedulePortRead(static_cast<uint8_t>(lowAddress - 0x00F4), PendingPortReadKind::MovwLow, 3, test_speed_at_start, wait_fraction_at_start);
            schedulePortRead(static_cast<uint8_t>(highAddress - 0x00F4), PendingPortReadKind::MovwHigh, 5, test_speed_at_start, wait_fraction_at_start);
        } else {
            const uint8_t lo = readInternal(lowAddress);
            (void)readInternal(lowAddress);
            const uint8_t hi = readInternal(highAddress);
            const uint16_t v = static_cast<uint16_t>(lo | (static_cast<uint16_t>(hi) << 8));
            a_=static_cast<uint8_t>(v); y_=static_cast<uint8_t>(v>>8); setNZ16(v);
        }
        used=5; break;
    }
    case 0xDA: { const uint8_t off=fetch8(); (void)readInternal(dp(off)); writeInternal(dp(off),a_); writeInternal(dp(static_cast<uint8_t>(off+1)),y_); used=5; break; }
    case 0xEF: sleeping_ = true; used = 3; break;
    case 0xFF: stopped_ = true; used = 2; break;
    default:
        used = 2;
        break;
    }
    executing_instruction_ = false;
    scheduleStagedPortWrites(used, test_speed_at_start, wait_fraction_at_start);
    uint8_t elapsed = used;
    if (!flat_test_memory_) {
        const uint16_t scaled = static_cast<uint16_t>(used) * TEST_CPU_RATE_DENOMINATOR[test_speed_at_start] + cpu_wait_fraction_;
        elapsed = static_cast<uint8_t>(scaled / 3u);
        cpu_wait_fraction_ = static_cast<uint8_t>(scaled % 3u);
        advanceTimers(elapsed);
        advanceDSP(elapsed);
    }
    cycles_ += elapsed;
    return elapsed;
}

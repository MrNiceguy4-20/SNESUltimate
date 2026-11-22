#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <memory>
#include <utility>
#include <vector>
class Bus;
class APU {
public:
    APU();
    ~APU();
    void attachBus(Bus* bus) { bus_ = bus; }
    void power();
    void reset();
    uint8_t tick();
    uint8_t readPort(uint8_t port);
    void writePort(uint8_t port, uint8_t value);
    void beginScheduledInstruction(uint64_t startTimeFp, uint64_t masterClocksPerApuCycleFp);
    void synchronizePorts(uint64_t masterClock);
    uint64_t nextPortEventMasterClock() const;
    using PortWriteObserver = std::function<void(uint8_t, uint8_t, bool, uint64_t)>;
    void setPortWriteObserver(PortWriteObserver observer) { port_write_observer_ = std::move(observer); }
    uint8_t read(uint16_t address);
    void write(uint16_t address, uint8_t value);
    uint16_t pc() const { return pc_; }
    uint8_t a() const { return a_; }
    uint8_t x() const { return x_; }
    uint8_t y() const { return y_; }
    uint8_t sp() const { return sp_; }
    uint8_t psw() const { return psw_; }
    uint64_t cycles() const { return cycles_; }
    bool sleeping() const { return sleeping_; }
    bool stopped() const { return stopped_; }
    bool iplEnabled() const { return (control_ & 0x80) != 0; }
    struct State {
        uint16_t pc = 0;
        uint8_t a = 0;
        uint8_t x = 0;
        uint8_t y = 0;
        uint8_t sp = 0;
        uint8_t psw = 0;
    };
    void setStateForTesting(const State& state);
    void enableFlatTestMemory(bool enabled) { flat_test_memory_ = enabled; }
    void clearFlatTestMemory(uint8_t value = 0) { ram_.fill(value); }
    void flatTestPoke(uint16_t address, uint8_t value) { ram_[address] = value; }
    uint8_t flatTestPeek(uint16_t address) const { return ram_[address]; }
    static constexpr int AUDIO_SAMPLE_RATE = 32000;
    void takeAudioSamples(std::vector<int16_t>& out);
    std::vector<int16_t> takeAudioSamples();
    friend class SNES;
private:
    enum PswFlag : uint8_t {
        C = 0x01, Z = 0x02, I = 0x04, H = 0x08,
        B = 0x10, P = 0x20, V = 0x40, N = 0x80
    };
    struct Timer {
        uint8_t target = 0;
        uint16_t stage = 0;
        uint8_t output = 0;
        uint16_t prescaler = 0;
        bool enabled = false;
    };
    enum class EnvelopeState : uint8_t { Attack, Decay, Sustain, Release };
    struct Voice {
        uint16_t brr_address = 0;
        uint16_t loop_address = 0;
        std::array<int16_t, 32> decoded{};
        uint8_t sample_index = 0;
        uint32_t pitch_counter = 0;
        uint16_t pitch_latch = 0;
        uint16_t effective_pitch = 0;
        uint8_t pitch_low_latch = 0;
        uint8_t adsr0_latch = 0;
        bool pitch_latch_valid = false;
        int16_t previous1 = 0;
        int16_t previous2 = 0;
        int envelope = 0;
        int hidden_envelope = 0;
        int16_t output = 0;
        uint8_t pipeline_envx = 0;
        uint8_t pipeline_outx = 0;
        bool pending_endx_set = false;
        bool pending_kon_clear = false;
        bool active = false;
        uint8_t keyon_delay = 0;
        bool end_flag = false;
        bool loop_flag = false;
        bool next_end_flag = false;
        bool next_loop_flag = false;
        uint8_t fetch_srcn = 0;
        uint16_t fetch_dir_addr = 0;
        uint16_t fetch_start_addr = 0;
        uint16_t fetch_loop_addr = 0;
        bool fetch_v1_valid = false;
        bool fetch_v2_valid = false;
        bool brr_initialized = false;
        uint8_t brr_offset = 1;
        uint8_t brr_header_latch = 0;
        uint8_t brr_byte_latch = 0;
        bool brr_v3_valid = false;
        bool brr_decode_allowed = true;
        std::array<uint8_t, 9> brr_staging{};
        uint8_t brr_staging_mask = 0;
        size_t brr_decode_destination = 0;
        bool brr_lookahead_ready = false;
        bool kon_source_captured = false;
        EnvelopeState envelope_state = EnvelopeState::Release;
    };
    uint8_t readInternal(uint16_t address);
    uint8_t readBackingRamForDummyCycle(uint16_t address);
    void writeInternal(uint16_t address, uint8_t value);
    void writeControl(uint8_t value);
    void advanceTimers(uint8_t spc_cycles);
    uint8_t readDSP(uint8_t address) const;
    void writeDSP(uint8_t address, uint8_t value);
    void advanceDSP(uint8_t spc_cycles);
    void processEchoPhase(uint8_t phase);
    void addVoiceOutput(int index, int channel);
    void processVoiceWritebackPhase(uint8_t phase);
    void processVoiceFetchPhase(uint8_t phase);
    void processVoiceBRRPhase(uint8_t phase);
    void processVoicePitchPhase(uint8_t phase);
    void processVoiceSamplePhase(uint8_t phase);
    void advanceVoicePitch(int index);
    void keyOnVoice(int index);
    void decodeBRRGroup(Voice& voice, size_t destination, uint8_t header, uint8_t first, uint8_t second);
    int16_t gaussianInterpolate(const Voice& voice) const;
    void advanceEnvelope(int index);
    bool dspCounterPoll(uint8_t rate) const;
    int16_t voiceSample(int index, int16_t modulation_source);
    int16_t clamp16(int value) const;
    uint8_t fetch8();
    uint16_t fetch16();
    uint16_t dpAddress(uint8_t offset) const;
    uint16_t read16Dp(uint8_t offset);
    void push(uint8_t value);
    uint8_t pull();
    void setFlag(PswFlag flag, bool value);
    bool flag(PswFlag flag) const { return (psw_ & flag) != 0; }
    void setNZ(uint8_t value);
    void compare(uint8_t lhs, uint8_t rhs);
    void branch(bool take);
    PortWriteObserver port_write_observer_;
    struct PendingPortWrite {
        uint64_t due_master_clock = 0;
        uint64_t spc_cycle = 0;
        uint8_t port = 0;
        uint8_t value = 0;
    };
    struct StagedPortWrite {
        uint8_t port = 0;
        uint8_t value = 0;
    };
    enum class PendingPortReadKind : uint8_t { None, LoadA, LoadX, LoadY, CompareY, MovwLow, MovwHigh };
    struct PendingPortRead {
        uint64_t due_master_clock = 0;
        uint64_t spc_cycle = 0;
        uint8_t port = 0;
        PendingPortReadKind kind = PendingPortReadKind::None;
    };
    void commitPortWrite(uint8_t port, uint8_t value, uint64_t spcCycle);
    void commitPortRead(const PendingPortRead& event);
    void schedulePortRead(uint8_t port, PendingPortReadKind kind, uint8_t logicalCycle, uint8_t testSpeedAtStart, uint8_t waitFractionAtStart);
    void scheduleStagedPortWrites(uint8_t used, uint8_t testSpeedAtStart, uint8_t waitFractionAtStart);
    Bus* bus_ = nullptr;
    std::array<uint8_t, 65536> ram_{};
    std::array<uint8_t, 4> cpu_to_apu_{};
    std::array<uint8_t, 4> apu_to_cpu_{};
    std::array<uint8_t, 128> dsp_registers_{};
    std::array<Timer, 3> timers_{};
    std::array<Voice, 8> voices_{};
    static constexpr size_t AUDIO_QUEUE_CAPACITY_SAMPLES = static_cast<size_t>(AUDIO_SAMPLE_RATE) * 4;
    class AudioRingBuffer {
    public:
        void clear() { head_ = 0; size_ = 0; }
        bool empty() const { return size_ == 0; }
        size_t size() const { return size_; }
        int16_t operator[](size_t index) const {
            size_t physical = head_ + index;
            if (physical >= AUDIO_QUEUE_CAPACITY_SAMPLES) physical -= AUDIO_QUEUE_CAPACITY_SAMPLES;
            return samples_[physical];
        }
        void pushStereo(int16_t left, int16_t right) {
            if (size_ + 2 > AUDIO_QUEUE_CAPACITY_SAMPLES) {
                head_ += 2;
                if (head_ >= AUDIO_QUEUE_CAPACITY_SAMPLES) head_ -= AUDIO_QUEUE_CAPACITY_SAMPLES;
                size_ -= 2;
            }
            size_t tail = head_ + size_;
            if (tail >= AUDIO_QUEUE_CAPACITY_SAMPLES) tail -= AUDIO_QUEUE_CAPACITY_SAMPLES;
            samples_[tail] = left;
            ++tail;
            if (tail == AUDIO_QUEUE_CAPACITY_SAMPLES) tail = 0;
            samples_[tail] = right;
            size_ += 2;
        }
        void copyTo(std::vector<int16_t>& out) const {
            out.resize(size_);
            const size_t first = std::min(size_, AUDIO_QUEUE_CAPACITY_SAMPLES - head_);
            std::copy_n(samples_.get() + head_, first, out.data());
            if (first < size_) {
                std::copy_n(samples_.get(), size_ - first, out.data() + first);
            }
        }
        void drainTo(std::vector<int16_t>& out) {
            copyTo(out);
            clear();
        }
        bool assign(const std::vector<int16_t>& in) {
            if (in.size() > AUDIO_QUEUE_CAPACITY_SAMPLES || (in.size() & 1u) != 0) return false;
            clear();
            for (size_t i = 0; i < in.size(); ++i) samples_[i] = in[i];
            size_ = in.size();
            return true;
        }
    private:
        std::unique_ptr<int16_t[]> samples_ = std::make_unique<int16_t[]>(AUDIO_QUEUE_CAPACITY_SAMPLES);
        size_t head_ = 0;
        size_t size_ = 0;
    };
    AudioRingBuffer audio_queue_;
    uint8_t dsp_phase_ = 0;
    uint8_t dsp_new_kon_ = 0;
    uint8_t dsp_kon_ = 0;
    uint8_t dsp_koff_ = 0;
    bool dsp_every_other_sample_ = false;
    uint8_t dsp_pmon_ = 0;
    uint8_t dsp_non_ = 0;
    uint8_t dsp_eon_ = 0;
    uint8_t dsp_dir_ = 0;
    uint8_t dsp_srcn_ = 0;
    uint16_t dsp_dir_addr_ = 0;
    uint8_t dsp_esa_ = 0;
    uint8_t dsp_echo_enabled_ = 0;
    uint8_t dsp_endx_buf_ = 0;
    uint8_t dsp_envx_buf_ = 0;
    uint8_t dsp_outx_buf_ = 0;
    uint16_t noise_lfsr_ = 0x4000;
    uint16_t dsp_counter_ = 0;
    int16_t noise_sample_ = 0;
    int16_t dsp_voice_output_latch_ = 0;
    std::array<std::array<int16_t, 8>, 2> echo_history_{};
    uint8_t echo_history_pos_ = 0;
    uint16_t echo_offset_ = 0;
    uint16_t echo_length_ = 0;
    uint16_t echo_pointer_ = 0;
    std::array<int, 2> dsp_main_out_{};
    std::array<int, 2> dsp_echo_out_{};
    std::array<int, 2> dsp_echo_in_{};
    int16_t dsp_output_left_ = 0;
    uint16_t pc_ = 0xFFC0;
    uint8_t a_ = 0;
    uint8_t x_ = 0;
    uint8_t y_ = 0;
    uint8_t sp_ = 0;
    uint8_t psw_ = 0;
    uint8_t control_ = 0xB0;
    uint8_t test_ = 0x0A;
    uint8_t dsp_address_ = 0;
    uint8_t ram_f8_ = 0;
    uint8_t ram_f9_ = 0;
    uint64_t cycles_ = 0;
    uint8_t cpu_wait_fraction_ = 0;
    std::array<PendingPortWrite, 8> pending_port_writes_{};
    uint8_t pending_port_write_count_ = 0;
    std::array<PendingPortRead, 8> pending_port_reads_{};
    uint8_t pending_port_read_count_ = 0;
    uint8_t pending_movw_low_ = 0;
    std::array<StagedPortWrite, 4> staged_port_writes_{};
    uint8_t staged_port_write_count_ = 0;
    uint64_t scheduled_instruction_start_fp_ = 0;
    uint64_t scheduled_master_clocks_per_apu_cycle_fp_ = 0;
    bool scheduled_instruction_timing_valid_ = false;
    bool executing_instruction_ = false;
    bool sleeping_ = false;
    bool stopped_ = false;
    bool flat_test_memory_ = false;
};

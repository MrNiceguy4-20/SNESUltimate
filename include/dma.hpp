#pragma once
#include <array>
#include <cstdint>
#include <string>
class Bus;
class DMA {
public:
    DMA();
    ~DMA();
    void attachBus(Bus* bus) { bus_ = bus; }
    void reset();
    void tick();
    uint8_t readRegister(uint16_t address) const;
    void writeRegister(uint16_t address, uint8_t value);
    void startDMA(uint8_t channelMask, uint64_t masterClock = 0, uint8_t resumeCpuSpeed = 8);
    void setResumeCpuSpeed(uint8_t masterClocks) {
        dma_resume_cpu_speed_ = (masterClocks == 6 || masterClocks == 8 || masterClocks == 12) ? masterClocks : 8;
    }
    bool dmaActive() const { return dma_active_; }
    uint64_t dmaNextDelay() const { return dma_next_delay_; }
    uint64_t dmaResumeDelayAfterHDMA(uint64_t pausedDelay) const {
        return dma_phase_ == DMAPhase::InterruptedSelect ? 0 : pausedDelay;
    }
    uint64_t stepDMA();
    void setHDMAEnable(uint8_t channelMask);
    uint8_t hdmaEnable() const { return hdma_enable_; }
    void startHDMAInit(uint64_t masterClock = 0, uint8_t resumeCpuSpeed = 8);
    void startHDMALine(uint64_t masterClock = 0, uint8_t resumeCpuSpeed = 8);
    void startHDMALineCaptured(uint8_t capturedMask, uint64_t masterClock = 0, uint8_t resumeCpuSpeed = 8);
    void deactivateHDMAForVBlank();
    bool hdmaActive() const { return hdma_active_; }
    uint64_t hdmaNextDelay() const { return hdma_next_delay_; }
    uint64_t stepHDMA();
    void beginHDMAFrame();
    void runHDMALine();
    uint64_t takePendingMasterClocks();
    uint64_t lastDMAMasterClocks() const { return last_dma_master_clocks_; }
    uint64_t lastHDMAMasterClocks() const { return last_hdma_master_clocks_; }
    static constexpr uint64_t HDMA_GLOBAL_OVERHEAD = 8;
    static constexpr uint64_t HDMA_CHANNEL_OVERHEAD = 8;
    static constexpr uint64_t HDMA_NESTED_GLOBAL_OVERHEAD = 8;
    static constexpr uint64_t DMA_GLOBAL_OVERHEAD = 8;
    static constexpr uint64_t DMA_CHANNEL_OVERHEAD = 8;
    static constexpr uint64_t DMA_BYTE_CLOCKS = 8;
    friend class SNES;
private:
    enum class DMAPhase : uint8_t {
        Idle,
        Sync,
        GlobalOverhead,
        ChannelOverhead,
        Transfer,
        EndSync,
        InterruptedSelect
    };
    enum class HDMAPhase : uint8_t {
        Idle,
        InitSync,
        InitGlobal,
        InitChannel,
        InitIndirectLow,
        InitIndirectHigh,
        LineSync,
        LineGlobal,
        LineChannel,
        LineIndirectLow,
        LineIndirectHigh,
        LineTransfer,
        EndSync,
        LineDescriptor
    };
    struct HDMAState {
        bool active = false;
        bool do_transfer = false;
        bool repeat = false;
        uint8_t lines_remaining = 0;
    };
    uint8_t& reg(int channel, int offset) { return registers_[channel * 0x10 + offset]; }
    uint8_t reg(int channel, int offset) const { return registers_[channel * 0x10 + offset]; }
    uint16_t reg16(int channel, int offset) const;
    void setReg16(int channel, int offset, uint16_t value);
    static int transferLength(uint8_t mode);
    static uint8_t bbusOffset(uint8_t mode, int index);
    void transferByte(int channel, uint32_t aAddress, uint16_t bAddress, bool hdma = false);
    uint32_t dmaAAddress(int channel) const;
    void advanceDMAAAddress(int channel);
    bool selectNextDMAChannel();
    bool channelCanWriteVram(int channel) const;
    void finishDMAChannel();
    bool abortCurrentDMAChannelForHDMA();
    bool selectNextHDMAChannel();
    bool selectNextHDMATransferChannel();
    uint8_t activeHDMAMask() const { return hdma_init_operation_ ? hdma_enable_ : hdma_operation_mask_; }
    bool loadHDMADescriptor(int channel);
    void finishHDMALineChannel(int channel);
    uint32_t hdmaSourceAddress(int channel);
    void advanceHDMASource(int channel);
    void finishHDMAOperation();
    Bus* bus_ = nullptr;
    std::array<uint8_t, 0x80> registers_{};
    std::array<HDMAState, 8> hdma_state_{};
    uint8_t hdma_enable_ = 0;
    bool dma_active_ = false;
    uint8_t dma_channel_mask_ = 0;
    int dma_channel_ = -1;
    uint32_t dma_remaining_ = 0;
    int dma_pattern_index_ = 0;
    DMAPhase dma_phase_ = DMAPhase::Idle;
    uint64_t dma_next_delay_ = 0;
    uint64_t dma_accumulated_clocks_ = 0;
    uint8_t dma_resume_cpu_speed_ = 8;
    bool hdma_active_ = false;
    bool hdma_init_operation_ = false;
    int hdma_channel_ = -1;
    int hdma_pattern_index_ = 0;
    uint8_t hdma_indirect_low_ = 0;
    HDMAPhase hdma_phase_ = HDMAPhase::Idle;
    uint64_t hdma_next_delay_ = 0;
    uint64_t hdma_accumulated_clocks_ = 0;
    uint8_t hdma_operation_mask_ = 0;
    uint8_t hdma_resume_cpu_speed_ = 8;
    uint64_t pending_master_clocks_ = 0;
    uint64_t last_dma_master_clocks_ = 0;
    uint64_t last_hdma_master_clocks_ = 0;
};

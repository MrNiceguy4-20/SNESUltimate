#include "dma.hpp"
#include "bus.hpp"
#include <iomanip>
#include <sstream>
namespace {
constexpr uint64_t DMA_GLOBAL_OVERHEAD = DMA::DMA_GLOBAL_OVERHEAD;
constexpr uint64_t DMA_CHANNEL_OVERHEAD = DMA::DMA_CHANNEL_OVERHEAD;
constexpr uint64_t DMA_BYTE_CLOCKS = DMA::DMA_BYTE_CLOCKS;
constexpr uint64_t HDMA_GLOBAL_OVERHEAD = DMA::HDMA_GLOBAL_OVERHEAD;
constexpr uint64_t HDMA_CHANNEL_OVERHEAD = DMA::HDMA_CHANNEL_OVERHEAD;
}
DMA::DMA() { reset(); }
DMA::~DMA() = default;
void DMA::reset() {
    registers_.fill(0x00);
    hdma_state_.fill({});
    hdma_enable_ = 0;
    dma_active_ = false;
    dma_channel_mask_ = 0;
    dma_channel_ = -1;
    dma_remaining_ = 0;
    dma_pattern_index_ = 0;
    dma_phase_ = DMAPhase::Idle;
    dma_next_delay_ = 0;
    dma_accumulated_clocks_ = 0;
    dma_resume_cpu_speed_ = 8;
    hdma_active_ = false;
    hdma_init_operation_ = false;
    hdma_channel_ = -1;
    hdma_pattern_index_ = 0;
    hdma_indirect_low_ = 0;
    hdma_phase_ = HDMAPhase::Idle;
    hdma_next_delay_ = 0;
    hdma_accumulated_clocks_ = 0;
    hdma_operation_mask_ = 0;
    hdma_resume_cpu_speed_ = 8;
    pending_master_clocks_ = 0;
    last_dma_master_clocks_ = 0;
    last_hdma_master_clocks_ = 0;
}
void DMA::tick() {}
uint16_t DMA::reg16(int channel, int offset) const {
    return static_cast<uint16_t>(reg(channel, offset) |
        (static_cast<uint16_t>(reg(channel, offset + 1)) << 8));
}
void DMA::setReg16(int channel, int offset, uint16_t value) {
    reg(channel, offset) = static_cast<uint8_t>(value & 0xFF);
    reg(channel, offset + 1) = static_cast<uint8_t>(value >> 8);
}
uint8_t DMA::readRegister(uint16_t address) const {
    if (address < 0x4300 || address > 0x437F) return 0xFF;
    return registers_[address - 0x4300];
}
void DMA::writeRegister(uint16_t address, uint8_t value) {
    if (address >= 0x4300 && address <= 0x437F) {
        registers_[address - 0x4300] = value;
    }
}
int DMA::transferLength(uint8_t mode) {
    static constexpr int lengths[8] = {1, 2, 2, 4, 4, 4, 2, 4};
    return lengths[mode & 7];
}
uint8_t DMA::bbusOffset(uint8_t mode, int index) {
    static constexpr uint8_t pattern[8][4] = {
        {0, 0, 0, 0}, {0, 1, 0, 1}, {0, 0, 0, 0}, {0, 0, 1, 1},
        {0, 1, 2, 3}, {0, 1, 0, 1}, {0, 0, 0, 0}, {0, 0, 1, 1},
    };
    return pattern[mode & 7][index & 3];
}
void DMA::transferByte(int channel, uint32_t aAddress, uint16_t bAddress, bool hdma) {
    if (bus_ == nullptr) return;
    const bool bToA = (reg(channel, 0) & 0x80) != 0;
    const uint8_t bank = static_cast<uint8_t>((aAddress >> 16) & 0xFF);
    const uint16_t addr = static_cast<uint16_t>(aAddress & 0xFFFF);
    const bool aBusIsWram = bank == 0x7E || bank == 0x7F ||
        ((bank <= 0x3F || (bank >= 0x80 && bank <= 0xBF)) && addr < 0x2000);
    if (bAddress == 0x2180 && aBusIsWram) {
        if (bToA) bus_->write(aAddress, 0x00);
        return;
    }
    if (bToA) {
        bus_->writeDMA(aAddress, bus_->read(bAddress));
    } else {
        const uint8_t value = bus_->readDMA(channel, aAddress);
        bus_->write(bAddress, value);
    }
}
uint32_t DMA::dmaAAddress(int channel) const {
    return (static_cast<uint32_t>(reg(channel, 4)) << 16) | reg16(channel, 2);
}
void DMA::advanceDMAAAddress(int channel) {
    const uint8_t control = reg(channel, 0);
    if ((control & 0x08) != 0) return;
    uint16_t address = reg16(channel, 2);
    if ((control & 0x10) != 0) --address;
    else ++address;
    setReg16(channel, 2, address);
}
void DMA::startDMA(uint8_t channelMask, uint64_t masterClock, uint8_t resumeCpuSpeed) {
    if (bus_ == nullptr || channelMask == 0 || dma_active_) return;
    dma_active_ = true;
    dma_channel_mask_ = channelMask;
    dma_channel_ = -1;
    dma_remaining_ = 0;
    dma_pattern_index_ = 0;
    dma_phase_ = DMAPhase::Sync;
    dma_accumulated_clocks_ = 0;
    setResumeCpuSpeed(resumeCpuSpeed);
    last_dma_master_clocks_ = 0;
    dma_next_delay_ = 8u - (masterClock & 7u);
}
bool DMA::channelCanWriteVram(int channel) const {
    if (channel < 0 || channel >= 8 || (reg(channel, 0) & 0x80) != 0) return false;
    const uint8_t mode = reg(channel, 0) & 7;
    const uint16_t bbase = static_cast<uint16_t>(0x2100 | reg(channel, 1));
    const int count = transferLength(mode);
    for (int i = 0; i < count; ++i) {
        const uint16_t b = static_cast<uint16_t>(bbase + bbusOffset(mode, i));
        if (b == 0x2118 || b == 0x2119) return true;
    }
    return false;
}
bool DMA::selectNextDMAChannel() {
    for (int channel = dma_channel_ + 1; channel < 8; ++channel) {
        if ((dma_channel_mask_ & (1u << channel)) == 0) continue;
        dma_channel_ = channel;
        dma_remaining_ = reg16(channel, 5);
        if (dma_remaining_ == 0) dma_remaining_ = 0x10000;
        dma_pattern_index_ = 0;
        if (bus_ != nullptr) bus_->beginCartridgeDMA(channel, dmaAAddress(channel), dma_remaining_, reg(channel, 0));
        return true;
    }
    return false;
}
void DMA::finishDMAChannel() {
    if (dma_channel_ >= 0) {
        if (bus_ != nullptr) bus_->endCartridgeDMA(dma_channel_);
        setReg16(dma_channel_, 5, 0);
    }
}
bool DMA::abortCurrentDMAChannelForHDMA() {
    if (!dma_active_ || dma_channel_ < 0 || dma_channel_ >= 8) return false;
    const uint8_t bit = static_cast<uint8_t>(1u << dma_channel_);
    if ((hdma_enable_ & bit) == 0 || !hdma_state_[dma_channel_].active) return false;
    if (bus_ != nullptr) bus_->endCartridgeDMA(dma_channel_);
    setReg16(dma_channel_, 5, static_cast<uint16_t>(dma_remaining_ & 0xFFFF));
    dma_channel_mask_ = static_cast<uint8_t>(dma_channel_mask_ & ~bit);
    dma_pattern_index_ = 0;
    dma_phase_ = DMAPhase::InterruptedSelect;
    dma_next_delay_ = 0;
    return true;
}
uint64_t DMA::stepDMA() {
    if (!dma_active_) return 0;
    dma_accumulated_clocks_ += dma_next_delay_;
    switch (dma_phase_) {
    case DMAPhase::Sync:
        dma_phase_ = DMAPhase::GlobalOverhead;
        return dma_next_delay_ = DMA_GLOBAL_OVERHEAD;
    case DMAPhase::GlobalOverhead:
        if (!selectNextDMAChannel()) {
            dma_phase_ = DMAPhase::EndSync;
            const uint64_t mod = dma_accumulated_clocks_ % dma_resume_cpu_speed_;
            return dma_next_delay_ = static_cast<uint64_t>(dma_resume_cpu_speed_) - mod;
        }
        dma_phase_ = DMAPhase::ChannelOverhead;
        return dma_next_delay_ = DMA_CHANNEL_OVERHEAD;
    case DMAPhase::ChannelOverhead:
        dma_phase_ = DMAPhase::Transfer;
        return dma_next_delay_ = DMA_BYTE_CLOCKS;
    case DMAPhase::InterruptedSelect:
        if (selectNextDMAChannel()) {
            dma_phase_ = DMAPhase::ChannelOverhead;
            return dma_next_delay_ = DMA_CHANNEL_OVERHEAD;
        }
        dma_phase_ = DMAPhase::EndSync;
        {
            const uint64_t mod = dma_accumulated_clocks_ % dma_resume_cpu_speed_;
            return dma_next_delay_ = static_cast<uint64_t>(dma_resume_cpu_speed_) - mod;
        }
    case DMAPhase::Transfer: {
        const uint8_t mode = reg(dma_channel_, 0) & 7;
        const uint16_t bbase = static_cast<uint16_t>(0x2100 | reg(dma_channel_, 1));
        transferByte(dma_channel_, dmaAAddress(dma_channel_),
            static_cast<uint16_t>(bbase + bbusOffset(mode, dma_pattern_index_)));
        advanceDMAAAddress(dma_channel_);
        ++dma_pattern_index_;
        if (--dma_remaining_ != 0) {
            setReg16(dma_channel_, 5, static_cast<uint16_t>(dma_remaining_ & 0xFFFF));
            return dma_next_delay_ = DMA_BYTE_CLOCKS;
        }
        finishDMAChannel();
        if (selectNextDMAChannel()) {
            dma_phase_ = DMAPhase::ChannelOverhead;
            return dma_next_delay_ = DMA_CHANNEL_OVERHEAD;
        }
        dma_phase_ = DMAPhase::EndSync;
        {
            const uint64_t mod = dma_accumulated_clocks_ % dma_resume_cpu_speed_;
            return dma_next_delay_ = static_cast<uint64_t>(dma_resume_cpu_speed_) - mod;
        }
    }
    case DMAPhase::EndSync:
        dma_active_ = false;
        dma_phase_ = DMAPhase::Idle;
        dma_next_delay_ = 0;
        last_dma_master_clocks_ = dma_accumulated_clocks_;
        return 0;
    case DMAPhase::Idle:
    default:
        dma_active_ = false; dma_next_delay_ = 0; return 0;
    }
}
void DMA::setHDMAEnable(uint8_t channelMask) {
    hdma_enable_ = channelMask;
}
bool DMA::selectNextHDMAChannel() {
    for (int channel = hdma_channel_ + 1; channel < 8; ++channel) {
        if ((activeHDMAMask() & (1u << channel)) == 0) continue;
        if (!hdma_init_operation_ && !hdma_state_[channel].active) continue;
        hdma_channel_ = channel;
        hdma_pattern_index_ = 0;
        return true;
    }
    return false;
}
bool DMA::selectNextHDMATransferChannel() {
    for (int channel = hdma_channel_ + 1; channel < 8; ++channel) {
        if ((activeHDMAMask() & (1u << channel)) == 0) continue;
        const auto& state = hdma_state_[channel];
        if (!state.active || !state.do_transfer) continue;
        hdma_channel_ = channel;
        hdma_pattern_index_ = 0;
        return true;
    }
    return false;
}
bool DMA::loadHDMADescriptor(int channel) {
    if (bus_ == nullptr) return false;
    uint16_t table = reg16(channel, 8);
    const uint32_t base = static_cast<uint32_t>(reg(channel, 4)) << 16;
    const uint8_t descriptor = bus_->readDMA(channel, base | table);
    setReg16(channel, 8, static_cast<uint16_t>(table + 1));
    reg(channel, 0xA) = descriptor;
    auto& state = hdma_state_[channel];
    if (descriptor == 0) {
        state = {};
        return false;
    }
    state.active = true;
    state.repeat = descriptor > 0x80;
    state.lines_remaining = static_cast<uint8_t>(descriptor & 0x7F);
    if (state.lines_remaining == 0) state.lines_remaining = 128;
    state.do_transfer = true;
    return true;
}
uint32_t DMA::hdmaSourceAddress(int channel) {
    if ((reg(channel, 0) & 0x40) != 0) {
        return (static_cast<uint32_t>(reg(channel, 7)) << 16) | reg16(channel, 5);
    }
    return (static_cast<uint32_t>(reg(channel, 4)) << 16) | reg16(channel, 8);
}
void DMA::advanceHDMASource(int channel) {
    if ((reg(channel, 0) & 0x40) != 0) {
        setReg16(channel, 5, static_cast<uint16_t>(reg16(channel, 5) + 1));
    } else {
        setReg16(channel, 8, static_cast<uint16_t>(reg16(channel, 8) + 1));
    }
}
void DMA::finishHDMALineChannel(int channel) {
    auto& state = hdma_state_[channel];
    if (state.lines_remaining > 0) --state.lines_remaining;
    reg(channel, 0xA) = static_cast<uint8_t>((state.repeat ? 0x80 : 0x00) |
        (state.lines_remaining & 0x7F));
    if (state.lines_remaining != 0) state.do_transfer = state.repeat;
}
void DMA::finishHDMAOperation() {
    if (!dma_active_ && hdma_phase_ != HDMAPhase::EndSync) {
        hdma_phase_ = HDMAPhase::EndSync;
        const uint64_t mod = hdma_accumulated_clocks_ % hdma_resume_cpu_speed_;
        hdma_next_delay_ = static_cast<uint64_t>(hdma_resume_cpu_speed_) - mod;
        return;
    }
    hdma_active_ = false;
    hdma_phase_ = HDMAPhase::Idle;
    hdma_next_delay_ = 0;
    last_hdma_master_clocks_ = hdma_accumulated_clocks_;
    if (dma_active_) dma_accumulated_clocks_ += hdma_accumulated_clocks_;
}
void DMA::startHDMAInit(uint64_t masterClock, uint8_t resumeCpuSpeed) {
    if (hdma_active_) return;
    for (auto& state : hdma_state_) state = {true, false, false, 0};
    if (hdma_enable_ == 0) return;
    hdma_active_ = true;
    hdma_init_operation_ = true;
    hdma_channel_ = -1;
    hdma_pattern_index_ = 0;
    hdma_accumulated_clocks_ = 0;
    hdma_resume_cpu_speed_ = (resumeCpuSpeed == 6 || resumeCpuSpeed == 8 || resumeCpuSpeed == 12) ? resumeCpuSpeed : 8;
    if (dma_active_) {
        hdma_phase_ = HDMAPhase::InitGlobal;
        hdma_next_delay_ = HDMA_NESTED_GLOBAL_OVERHEAD;
    } else {
        hdma_phase_ = HDMAPhase::InitSync;
        hdma_next_delay_ = 8u - (masterClock & 7u);
    }
    hdma_operation_mask_ = hdma_enable_;
}
void DMA::startHDMALine(uint64_t masterClock, uint8_t resumeCpuSpeed) {
    startHDMALineCaptured(hdma_enable_, masterClock, resumeCpuSpeed);
}
void DMA::startHDMALineCaptured(uint8_t capturedMask, uint64_t masterClock, uint8_t resumeCpuSpeed) {
    if (hdma_active_) return;
    hdma_operation_mask_ = capturedMask;
    bool any = false;
    for (int channel = 0; channel < 8; ++channel) {
        if ((hdma_operation_mask_ & (1u << channel)) != 0 && hdma_state_[channel].active) {
            any = true; break;
        }
    }
    if (!any) return;
    const bool nested_in_mdma = dma_active_;
    if (nested_in_mdma) abortCurrentDMAChannelForHDMA();
    hdma_active_ = true;
    hdma_init_operation_ = false;
    hdma_channel_ = -1;
    hdma_pattern_index_ = 0;
    hdma_accumulated_clocks_ = 0;
    hdma_resume_cpu_speed_ = (resumeCpuSpeed == 6 || resumeCpuSpeed == 8 || resumeCpuSpeed == 12) ? resumeCpuSpeed : 8;
    if (nested_in_mdma) {
        hdma_phase_ = HDMAPhase::LineGlobal;
        hdma_next_delay_ = HDMA_NESTED_GLOBAL_OVERHEAD;
    } else {
        hdma_phase_ = HDMAPhase::LineSync;
        hdma_next_delay_ = 8u - (masterClock & 7u);
    }
}
void DMA::deactivateHDMAForVBlank() {
    for (auto& state : hdma_state_) state.active = false;
}
uint64_t DMA::stepHDMA() {
    if (!hdma_active_) return 0;
    hdma_accumulated_clocks_ += hdma_next_delay_;
    switch (hdma_phase_) {
    case HDMAPhase::InitSync:
        hdma_phase_ = HDMAPhase::InitGlobal;
        return hdma_next_delay_ = HDMA_CHANNEL_OVERHEAD;
    case HDMAPhase::InitGlobal:
        if (!selectNextHDMAChannel()) { finishHDMAOperation(); return hdma_active_ ? hdma_next_delay_ : 0; }
        setReg16(hdma_channel_, 8, reg16(hdma_channel_, 2));
        hdma_state_[hdma_channel_].active = true;
        hdma_phase_ = HDMAPhase::InitChannel;
        return hdma_next_delay_ = HDMA_CHANNEL_OVERHEAD;
    case HDMAPhase::InitChannel:
        if (!loadHDMADescriptor(hdma_channel_)) {
            if (!selectNextHDMAChannel()) { finishHDMAOperation(); return hdma_active_ ? hdma_next_delay_ : 0; }
            setReg16(hdma_channel_, 8, reg16(hdma_channel_, 2));
            hdma_state_[hdma_channel_].active = true;
            return hdma_next_delay_ = HDMA_CHANNEL_OVERHEAD;
        }
        if ((reg(hdma_channel_, 0) & 0x40) != 0) {
            hdma_phase_ = HDMAPhase::InitIndirectLow;
            return hdma_next_delay_ = DMA_BYTE_CLOCKS;
        }
        if (!selectNextHDMAChannel()) { finishHDMAOperation(); return hdma_active_ ? hdma_next_delay_ : 0; }
        setReg16(hdma_channel_, 8, reg16(hdma_channel_, 2));
        hdma_state_[hdma_channel_].active = true;
        hdma_phase_ = HDMAPhase::InitChannel;
        return hdma_next_delay_ = HDMA_CHANNEL_OVERHEAD;
    case HDMAPhase::InitIndirectLow: {
        const uint16_t table = reg16(hdma_channel_, 8);
        const uint32_t base = static_cast<uint32_t>(reg(hdma_channel_, 4)) << 16;
        hdma_indirect_low_ = bus_->readDMA(hdma_channel_, base | table);
        setReg16(hdma_channel_, 8, static_cast<uint16_t>(table + 1));
        hdma_phase_ = HDMAPhase::InitIndirectHigh;
        return hdma_next_delay_ = DMA_BYTE_CLOCKS;
    }
    case HDMAPhase::InitIndirectHigh: {
        const uint16_t table = reg16(hdma_channel_, 8);
        const uint32_t base = static_cast<uint32_t>(reg(hdma_channel_, 4)) << 16;
        const uint8_t hi = bus_->readDMA(hdma_channel_, base | table);
        setReg16(hdma_channel_, 8, static_cast<uint16_t>(table + 1));
        setReg16(hdma_channel_, 5, static_cast<uint16_t>(hdma_indirect_low_ | (static_cast<uint16_t>(hi) << 8)));
        if (!selectNextHDMAChannel()) { finishHDMAOperation(); return hdma_active_ ? hdma_next_delay_ : 0; }
        setReg16(hdma_channel_, 8, reg16(hdma_channel_, 2));
        hdma_state_[hdma_channel_].active = true;
        hdma_phase_ = HDMAPhase::InitChannel;
        return hdma_next_delay_ = HDMA_CHANNEL_OVERHEAD;
    }
    case HDMAPhase::LineSync:
        hdma_phase_ = HDMAPhase::LineGlobal;
        return hdma_next_delay_ = HDMA_CHANNEL_OVERHEAD;
    case HDMAPhase::LineGlobal:
        hdma_channel_ = -1;
        if (selectNextHDMATransferChannel()) {
            hdma_phase_ = HDMAPhase::LineTransfer;
            return hdma_next_delay_ = DMA_BYTE_CLOCKS;
        }
        hdma_channel_ = -1;
        if (!selectNextHDMAChannel()) { finishHDMAOperation(); return hdma_active_ ? hdma_next_delay_ : 0; }
        hdma_phase_ = HDMAPhase::LineChannel;
        return hdma_next_delay_ = 0;
    case HDMAPhase::LineTransfer: {
        const uint8_t mode = reg(hdma_channel_, 0) & 7;
        const uint16_t bbase = static_cast<uint16_t>(0x2100 | reg(hdma_channel_, 1));
        transferByte(hdma_channel_, hdmaSourceAddress(hdma_channel_),
            static_cast<uint16_t>(bbase + bbusOffset(mode, hdma_pattern_index_)), true);
        advanceHDMASource(hdma_channel_);
        ++hdma_pattern_index_;
        if (hdma_pattern_index_ < transferLength(mode)) {
            return hdma_next_delay_ = DMA_BYTE_CLOCKS;
        }
        if (selectNextHDMATransferChannel()) {
            hdma_phase_ = HDMAPhase::LineTransfer;
            return hdma_next_delay_ = DMA_BYTE_CLOCKS;
        }
        hdma_channel_ = -1;
        if (!selectNextHDMAChannel()) { finishHDMAOperation(); return hdma_active_ ? hdma_next_delay_ : 0; }
        hdma_phase_ = HDMAPhase::LineChannel;
        return hdma_next_delay_ = 0;
    }
    case HDMAPhase::LineChannel: {
        auto& state = hdma_state_[hdma_channel_];
        if (!state.active) {
            if (!selectNextHDMAChannel()) { finishHDMAOperation(); return hdma_active_ ? hdma_next_delay_ : 0; }
            return hdma_next_delay_ = 0;
        }
        uint8_t counter = static_cast<uint8_t>(reg(hdma_channel_, 0xA) - 1u);
        reg(hdma_channel_, 0xA) = counter;
        state.repeat = (counter & 0x80) != 0;
        state.lines_remaining = static_cast<uint8_t>(counter & 0x7F);
        state.do_transfer = state.repeat;
        if ((counter & 0x7F) == 0) {
            hdma_phase_ = HDMAPhase::LineDescriptor;
            return hdma_next_delay_ = DMA_BYTE_CLOCKS;
        }
        if (!selectNextHDMAChannel()) { finishHDMAOperation(); return hdma_active_ ? hdma_next_delay_ : 0; }
        hdma_phase_ = HDMAPhase::LineChannel;
        return hdma_next_delay_ = 0;
    }
    case HDMAPhase::LineDescriptor: {
        auto& state = hdma_state_[hdma_channel_];
        const uint16_t table = reg16(hdma_channel_, 8);
        const uint32_t base = static_cast<uint32_t>(reg(hdma_channel_, 4)) << 16;
        const uint8_t next_descriptor = bus_ != nullptr
            ? bus_->readDMA(hdma_channel_, base | table)
            : 0x00;
        setReg16(hdma_channel_, 8, static_cast<uint16_t>(table + 1));
        reg(hdma_channel_, 0xA) = next_descriptor;
        if (next_descriptor == 0) {
            state = {};
        } else {
            state.active = true;
            state.repeat = next_descriptor > 0x80;
            state.lines_remaining = static_cast<uint8_t>(next_descriptor & 0x7F);
            if (state.lines_remaining == 0) state.lines_remaining = 128;
            state.do_transfer = true;
            if ((reg(hdma_channel_, 0) & 0x40) != 0) {
                hdma_phase_ = HDMAPhase::LineIndirectLow;
                return hdma_next_delay_ = DMA_BYTE_CLOCKS;
            }
        }
        if (!selectNextHDMAChannel()) { finishHDMAOperation(); return hdma_active_ ? hdma_next_delay_ : 0; }
        hdma_phase_ = HDMAPhase::LineChannel;
        return hdma_next_delay_ = 0;
    }
    case HDMAPhase::LineIndirectLow: {
        const uint16_t table = reg16(hdma_channel_, 8);
        const uint32_t base = static_cast<uint32_t>(reg(hdma_channel_, 4)) << 16;
        hdma_indirect_low_ = bus_->readDMA(hdma_channel_, base | table);
        setReg16(hdma_channel_, 8, static_cast<uint16_t>(table + 1));
        hdma_phase_ = HDMAPhase::LineIndirectHigh;
        return hdma_next_delay_ = DMA_BYTE_CLOCKS;
    }
    case HDMAPhase::LineIndirectHigh: {
        const uint16_t table = reg16(hdma_channel_, 8);
        const uint32_t base = static_cast<uint32_t>(reg(hdma_channel_, 4)) << 16;
        const uint8_t hi = bus_->readDMA(hdma_channel_, base | table);
        setReg16(hdma_channel_, 8, static_cast<uint16_t>(table + 1));
        setReg16(hdma_channel_, 5, static_cast<uint16_t>(hdma_indirect_low_ | (static_cast<uint16_t>(hi) << 8)));
        if (!selectNextHDMAChannel()) { finishHDMAOperation(); return hdma_active_ ? hdma_next_delay_ : 0; }
        hdma_phase_ = HDMAPhase::LineChannel;
        return hdma_next_delay_ = 0;
    }
    case HDMAPhase::EndSync:
        finishHDMAOperation();
        return 0;
    case HDMAPhase::Idle:
    default:
        finishHDMAOperation();
        return hdma_active_ ? hdma_next_delay_ : 0;
    }
}
void DMA::beginHDMAFrame() {
    startHDMAInit();
    while (hdmaActive()) {
        const uint64_t delay = hdmaNextDelay();
        pending_master_clocks_ += delay;
        stepHDMA();
    }
}
void DMA::runHDMALine() {
    startHDMALine();
    while (hdmaActive()) {
        const uint64_t delay = hdmaNextDelay();
        pending_master_clocks_ += delay;
        stepHDMA();
    }
}
uint64_t DMA::takePendingMasterClocks() {
    const uint64_t clocks = pending_master_clocks_;
    pending_master_clocks_ = 0;
    return clocks;
}

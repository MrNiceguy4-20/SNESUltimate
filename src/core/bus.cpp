#include "bus.hpp"
#include "apu.hpp"
#include "cartridge.hpp"
#include "controller.hpp"
#include "cpu.hpp"
#include "dma.hpp"
#include "ppu.hpp"
#include <algorithm>
Bus::Bus() : wram_(128 * 1024, 0) { power(); }
void Bus::enableFlatTestMemory(bool enabled) {
    flat_test_memory_enabled_ = enabled;
    if (enabled && flat_test_memory_.size() != 0x1000000u) {
        flat_test_memory_.assign(0x1000000u, 0);
    }
}
void Bus::clearFlatTestMemory(uint8_t value) {
    if (flat_test_memory_.size() != 0x1000000u) {
        flat_test_memory_.assign(0x1000000u, value);
    } else {
        std::fill(flat_test_memory_.begin(), flat_test_memory_.end(), value);
    }
}
uint8_t Bus::flatTestPeek(uint32_t address) const {
    if (flat_test_memory_.empty()) return 0;
    return flat_test_memory_[address & 0xFFFFFFu];
}
void Bus::flatTestPoke(uint32_t address, uint8_t value) {
    if (flat_test_memory_.size() != 0x1000000u) {
        flat_test_memory_.assign(0x1000000u, 0);
    }
    flat_test_memory_[address & 0xFFFFFFu] = value;
}
void Bus::power() {
    std::fill(wram_.begin(), wram_.end(), 0);
    reset();
}
void Bus::reset() {
    wram_address_ = 0;
    nmitimen_ = 0;
    wrio_ = 0xFF;
    if (controller1_ != nullptr) controller1_->setIoPort(wrio_);
    if (controller2_ != nullptr) controller2_->setIoPort(wrio_);
    mdmaen_ = 0;
    hdmaen_ = 0;
    memsel_ = 0;
    wrmpya_ = 0xFF;
    wrdiv_ = 0xFFFF;
    quotient_ = 0;
    product_remainder_ = 0;
    math_mpy_counter_ = 0;
    math_div_counter_ = 0;
    math_shift_ = 0;
    master_clock_ = 0;
    open_bus_ = 0;
    open_bus_bit_refresh_clock_.fill(master_clock_);
    cpu_io_read_bus_ = 0;
    joy_strobe_ = false;
    auto_joy_strobe_ = false;
    auto_joy_.fill(0);
    auto_joy_active_ = false;
    auto_joy_elapsed_clocks_ = 0;
    auto_joy_next_sample_clock_ = 256;
    auto_joy_latch_released_ = false;
    auto_joy_pending_ = false;
    htime_ = 0x01FF;
    vtime_ = 0x01FF;
    rdnmi_flag_ = false;
    nmi_line_level_ = false;
    nmi_last_hclock_ = 0;
    timeup_flag_ = false;
    timer_irq_active_until_ = 0;
    timer_irq_line_due_ = UINT64_MAX;
    timer_irq_line_asserted_ = false;
    timer_irq_edge_committed_ = false;
    timer_hcounter_ = 0;
    timer_vcounter_ = 0;
    timer_irq_level_ = false;
    timer_irq_need_ = 0;
    timer_irq_enabled_ = false;
    timer_last_hclock_ = 0;
    hblank_ = false;
    vblank_ = false;
    previous_vblank_ = false;
    irq_match_latched_ = false;
    cartridge_irq_ = false;
    irq_config_generation_ = 0;
    last_irq_config_write_ = 0;
    if (cpu_ != nullptr) {
        cpu_->setIRQLine(false);
    }
}
bool Bus::isSystemBank(uint8_t bank) const {
    return (bank & 0x40u) == 0;
}
uint8_t Bus::accessMasterClocks(uint32_t address) const {
    if (flat_test_memory_enabled_) return 6;
    address &= 0xFFFFFF;
    const uint8_t bank = static_cast<uint8_t>(address >> 16);
    const uint16_t addr = static_cast<uint16_t>(address);
    if (addr >= 0x8000) {
        return (bank >= 0x80 && fastRomEnabled()) ? 6 : 8;
    }
    if (bank == 0x7E || bank == 0x7F) {
        return 8;
    }
    if (isSystemBank(bank)) {
        if (addr < 0x2000) {
            return 8;
        }
        if (addr >= 0x2000 && addr <= 0x3FFF) {
            return 6;
        }
        if (addr >= 0x4000 && addr <= 0x41FF) {
            return 12;
        }
        if (addr >= 0x4200 && addr <= 0x5FFF) {
            return 6;
        }
        if (cartridge_ != nullptr && cartridge_->isLoaded() &&
            cartridge_->isHiROMFamily() &&
            bank >= 0xA0 && bank <= 0xBF && addr >= 0x6000 && addr <= 0x7FFF) {
            return 8;
        }
        if (addr >= 0x8000 && bank >= 0x80 && fastRomEnabled()) {
            return 6;
        }
        return 8;
    }
    if (cartridge_ != nullptr && cartridge_->isLoaded() &&
        cartridge_->isLoROMFamily() &&
        bank >= 0xF0 && addr < 0x8000) {
        return 8;
    }
    if (bank >= 0x80 && fastRomEnabled()) {
        return 6;
    }
    return 8;
}
void Bus::configureOpenBusDecay(bool enabled, uint64_t thresholdMasterClocks, uint8_t targetValue) {
    open_bus_decay_enabled_ = enabled;
    open_bus_decay_threshold_ = thresholdMasterClocks;
    open_bus_decay_target_ = targetValue;
    open_bus_bit_refresh_clock_.fill(master_clock_);
}
void Bus::driveOpenBus(uint8_t value, uint8_t drivenMask) {
    if (drivenMask == 0) return;
    open_bus_ = static_cast<uint8_t>((open_bus_ & ~drivenMask) | (value & drivenMask));
    for (unsigned bit = 0; bit < 8; ++bit) {
        if ((drivenMask & (1u << bit)) != 0) open_bus_bit_refresh_clock_[bit] = master_clock_;
    }
}
void Bus::applyOpenBusDecay() {
    if (!open_bus_decay_enabled_ || open_bus_decay_threshold_ == 0) return;
    for (unsigned bit = 0; bit < 8; ++bit) {
        if (master_clock_ - open_bus_bit_refresh_clock_[bit] < open_bus_decay_threshold_) continue;
        const uint8_t mask = static_cast<uint8_t>(1u << bit);
        if ((open_bus_decay_target_ & mask) != 0) open_bus_ |= mask;
        else open_bus_ &= static_cast<uint8_t>(~mask);
    }
}
uint8_t Bus::openBus() {
    applyOpenBusDecay();
    return open_bus_;
}
uint8_t Bus::read(uint32_t address) {
    applyOpenBusDecay();
    address &= 0xFFFFFF;
    if (flat_test_memory_enabled_) {
        const uint8_t value = flatTestPeek(address);
        driveOpenBus(value);
        cpu_io_read_bus_ = value;
        return value;
    }
    const uint8_t bank = static_cast<uint8_t>(address >> 16);
    const uint16_t addr = static_cast<uint16_t>(address);
    uint8_t value = 0xFF;
    if (isSystemBank(bank)) {
        bool handled = false;
        value = readSystemArea(bank, addr, handled);
        if (handled) {
            driveOpenBus(value);
            cpu_io_read_bus_ = value;
            return value;
        }
    }
    if (cartridge_ != nullptr && cartridge_->isLoaded() && cartridge_->readHardware(address, open_bus_, value)) {
        driveOpenBus(value);
        cpu_io_read_bus_ = value;
        return value;
    }
    if (bank == 0x7E || bank == 0x7F) {
        value = wram_[((static_cast<uint32_t>(bank) - 0x7E) << 16) | addr];
        driveOpenBus(value);
        cpu_io_read_bus_ = value;
        return value;
    }
    if (cartridge_ != nullptr && cartridge_->isLoaded()) {
        switch (cartridge_->mapperType()) {
        case MapperType::LoROM: value = readLoROM(address); break;
        case MapperType::ExLoROM: value = readExLoROM(address); break;
        case MapperType::HiROM: value = readHiROM(address); break;
        case MapperType::ExHiROM: value = readExHiROM(address); break;
        }
    }
    driveOpenBus(value);
    cpu_io_read_bus_ = value;
    return value;
}
bool Bus::isInvalidDMAABusAddress(uint32_t address) const {
    address &= 0xFFFFFFu;
    const uint8_t bank = static_cast<uint8_t>(address >> 16);
    if (!isSystemBank(bank)) return false;
    const uint16_t addr = static_cast<uint16_t>(address);
    return (addr >= 0x2100 && addr <= 0x21FF) ||
           (addr >= 0x4000 && addr <= 0x41FF) ||
           (addr >= 0x4200 && addr <= 0x421F) ||
           (addr >= 0x4300 && addr <= 0x437F);
}
uint8_t Bus::readDMA(int channel, uint32_t address) {
    applyOpenBusDecay();
    address &= 0xFFFFFFu;
    if (isInvalidDMAABusAddress(address)) return open_bus_;
    uint8_t value = 0xFF;
    if (cartridge_ != nullptr && cartridge_->isLoaded() &&
        cartridge_->readHardwareDMA(channel, address, value)) {
        driveOpenBus(value);
        return value;
    }
    return read(address);
}
void Bus::writeDMA(uint32_t address, uint8_t value) {
    address &= 0xFFFFFFu;
    if (isInvalidDMAABusAddress(address)) {
        driveOpenBus(value);
        return;
    }
    write(address, value);
}
void Bus::beginCartridgeDMA(int channel, uint32_t aAddress, uint32_t length, uint8_t control) {
    if (cartridge_ != nullptr && cartridge_->isLoaded())
        cartridge_->beginHardwareDMA(channel, aAddress, length, control);
}
void Bus::endCartridgeDMA(int channel) {
    if (cartridge_ != nullptr && cartridge_->isLoaded()) cartridge_->endHardwareDMA(channel);
}
void Bus::write(uint32_t address, uint8_t value) {
    address &= 0xFFFFFF;
    if (flat_test_memory_enabled_) {
        flatTestPoke(address, value);
        driveOpenBus(value);
        return;
    }
    const uint8_t previousBus = open_bus_;
    driveOpenBus(value);
    const uint8_t bank = static_cast<uint8_t>(address >> 16);
    const uint16_t addr = static_cast<uint16_t>(address);
    if (isSystemBank(bank)) {
        bool handled = false;
        writeSystemArea(bank, addr, value, previousBus, handled);
        if (handled) {
            return;
        }
    }
    if (cartridge_ != nullptr && cartridge_->isLoaded() && cartridge_->writeHardware(address, value)) {
        return;
    }
    if (bank == 0x7E || bank == 0x7F) {
        wram_[((static_cast<uint32_t>(bank) - 0x7E) << 16) | addr] = value;
        return;
    }
    if (cartridge_ == nullptr || !cartridge_->isLoaded()) {
        return;
    }
    switch (cartridge_->mapperType()) {
    case MapperType::LoROM: writeLoROM(address, value); break;
    case MapperType::ExLoROM: writeExLoROM(address, value); break;
    case MapperType::HiROM: writeHiROM(address, value); break;
    case MapperType::ExHiROM: writeExHiROM(address, value); break;
    }
}
uint16_t Bus::read16(uint32_t address) {
    const uint16_t low = read(address);
    const uint16_t high = read((address + 1) & 0xFFFFFF);
    return static_cast<uint16_t>((high << 8) | low);
}
void Bus::write16(uint32_t address, uint16_t value) {
    write(address, static_cast<uint8_t>(value & 0xFF));
    write((address + 1) & 0xFFFFFF, static_cast<uint8_t>((value >> 8) & 0xFF));
}
uint8_t Bus::readSystemArea(uint8_t, uint16_t addr, bool& handled) {
    handled = true;
    if (addr < 0x2000) {
        return wram_[addr];
    }
    if (addr >= 0x2100 && addr <= 0x213F) {
        return ppu_ != nullptr ? ppu_->readRegister(addr) : 0xFF;
    }
    if (addr >= 0x2140 && addr <= 0x217F) {
        return apu_ != nullptr ? apu_->readPort(static_cast<uint8_t>((addr - 0x2140) & 0x03)) : 0xFF;
    }
    if (addr >= 0x2180 && addr <= 0x2183) {
        return readWramPort(addr);
    }
    if (addr == 0x4016) {
        const uint8_t lines = controller1_ != nullptr ? controller1_->readDataLines() : 0;
        return static_cast<uint8_t>((cpu_io_read_bus_ & 0xFC) | (lines & 0x03));
    }
    if (addr == 0x4017) {
        const uint8_t lines = controller2_ != nullptr ? controller2_->readDataLines() : 0;
        return static_cast<uint8_t>((cpu_io_read_bus_ & 0xE0) | 0x1C | (lines & 0x03));
    }
    if (addr >= 0x4200 && addr <= 0x421F) {
        return readCpuIo(addr);
    }
    if (addr >= 0x4300 && addr <= 0x437F) {
        return dma_ != nullptr ? dma_->readRegister(addr) : 0xFF;
    }
    handled = false;
    return 0xFF;
}
void Bus::writeSystemArea(uint8_t, uint16_t addr, uint8_t value, uint8_t previousBus, bool& handled) {
    handled = true;
    if (addr < 0x2000) {
        wram_[addr] = value;
        return;
    }
    if (addr >= 0x2100 && addr <= 0x213F) {
        if (ppu_ != nullptr) {
            ppu_->writeRegister(addr, value, previousBus);
        }
        return;
    }
    if (addr >= 0x2140 && addr <= 0x217F) {
        if (apu_ != nullptr) {
            apu_->writePort(static_cast<uint8_t>((addr - 0x2140) & 0x03), value);
        }
        return;
    }
    if (addr >= 0x2180 && addr <= 0x2183) {
        writeWramPort(addr, value);
        return;
    }
    if (addr == 0x4016) {
        joy_strobe_ = (value & 0x01) != 0;
        updateControllerStrobe();
        return;
    }
    if (addr >= 0x4200 && addr <= 0x421F) {
        writeCpuIo(addr, value);
        return;
    }
    if (addr >= 0x4300 && addr <= 0x437F) {
        if (dma_ != nullptr) {
            dma_->writeRegister(addr, value);
        }
        if (cartridge_ != nullptr && cartridge_->isLoaded()) {
            const int channel = static_cast<int>((addr - 0x4300) >> 4);
            const int offset = static_cast<int>(addr & 0x0F);
            cartridge_->notifyHardwareDMARegisterWrite(channel, offset, value);
        }
        return;
    }
    handled = false;
}
uint8_t Bus::readWramPort(uint16_t addr) {
    switch (addr) {
    case 0x2180: {
        const uint8_t value = wram_[wram_address_ & 0x1FFFF];
        wram_address_ = (wram_address_ + 1) & 0x1FFFF;
        return value;
    }
    case 0x2181:
    case 0x2182:
    case 0x2183:
        return open_bus_;
    default:
        return 0xFF;
    }
}
void Bus::writeWramPort(uint16_t addr, uint8_t value) {
    switch (addr) {
    case 0x2180:
        wram_[wram_address_ & 0x1FFFF] = value;
        wram_address_ = (wram_address_ + 1) & 0x1FFFF;
        break;
    case 0x2181:
        wram_address_ = (wram_address_ & 0x1FF00) | value;
        break;
    case 0x2182:
        wram_address_ = (wram_address_ & 0x100FF) | (static_cast<uint32_t>(value) << 8);
        break;
    case 0x2183:
        wram_address_ = (wram_address_ & 0x0FFFF) | (static_cast<uint32_t>(value & 0x01) << 16);
        break;
    default:
        break;
    }
}
void Bus::stepMathAlu() {
    if (math_mpy_counter_ != 0) {
        --math_mpy_counter_;
        if ((quotient_ & 1u) != 0) {
            product_remainder_ = static_cast<uint16_t>(product_remainder_ + math_shift_);
        }
        quotient_ = static_cast<uint16_t>(quotient_ >> 1);
        math_shift_ <<= 1;
    } else if (math_div_counter_ != 0) {
        --math_div_counter_;
        quotient_ = static_cast<uint16_t>(quotient_ << 1);
        math_shift_ >>= 1;
        if (static_cast<uint32_t>(product_remainder_) >= math_shift_) {
            product_remainder_ = static_cast<uint16_t>(static_cast<uint32_t>(product_remainder_) - math_shift_);
            quotient_ = static_cast<uint16_t>(quotient_ | 1u);
        }
    }
}
void Bus::completeCpuMachineCycle() {
    stepMathAlu();
}
void Bus::setMasterClock(uint64_t masterClock) {
    master_clock_ = masterClock;
}
uint8_t Bus::readCpuIo(uint16_t addr) {
    switch (addr) {
    case 0x4200: case 0x4201: case 0x4202: case 0x4203:
    case 0x4204: case 0x4205: case 0x4206: case 0x4207:
    case 0x4208: case 0x4209: case 0x420A: case 0x420B:
    case 0x420C: case 0x420D: case 0x420E: case 0x420F:
        return cpu_io_read_bus_;
    case 0x4210: {
        const uint8_t value = static_cast<uint8_t>((rdnmi_flag_ ? 0x80 : 0x00) |
            (cpu_io_read_bus_ & 0x70) | scpuVersion());
        rdnmi_flag_ = false;
        updateNmiLine();
        return value;
    }
    case 0x4211: {
        const bool irq_visible = timeup_flag_ || cartridge_irq_;
        const uint8_t value = static_cast<uint8_t>((irq_visible ? 0x80 : 0x00) |
            (cpu_io_read_bus_ & 0x7F));
        const bool comparator_active = timeup_flag_ && master_clock_ < timer_irq_active_until_;
        if (timeup_flag_ && !comparator_active) {
            timeup_flag_ = false;
            timer_irq_line_due_ = UINT64_MAX;
            timer_irq_line_asserted_ = false;
            timer_irq_edge_committed_ = false;
        }
        if (cpu_ != nullptr) {
            cpu_->setIRQLine(cartridge_irq_ || timer_irq_line_asserted_);
        }
        return value;
    }
    case 0x4212: {
        const bool liveVblank = ppu_ != nullptr ? ppu_->vblank() : vblank_;
        const bool liveHblank = ppu_ != nullptr ? ppu_->hblank() : hblank_;
        return static_cast<uint8_t>((liveVblank ? 0x80 : 0x00) | (liveHblank ? 0x40 : 0x00) |
            (cpu_io_read_bus_ & 0x3E) | (auto_joy_active_ ? 0x01 : 0x00));
    }
    case 0x4213:
        return wrio_;
    case 0x4214:
    case 0x4215:
    case 0x4216:
    case 0x4217: {
        if (addr == 0x4214) return static_cast<uint8_t>(quotient_ & 0xFF);
        if (addr == 0x4215) return static_cast<uint8_t>(quotient_ >> 8);
        if (addr == 0x4216) return static_cast<uint8_t>(product_remainder_ & 0xFF);
        return static_cast<uint8_t>(product_remainder_ >> 8);
    }
    case 0x4218: return static_cast<uint8_t>(auto_joy_[0] & 0xFF);
    case 0x4219: return static_cast<uint8_t>(auto_joy_[0] >> 8);
    case 0x421A: return static_cast<uint8_t>(auto_joy_[1] & 0xFF);
    case 0x421B: return static_cast<uint8_t>(auto_joy_[1] >> 8);
    case 0x421C: return static_cast<uint8_t>(auto_joy_[2] & 0xFF);
    case 0x421D: return static_cast<uint8_t>(auto_joy_[2] >> 8);
    case 0x421E: return static_cast<uint8_t>(auto_joy_[3] & 0xFF);
    case 0x421F: return static_cast<uint8_t>(auto_joy_[3] >> 8);
    default: return cpu_io_read_bus_;
    }
}
void Bus::writeCpuIo(uint16_t addr, uint8_t value) {
    switch (addr) {
    case 0x4200: {
        nmitimen_ = value;
        timer_irq_enabled_ = (nmitimen_ & 0x30) != 0;
        last_irq_config_write_ = 0x4200;
        ++irq_config_generation_;
        updateNmiLine();
        if ((nmitimen_ & 0x30) == 0) {
            const bool edge_in_flight = timer_irq_need_ != 0 ||
                (timer_irq_edge_committed_ && master_clock_ < timer_irq_active_until_);
            if (!edge_in_flight) {
                timeup_flag_ = false;
                timer_irq_active_until_ = 0;
                timer_irq_line_due_ = UINT64_MAX;
                timer_irq_line_asserted_ = false;
                timer_irq_edge_committed_ = false;
            }
            if (cpu_ != nullptr) cpu_->setIRQLine(cartridge_irq_ || timer_irq_line_asserted_);
        }
        break;
    }
    case 0x4201: {
        const bool old_latch = (wrio_ & 0x80) != 0;
        const bool new_latch = (value & 0x80) != 0;
        wrio_ = value;
        if (controller1_ != nullptr) controller1_->setIoPort(wrio_);
        if (controller2_ != nullptr) controller2_->setIoPort(wrio_);
        if (old_latch && !new_latch && ppu_ != nullptr) {
            ppu_->latchCounters();
        }
        break;
    }
    case 0x4202:
        wrmpya_ = value;
        break;
    case 0x4203:
        product_remainder_ = 0;
        if (math_mpy_counter_ != 0 || math_div_counter_ != 0) break;
        quotient_ = static_cast<uint16_t>((static_cast<uint16_t>(value) << 8) | wrmpya_);
        math_shift_ = value;
        math_mpy_counter_ = static_cast<uint8_t>(MATH_MUL_CPU_CYCLES);
        break;
    case 0x4204:
        wrdiv_ = static_cast<uint16_t>((wrdiv_ & 0xFF00) | value);
        break;
    case 0x4205:
        wrdiv_ = static_cast<uint16_t>((wrdiv_ & 0x00FF) | (static_cast<uint16_t>(value) << 8));
        break;
    case 0x4206:
        product_remainder_ = wrdiv_;
        if (math_mpy_counter_ != 0 || math_div_counter_ != 0) break;
        math_shift_ = static_cast<uint32_t>(value) << 16;
        math_div_counter_ = static_cast<uint8_t>(MATH_DIV_CPU_CYCLES);
        break;
    case 0x4207:
        htime_ = static_cast<uint16_t>((htime_ & 0x0100) | value);
        irq_match_latched_ = false;
        last_irq_config_write_ = 0x4207;
        ++irq_config_generation_;
        updateTimerIrqLevel(timer_last_hclock_);
        break;
    case 0x4208:
        htime_ = static_cast<uint16_t>((htime_ & 0x00FF) | ((value & 0x01) << 8));
        irq_match_latched_ = false;
        last_irq_config_write_ = 0x4208;
        ++irq_config_generation_;
        updateTimerIrqLevel(timer_last_hclock_);
        break;
    case 0x4209:
        vtime_ = static_cast<uint16_t>((vtime_ & 0x0100) | value);
        irq_match_latched_ = false;
        last_irq_config_write_ = 0x4209;
        ++irq_config_generation_;
        updateTimerIrqLevel(timer_last_hclock_);
        break;
    case 0x420A:
        vtime_ = static_cast<uint16_t>((vtime_ & 0x00FF) | ((value & 0x01) << 8));
        irq_match_latched_ = false;
        last_irq_config_write_ = 0x420A;
        ++irq_config_generation_;
        updateTimerIrqLevel(timer_last_hclock_);
        break;
    case 0x420B:
        mdmaen_ = value;
        if (dma_ != nullptr && value != 0) {
            dma_->startDMA(value, master_clock_);
        }
        mdmaen_ = 0;
        break;
    case 0x420C:
        hdmaen_ = value;
        if (dma_ != nullptr) {
            dma_->setHDMAEnable(value);
        }
        break;
    case 0x420D:
        memsel_ = static_cast<uint8_t>(value & 0x01);
        break;
    default:
        break;
    }
}
void Bus::updateControllerStrobe() {
    const bool high = joy_strobe_ || auto_joy_strobe_;
    if (controller1_ != nullptr) controller1_->setStrobe(high);
    if (controller2_ != nullptr) controller2_->setStrobe(high);
}
void Bus::beginAutoJoypad() {
    auto_joy_active_ = true;
    auto_joy_elapsed_clocks_ = 0;
    auto_joy_next_sample_clock_ = 256;
    auto_joy_latch_released_ = false;
    auto_joy_strobe_ = true;
    updateControllerStrobe();
}
void Bus::advanceAutoJoypad(uint32_t masterClocks) {
    if (!auto_joy_active_ || masterClocks == 0) return;
    const uint32_t oldElapsed = auto_joy_elapsed_clocks_;
    auto_joy_elapsed_clocks_ = std::min<uint32_t>(4224u, auto_joy_elapsed_clocks_ + masterClocks);
    if (!auto_joy_latch_released_ && oldElapsed < 128u && auto_joy_elapsed_clocks_ >= 128u) {
        auto_joy_strobe_ = false;
        updateControllerStrobe();
        auto_joy_.fill(0);
        auto_joy_latch_released_ = true;
    }
    while (auto_joy_next_sample_clock_ <= 4096u &&
           auto_joy_next_sample_clock_ <= auto_joy_elapsed_clocks_) {
        const uint8_t port1 = controller1_ != nullptr ? controller1_->readDataLines() : 0;
        const uint8_t port2 = controller2_ != nullptr ? controller2_->readDataLines() : 0;
        auto_joy_[0] = static_cast<uint16_t>((auto_joy_[0] << 1) | (port1 & 0x01));
        auto_joy_[2] = static_cast<uint16_t>((auto_joy_[2] << 1) | ((port1 >> 1) & 0x01));
        auto_joy_[1] = static_cast<uint16_t>((auto_joy_[1] << 1) | (port2 & 0x01));
        auto_joy_[3] = static_cast<uint16_t>((auto_joy_[3] << 1) | ((port2 >> 1) & 0x01));
        auto_joy_next_sample_clock_ = static_cast<uint16_t>(auto_joy_next_sample_clock_ + 256u);
    }
    if (auto_joy_elapsed_clocks_ >= 4224u) auto_joy_active_ = false;
}
void Bus::updateVideoTiming(int scanline, int dot, bool hblank, bool vblank) {
    hblank_ = hblank;
    vblank_ = vblank;
    const bool vblankEdge = vblank_ != previous_vblank_;
    const bool scope1Active = controller1_ != nullptr && controller1_->lightGunActive();
    const bool scope2Active = controller2_ != nullptr && controller2_->lightGunActive();
    if (!auto_joy_active_ && !auto_joy_pending_ && !vblankEdge &&
        !scope1Active && !scope2Active) {
        return;
    }
    if (ppu_ != nullptr && ((scope1Active && controller1_->lightGunLatchAt(scanline, dot)) ||
                            (scope2Active && controller2_->lightGunLatchAt(scanline, dot)))) {
        ppu_->latchCounters();
    }
    if (auto_joy_active_ && ppu_ != nullptr) {
        int completedDot = dot - 1;
        int completedLine = scanline;
        if (completedDot < 0) {
            const bool currentOddField = (ppu_->frameCounter() & 1u) != 0;
            const bool completedOddField = scanline > 0 ? currentOddField : !currentOddField;
            if (scanline > 0) {
                completedLine = scanline - 1;
            } else {
                const int baseLines = ppu_->palRegion() ? 312 : 262;
                const int completedFieldLines = baseLines +
                    ((ppu_->interlaceEnabled() && completedOddField) ? 1 : 0);
                completedLine = completedFieldLines - 1;
            }
            completedDot = ppu_->masterClocksForLine(completedLine, completedOddField) == 1368
                ? PPU::LONG_DOTS_PER_LINE - 1
                : PPU::DOTS_PER_LINE - 1;
        }
        const bool oddField = (ppu_->frameCounter() & 1u) != 0;
        const bool shortLine = !ppu_->palRegion() && !ppu_->interlaceEnabled() && oddField && completedLine == 240;
        const uint32_t clocks = (!shortLine && (completedDot == 323 || completedDot == 327)) ? 6u : 4u;
        advanceAutoJoypad(clocks);
    }
    if (vblankEdge) {
        if (vblank_) {
            auto_joy_pending_ = (nmitimen_ & 0x01) != 0;
        } else {
            rdnmi_flag_ = false;
            updateNmiLine();
            auto_joy_pending_ = false;
            auto_joy_active_ = false;
            auto_joy_strobe_ = false;
            updateControllerStrobe();
            auto_joy_elapsed_clocks_ = 0;
        }
        previous_vblank_ = vblank_;
    }
    static constexpr int kAutoJoyStartDot = 33;
    if (auto_joy_pending_ && ppu_ != nullptr && scanline == ppu_->vblankStartLine() &&
        dot == kAutoJoyStartDot) {
        beginAutoJoypad();
        auto_joy_pending_ = false;
    }
}
void Bus::triggerVBlankNmiEdge() {
    rdnmi_flag_ = true;
    updateNmiLine();
}
void Bus::updateNmiLine() {
    const bool level = rdnmi_flag_ && ((nmitimen_ & 0x80) != 0);
    if (!nmi_line_level_ && level && cpu_ != nullptr) cpu_->requestNMI();
    nmi_line_level_ = level;
}
void Bus::processNmiCircuitTick(uint16_t hClock, uint16_t scanline) {
    nmi_last_hclock_ = hClock;
    if (ppu_ == nullptr) return;
    if (hClock == 2 && scanline == static_cast<uint16_t>(ppu_->vblankStartLine())) {
        rdnmi_flag_ = true;
        updateNmiLine();
    }
}
void Bus::setCartridgeIrq(bool asserted) {
    cartridge_irq_ = asserted;
    if (cpu_ != nullptr) cpu_->setIRQLine(cartridge_irq_ || timer_irq_line_asserted_);
}
void Bus::updateTimerIrqLevel(uint16_t hClock) {
    if (!timer_irq_enabled_) {
        timer_irq_level_ = false;
        return;
    }
    const bool h_enabled = (nmitimen_ & 0x10) != 0;
    const bool v_enabled = (nmitimen_ & 0x20) != 0;
    const bool level = (!h_enabled || timer_hcounter_ == htime_) &&
                       (!v_enabled || timer_vcounter_ == vtime_);
    if (!timer_irq_level_ && level) {
        timer_irq_need_ = (h_enabled && hClock == 6) ? 3 : 2;
        timer_irq_edge_committed_ = true;
    }
    timer_irq_level_ = level;
}
void Bus::processTimerIrqCircuitTick(uint16_t hClock, uint16_t scanline) {
    timer_last_hclock_ = hClock;
    if (timer_irq_need_ > 0) {
        --timer_irq_need_;
        if (timer_irq_need_ == 1) {
            timeup_flag_ = true;
            timer_irq_active_until_ = master_clock_ + 4;
        } else if (timer_irq_need_ == 0) {
            timer_irq_line_asserted_ = timeup_flag_ && timer_irq_edge_committed_;
            timer_irq_edge_committed_ = false;
            if (cpu_ != nullptr) cpu_->setIRQLine(cartridge_irq_ || timer_irq_line_asserted_);
        }
    }
    if (hClock > 10) {
        ++timer_hcounter_;
    } else if (hClock == 10) {
        timer_hcounter_ = 0;
    } else if (hClock == 6) {
        timer_hcounter_ = 0;
        if (scanline > 0) ++timer_vcounter_;
    } else if (hClock == 2) {
        ++timer_hcounter_;
        if (scanline == 0) timer_vcounter_ = 0;
    }
    updateTimerIrqLevel(hClock);
}
void Bus::triggerTimerIrq() {
    if ((nmitimen_ & 0x30) == 0) return;
    timeup_flag_ = true;
    timer_irq_active_until_ = master_clock_ + 4;
    timer_irq_line_due_ = master_clock_ + 4;
    timer_irq_line_asserted_ = false;
    timer_irq_edge_committed_ = true;
    irq_match_latched_ = true;
    if (cpu_ != nullptr) cpu_->setIRQLine(cartridge_irq_);
}
void Bus::serviceTimerIrqLine() {
    if (timer_irq_line_due_ == UINT64_MAX || master_clock_ < timer_irq_line_due_) return;
    timer_irq_line_due_ = UINT64_MAX;
    timer_irq_line_asserted_ = timeup_flag_ && timer_irq_edge_committed_;
    timer_irq_edge_committed_ = false;
    if (cpu_ != nullptr) cpu_->setIRQLine(cartridge_irq_ || timer_irq_line_asserted_);
}
uint8_t Bus::readLoROM(uint32_t address) {
    const uint8_t bank = static_cast<uint8_t>(address >> 16);
    const uint16_t addr = static_cast<uint16_t>(address);
    if ((((bank >= 0x70) && (bank <= 0x7D)) || bank >= 0xF0) && addr < 0x8000 &&
        cartridge_->sramSize() != 0) {
        const uint32_t sram_address = (static_cast<uint32_t>(bank & 0x0F) << 15) | addr;
        return cartridge_->read_sram(sram_address);
    }
    if (addr >= 0x8000 || ((bank >= 0x40 && bank <= 0x7D) || bank >= 0xC0)) {
        const uint32_t rom_address = (static_cast<uint32_t>(bank & 0x7F) << 15) | (addr & 0x7FFF);
        return cartridge_->read_rom(rom_address);
    }
    return 0xFF;
}
void Bus::writeLoROM(uint32_t address, uint8_t value) {
    const uint8_t bank = static_cast<uint8_t>(address >> 16);
    const uint16_t addr = static_cast<uint16_t>(address);
    if ((((bank >= 0x70) && (bank <= 0x7D)) || bank >= 0xF0) && addr < 0x8000 &&
        cartridge_->sramSize() != 0) {
        const uint32_t sram_address = (static_cast<uint32_t>(bank & 0x0F) << 15) | addr;
        cartridge_->write_sram(sram_address, value);
    }
}
uint8_t Bus::readExLoROM(uint32_t address) {
    const uint8_t bank = static_cast<uint8_t>(address >> 16);
    const uint16_t addr = static_cast<uint16_t>(address);
    if ((((bank >= 0x70) && (bank <= 0x7D)) || bank >= 0xF0) && addr < 0x8000 &&
        cartridge_->sramSize() != 0) {
        const uint32_t sram_address = (static_cast<uint32_t>(bank & 0x0F) << 15) | addr;
        return cartridge_->read_sram(sram_address);
    }
    if (addr >= 0x8000) {
        uint32_t rom_address = (static_cast<uint32_t>(bank & 0x7F) << 15) | (addr & 0x7FFF);
        if (bank < 0x80) rom_address += 0x400000;
        return cartridge_->read_rom(rom_address);
    }
    return 0xFF;
}
void Bus::writeExLoROM(uint32_t address, uint8_t value) {
    const uint8_t bank = static_cast<uint8_t>(address >> 16);
    const uint16_t addr = static_cast<uint16_t>(address);
    if ((((bank >= 0x70) && (bank <= 0x7D)) || bank >= 0xF0) && addr < 0x8000 &&
        cartridge_->sramSize() != 0) {
        const uint32_t sram_address = (static_cast<uint32_t>(bank & 0x0F) << 15) | addr;
        cartridge_->write_sram(sram_address, value);
    }
}
uint8_t Bus::readHiROM(uint32_t address) {
    const uint8_t bank = static_cast<uint8_t>(address >> 16);
    const uint16_t addr = static_cast<uint16_t>(address);
    if ((((bank >= 0x20) && (bank <= 0x3F)) || ((bank >= 0xA0) && (bank <= 0xBF))) &&
        addr >= 0x6000 && addr < 0x8000 && cartridge_->sramSize() != 0) {
        const uint32_t sram_address = (static_cast<uint32_t>(bank & 0x1F) << 13) | (addr - 0x6000);
        return cartridge_->read_sram(sram_address);
    }
    if ((bank >= 0x40 && bank <= 0x7D) || bank >= 0xC0 || addr >= 0x8000) {
        const uint32_t rom_address = (static_cast<uint32_t>(bank & 0x3F) << 16) | addr;
        return cartridge_->read_rom(rom_address);
    }
    return 0xFF;
}
void Bus::writeHiROM(uint32_t address, uint8_t value) {
    const uint8_t bank = static_cast<uint8_t>(address >> 16);
    const uint16_t addr = static_cast<uint16_t>(address);
    if ((((bank >= 0x20) && (bank <= 0x3F)) || ((bank >= 0xA0) && (bank <= 0xBF))) &&
        addr >= 0x6000 && addr < 0x8000 && cartridge_->sramSize() != 0) {
        const uint32_t sram_address = (static_cast<uint32_t>(bank & 0x1F) << 13) | (addr - 0x6000);
        cartridge_->write_sram(sram_address, value);
    }
}
uint8_t Bus::readExHiROM(uint32_t address) {
    const uint8_t bank = static_cast<uint8_t>(address >> 16);
    const uint16_t addr = static_cast<uint16_t>(address);
    if ((((bank >= 0x20) && (bank <= 0x3F)) || ((bank >= 0xA0) && (bank <= 0xBF))) &&
        addr >= 0x6000 && addr < 0x8000 && cartridge_->sramSize() != 0) {
        const uint32_t sram_address = (static_cast<uint32_t>(bank & 0x1F) << 13) | (addr - 0x6000);
        return cartridge_->read_sram(sram_address);
    }
    const bool full_bank_rom = (bank >= 0x40 && bank <= 0x7D) || bank >= 0xC0;
    const bool upper_half_rom = addr >= 0x8000;
    if (!full_bank_rom && !upper_half_rom) {
        return 0xFF;
    }
    uint32_t rom_address = (static_cast<uint32_t>(bank & 0x3F) << 16) | addr;
    if (bank < 0x80) {
        rom_address += 0x400000;
    }
    return cartridge_->read_rom(rom_address);
}
void Bus::writeExHiROM(uint32_t address, uint8_t value) {
    const uint8_t bank = static_cast<uint8_t>(address >> 16);
    const uint16_t addr = static_cast<uint16_t>(address);
    if ((((bank >= 0x20) && (bank <= 0x3F)) || ((bank >= 0xA0) && (bank <= 0xBF))) &&
        addr >= 0x6000 && addr < 0x8000 && cartridge_->sramSize() != 0) {
        const uint32_t sram_address = (static_cast<uint32_t>(bank & 0x1F) << 13) | (addr - 0x6000);
        cartridge_->write_sram(sram_address, value);
    }
}

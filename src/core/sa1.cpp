#include "sa1.hpp"
#include "cpu.hpp"
#include "state_io.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <sstream>
#include <iomanip>
namespace {
class SA1Device final : public CartridgeDevice {
public:
    SA1Device(const std::vector<uint8_t>& rom, std::vector<uint8_t>& bwram, bool& dirty, bool palRegion)
        : rom_(rom), bwram_(bwram), bwram_dirty_(dirty), pal_region_(palRegion),
          rom_mirror_mask_(highestPowerOfTwo(rom.size())),
          rom_power2_mask_(isPowerOfTwo(rom.size()) ? rom.size() - 1 : 0),
          bwram_power2_mask_(isPowerOfTwo(bwram.size()) ? bwram.size() - 1 : 0) {
        CPU::ExternalBus ext{};
        ext.context = this;
        ext.read = [](void* p, uint32_t a) { return static_cast<SA1Device*>(p)->readCcpu(a); };
        ext.write = [](void* p, uint32_t a, uint8_t v) { static_cast<SA1Device*>(p)->writeCcpu(a, v); };
        ext.access_master_clocks = [](void* p, uint32_t a) { return static_cast<SA1Device*>(p)->accessCcpu(a); };
        ext.read_timed = [](void* p, uint32_t a, uint8_t* clocks) {
            return static_cast<SA1Device*>(p)->readCcpu(a, clocks);
        };
        ext.write_timed = [](void* p, uint32_t a, uint8_t v, uint8_t* clocks) {
            static_cast<SA1Device*>(p)->writeCcpu(a, v, clocks);
        };
        cpu_.attachExternalBus(ext);
        cpu_.setInternalCycleMasterClocks(2);
        cpu_.setCycleStepping(false);
        cpu_.setInternalCycleCoalescing(true);
        rebuildRomMapCache();
    }
    void reset() override {
        iram_.fill(0);
        mmc_ = {0x00, 0x01, 0x02, 0x03};
        rebuildRomMapCache();
        ccnt_ = 0x20;
        sie_ = sic_ = scnt_ = cie_ = cic_ = 0;
        crv_ = cnv_ = civ_ = 0;
        snv_ = siv_ = 0;
        bmaps_ = bmap_ = 0;
        sbwe_ = cbwe_ = siwp_ = ciwp_ = 0;
        bwpa_ = 0x0F;
        bwpa_written_ = false;
        mcnt_ = 0;
        ma_ = mb_ = 0;
        math_result_ = 0;
        math_overflow_ = false;
        math_pending_ = false;
        math_pending_result_ = 0;
        math_pending_overflow_ = false;
        math_clocks_remaining_ = 0;
        cpu_running_ = false;
        clock_credit_ = 0;
        ccpu_open_bus_ = 0;
        host_contention_slots_.fill(0);
        host_fastrom_pause_clocks_ = 0;
        rom_pair_valid_ = false;
        rom_pair_next_ = 0;
        bwram_write_buffer_clocks_ = 0;
        host_irq_pending_ = false;
        host_chdma_pending_ = false;
        last_host_reg_sequence_ = 0;
        last_host_reg_address_ = 0;
        last_host_reg_value_ = 0;
        last_host_reg_write_ = false;
        sa1_irq_pending_ = false;
        sa1_nmi_pending_ = false;
        sa1_nmi_signaled_ = false;
        timer_irq_pending_ = false;
        dma_irq_pending_ = false;
        tmc_ = 0;
        hcnt_target_ = vcnt_target_ = 0;
        timer_dot_remainder_ = 0;
        timer_deferred_clocks_ = 0;
        timer_h_ = timer_v_ = 0;
        vbd_ = 0;
        vda_ = 0;
        variable_bit_pos_ = 0;
        variable_data_ = 0;
        brf_.fill(0);
        cc2_pixels_.fill(0);
        cc2_group_ = 0;
        cc2_line_ = 0;
        dma_active_ = false;
        cc1_active_ = false;
        dma_clock_credit_ = 0;
        dma_remaining_ = 0;
        dma_src_ = dma_dst_ = 0;
        cpu_.reset();
        CPU::State state = cpu_.state();
        state.pc = 0;
        state.pb = 0;
        cpu_.setStateForTesting(state);
    }
    bool implemented() const override { return true; }
    CartridgeSupportLevel supportLevel() const override { return CartridgeSupportLevel::TimingApproximate; }
    uint32_t stateTag() const override { return 0x20314153u; }
    std::vector<uint8_t> saveState() const override {
        stateio::Writer w; uint32_t version = 10; w.pod(version);
        w.array(iram_); w.array(mmc_); w.vector(bwram_);
        const auto cpuState = cpu_.runtimeState(); w.pod(cpuState);
        w.pod(ccnt_); w.pod(sie_); w.pod(sic_); w.pod(scnt_); w.pod(cie_); w.pod(cic_);
        w.pod(crv_); w.pod(cnv_); w.pod(civ_); w.pod(snv_); w.pod(siv_);
        w.pod(bmaps_); w.pod(bmap_); w.pod(sbwe_); w.pod(cbwe_); w.pod(bwpa_); w.pod(siwp_); w.pod(ciwp_); w.pod(bwpa_written_);
        w.pod(dcnt_); w.pod(cdma_); w.pod(bbf_); w.pod(sda_); w.pod(dda_); w.pod(dtc_);
        w.pod(mcnt_); w.pod(ma_); w.pod(mb_); w.pod(math_result_); w.pod(math_overflow_);
        w.pod(math_pending_); w.pod(math_pending_result_); w.pod(math_pending_overflow_); w.pod(math_clocks_remaining_);
        w.pod(cpu_running_); w.pod(clock_credit_); w.pod(ccpu_open_bus_); w.array(host_contention_slots_); w.pod(host_fastrom_pause_clocks_);
        w.pod(rom_pair_valid_); w.pod(rom_pair_next_); w.pod(bwram_write_buffer_clocks_);
        w.pod(host_irq_pending_); w.pod(host_chdma_pending_); w.pod(sa1_irq_pending_); w.pod(sa1_nmi_pending_); w.pod(sa1_nmi_signaled_); w.pod(timer_irq_pending_); w.pod(dma_irq_pending_);
        w.pod(tmc_); w.pod(hcnt_target_); w.pod(vcnt_target_); w.pod(timer_dot_remainder_); w.pod(timer_deferred_clocks_); w.pod(timer_h_); w.pod(timer_v_);
        w.pod(vbd_); w.pod(vda_); w.pod(variable_bit_pos_); w.pod(variable_data_);
        w.array(brf_); w.array(cc2_pixels_); w.pod(cc2_group_); w.pod(cc2_line_);
        w.pod(cc1_active_); w.pod(dma_active_); w.pod(dma_clock_credit_); w.pod(dma_src_); w.pod(dma_dst_); w.pod(dma_remaining_);
        return w.take();
    }
    bool loadState(const std::vector<uint8_t>& state) override {
        stateio::Reader r(state); uint32_t version = 0; CPU::RuntimeState cpuState{};
        if (!r.pod(version) || (version != 5 && version != 6 && version != 7 && version != 8 && version != 9 && version != 10) || !r.array(iram_) || !r.array(mmc_) || !r.vector(bwram_, 2 * 1024 * 1024) || !r.pod(cpuState) ||
            !r.pod(ccnt_) || !r.pod(sie_) || !r.pod(sic_) || !r.pod(scnt_) || !r.pod(cie_) || !r.pod(cic_) ||
            !r.pod(crv_) || !r.pod(cnv_) || !r.pod(civ_) || !r.pod(snv_) || !r.pod(siv_) ||
            !r.pod(bmaps_) || !r.pod(bmap_) || !r.pod(sbwe_) || !r.pod(cbwe_) || !r.pod(bwpa_) || !r.pod(siwp_) || !r.pod(ciwp_) || !r.pod(bwpa_written_) ||
            !r.pod(dcnt_) || !r.pod(cdma_) || !r.pod(bbf_) || !r.pod(sda_) || !r.pod(dda_) || !r.pod(dtc_) ||
            !r.pod(mcnt_) || !r.pod(ma_) || !r.pod(mb_) || !r.pod(math_result_) || !r.pod(math_overflow_) ||
            !r.pod(math_pending_) || !r.pod(math_pending_result_) || !r.pod(math_pending_overflow_) || !r.pod(math_clocks_remaining_) ||
            !r.pod(cpu_running_) || !r.pod(clock_credit_) || !r.pod(ccpu_open_bus_)) return false;
        if (version == 5) {
            uint8_t pending = 0;
            if (!r.pod(pending)) return false;
            host_contention_slots_.fill(0);
            for (unsigned i = 0; i < host_contention_slots_.size(); ++i)
                host_contention_slots_[i] = (pending & (1u << i)) ? 1u : 0u;
        } else if (version <= 7) {
            std::array<uint8_t, 3> legacySlots{};
            if (!r.array(legacySlots)) return false;
            for (unsigned i = 0; i < host_contention_slots_.size(); ++i) host_contention_slots_[i] = legacySlots[i];
        } else if (!r.array(host_contention_slots_)) return false;
        if (!r.pod(host_fastrom_pause_clocks_)) return false;
        if (version >= 7) {
            if (!r.pod(rom_pair_valid_) || !r.pod(rom_pair_next_) || !r.pod(bwram_write_buffer_clocks_)) return false;
        } else {
            rom_pair_valid_ = false;
            rom_pair_next_ = 0;
            bwram_write_buffer_clocks_ = 0;
        }
        if (!r.pod(host_irq_pending_) || !r.pod(host_chdma_pending_)) return false;
        if (version >= 10) {
            if (!r.pod(sa1_irq_pending_) || !r.pod(sa1_nmi_pending_) || !r.pod(sa1_nmi_signaled_)) return false;
        } else {
            sa1_irq_pending_ = (ccnt_ & 0x80u) != 0;
            sa1_nmi_pending_ = (ccnt_ & 0x10u) != 0;
            sa1_nmi_signaled_ = false;
        }
        if (!r.pod(timer_irq_pending_) || !r.pod(dma_irq_pending_) ||
            !r.pod(tmc_) || !r.pod(hcnt_target_) || !r.pod(vcnt_target_) || !r.pod(timer_dot_remainder_) || !r.pod(timer_deferred_clocks_) || !r.pod(timer_h_) || !r.pod(timer_v_) ||
            !r.pod(vbd_) || !r.pod(vda_) || !r.pod(variable_bit_pos_) || !r.pod(variable_data_) ||
            !r.array(brf_) || !r.array(cc2_pixels_) || !r.pod(cc2_group_)) return false;
        if (version >= 9) {
            if (!r.pod(cc2_line_)) return false;
        } else {
            cc2_line_ = 0;
        }
        if (!r.pod(cc1_active_) || !r.pod(dma_active_) || !r.pod(dma_clock_credit_) || !r.pod(dma_src_) || !r.pod(dma_dst_) || !r.pod(dma_remaining_) || !r.done()) return false;
        if (cc2_group_ > 3 || cc2_line_ > 15 || (tmc_ & ~0x83u) != 0 || math_clocks_remaining_ < 0 || math_clocks_remaining_ > 12 ||
            host_fastrom_pause_clocks_ > 12u || bwram_write_buffer_clocks_ > 4u || rom_pair_next_ > 0xFFFFFFu) return false;
        for (uint16_t slots : host_contention_slots_) if (slots > kMaxHostContentionSlots) return false;
        if (!cpu_.restoreRuntimeState(cpuState)) return false;
        rebuildRomMapCache();
        bwram_dirty_ = true;
        updateCcpuInterrupts();
        return true;
    }
    bool needsClock() const override { return true; }
    bool hostIrqPending() const override {
        return (host_irq_pending_ && (sie_ & 0x80) != 0) ||
               (host_chdma_pending_ && (sie_ & 0x20) != 0);
    }
    bool hostIrqMayChangeWithoutHostAccess() const override {
        return (sie_ & 0xA0) != 0;
    }
    bool hostAccessRequiresClock(uint32_t address, bool write) const override {
        (void)write;
        address &= 0xFFFFFFu;
        const uint8_t bank = static_cast<uint8_t>(address >> 16);
        const uint16_t addr = static_cast<uint16_t>(address);
        const bool systemBank = isSystemBank(bank);
        if (bank >= 0xC0) return false;
        if (systemBank && addr >= 0x8000) return false;
        if (bank == 0x00 && (addr >= 0xFFEA)) return false;
        if (systemBank && addr >= 0x2200 && addr <= 0x23FF) return true;
        if (systemBank && addr >= 0x3000 && addr <= 0x37FF) return true;
        if (systemBank && addr >= 0x6000 && addr <= 0x7FFF) return true;
        if (bank >= 0x40 && bank <= 0x7F) return true;
        return false;
    }
    void hostAccessBegin(uint32_t address, bool write, uint8_t masterClocks) override {
        if (write || masterClocks != 6 || !cpu_running_ || (ccnt_ & 0x60u) != 0) return;
        address &= 0xFFFFFFu;
        const uint8_t bank = static_cast<uint8_t>(address >> 16);
        const uint16_t addr = static_cast<uint16_t>(address);
        const bool rom = (bank >= 0xC0u) || (isSystemBank(bank) && bank >= 0x80u && addr >= 0x8000u);
        if (!rom) return;
        host_fastrom_pause_clocks_ = static_cast<uint8_t>(std::min<unsigned>(12u, host_fastrom_pause_clocks_ + masterClocks));
    }
    bool clockAndHostIrq(uint64_t masterClocks) override {
        SA1Device::clock(masterClocks);
        return (host_irq_pending_ && (sie_ & 0x80) != 0) ||
               (host_chdma_pending_ && (sie_ & 0x20) != 0);
    }
    void clock(uint64_t masterClocks) override {
        uint64_t arithmeticClocksRemaining = masterClocks;
        clockTimer(masterClocks);
        clockDma(masterClocks);
        if (!cpu_running_ || (ccnt_ & 0x60) != 0) {
            clockArithmetic(arithmeticClocksRemaining);
            return;
        }
        if (dmaBlocksCpu()) {
            clockArithmetic(arithmeticClocksRemaining);
            return;
        }
        uint64_t cpuClocks = masterClocks;
        if (host_fastrom_pause_clocks_ != 0) {
            const uint64_t paused = std::min<uint64_t>(cpuClocks, host_fastrom_pause_clocks_);
            cpuClocks -= paused;
            host_fastrom_pause_clocks_ = static_cast<uint8_t>(host_fastrom_pause_clocks_ - paused);
        }
        clock_credit_ += static_cast<int64_t>(cpuClocks);
        while (cpu_running_ && clock_credit_ >= 2) {
            const uint16_t beforePc = cpu_.programCounter();
            const uint8_t beforePb = cpu_.programBank();
            cpu_.step();
            uint32_t cost = cpu_.lastMasterClocks();
            if (cost == 0) cost = 2;
            if (cpu_.lastStepExecutedOpcode()) {
                cost += sa1ControlTransferWait(cpu_.lastOpcode(), beforePc, beforePb,
                                               cpu_.programCounter(), cpu_.programBank());
            }
            if (arithmeticClocksRemaining != 0) {
                const uint64_t elapsed = std::min<uint64_t>(arithmeticClocksRemaining, cost);
                clockArithmetic(elapsed);
                arithmeticClocksRemaining -= elapsed;
            }
            clock_credit_ -= static_cast<int64_t>(cost);
            if (cpu_.stopped()) cpu_running_ = false;
            if (dmaBlocksCpu()) break;
            if ((ccnt_ & 0x60) != 0) break;
        }
        if (arithmeticClocksRemaining != 0)
            clockArithmetic(arithmeticClocksRemaining);
    }
    bool readHost(uint32_t address, uint8_t openBus, uint8_t& value) override {
        host_open_bus_ = openBus;
        return read(address, value);
    }
    bool read(uint32_t address, uint8_t& value) override {
        address &= 0xFFFFFFu;
        const uint8_t bank = static_cast<uint8_t>(address >> 16);
        const uint16_t addr = static_cast<uint16_t>(address);
        if (bank == 0x00) {
            if ((scnt_ & 0x10) && (addr == 0xFFEA || addr == 0xFFEB)) {
                value = addr & 1 ? static_cast<uint8_t>(snv_ >> 8) : static_cast<uint8_t>(snv_);
                return true;
            }
            if ((scnt_ & 0x40) && (addr == 0xFFEE || addr == 0xFFEF)) {
                value = addr & 1 ? static_cast<uint8_t>(siv_ >> 8) : static_cast<uint8_t>(siv_);
                return true;
            }
        }
        if (isSystemBank(bank)) {
            if (addr >= 0x2200 && addr <= 0x23FF) {
                value = (addr == 0x2300) ? sfr() : host_open_bus_;
                last_host_reg_address_ = addr;
                last_host_reg_value_ = value;
                last_host_reg_write_ = false;
                ++last_host_reg_sequence_;
                return true;
            }
            if (addr >= 0x3000 && addr <= 0x37FF) {
                chargeHostDmaContention(SharedBus::Iram);
                queueHostCpuContention(SharedBus::Iram);
                value = iram_[addr & 0x07FF];
                return true;
            }
            if (addr >= 0x6000 && addr <= 0x7FFF) {
                const uint32_t bwaddr = bwramMirrorAddress(bmaps_, addr);
                chargeHostDmaContention(SharedBus::Bwram);
            queueHostCpuContention(SharedBus::Bwram);
                value = cc1_active_ ? readCharacterConversion1(bwaddr) : readBwram(bwaddr);
                return true;
            }
            if (addr >= 0x8000) {
                chargeHostDmaContention(SharedBus::Rom);
            queueHostCpuContention(SharedBus::Rom);
                value = readRom(mapRom(bank, addr));
                return true;
            }
        }
        if (bank >= 0x40 && bank <= 0x4F) {
            const uint32_t bwaddr = ((static_cast<uint32_t>(bank) - 0x40u) << 16) | addr;
            chargeHostDmaContention(SharedBus::Bwram);
            queueHostCpuContention(SharedBus::Bwram);
            value = cc1_active_ ? readCharacterConversion1(bwaddr) : readBwram(bwaddr);
            return true;
        }
        if (bank >= 0xC0) {
            chargeHostDmaContention(SharedBus::Rom);
            queueHostCpuContention(SharedBus::Rom);
            value = readRom(mapRom(bank, addr));
            return true;
        }
        return false;
    }
    bool write(uint32_t address, uint8_t value) override {
        address &= 0xFFFFFFu;
        const uint8_t bank = static_cast<uint8_t>(address >> 16);
        const uint16_t addr = static_cast<uint16_t>(address);
        if (isSystemBank(bank)) {
            if (addr >= 0x2200 && addr <= 0x22FF) {
                writeScpuReg(addr, value);
                last_host_reg_address_ = addr;
                last_host_reg_value_ = value;
                last_host_reg_write_ = true;
                ++last_host_reg_sequence_;
                return true;
            }
            if (addr >= 0x3000 && addr <= 0x37FF) {
                chargeHostDmaContention(SharedBus::Iram);
                queueHostCpuContention(SharedBus::Iram);
                writeIram(addr & 0x07FF, value, false);
                return true;
            }
            if (addr >= 0x6000 && addr <= 0x7FFF) {
                chargeHostDmaContention(SharedBus::Bwram);
            queueHostCpuContention(SharedBus::Bwram);
                writeBwram(bwramMirrorAddress(bmaps_, addr), value, false);
                return true;
            }
        }
        if (bank >= 0x40 && bank <= 0x4F) {
            chargeHostDmaContention(SharedBus::Bwram);
            queueHostCpuContention(SharedBus::Bwram);
            writeBwram(((static_cast<uint32_t>(bank) - 0x40u) << 16) | addr, value, false);
            return true;
        }
        return false;
    }
private:
    enum class SharedBus : uint8_t { Rom, Bwram, Iram };
    enum class ControlWaitClass : uint8_t { None, Always, Branch };
    static uint32_t sa1ControlTransferWait(uint8_t opcode, uint16_t beforePc, uint8_t beforePb,
                                           uint16_t afterPc, uint8_t afterPb) {
        static constexpr auto kControlWaitClass = [] {
            std::array<ControlWaitClass, 256> table{};
            table.fill(ControlWaitClass::None);
            for (uint8_t op : {uint8_t{0x00}, uint8_t{0x02}, uint8_t{0x20}, uint8_t{0x22},
                               uint8_t{0x40}, uint8_t{0x4C}, uint8_t{0x5C}, uint8_t{0x60},
                               uint8_t{0x6B}, uint8_t{0x6C}, uint8_t{0x7C}, uint8_t{0xDC},
                               uint8_t{0xFC}})
                table[op] = ControlWaitClass::Always;
            for (uint8_t op : {uint8_t{0x10}, uint8_t{0x30}, uint8_t{0x50}, uint8_t{0x70},
                               uint8_t{0x80}, uint8_t{0x82}, uint8_t{0x90}, uint8_t{0xB0},
                               uint8_t{0xD0}, uint8_t{0xF0}})
                table[op] = ControlWaitClass::Branch;
            return table;
        }();
        const ControlWaitClass kind = kControlWaitClass[opcode];
        if (kind == ControlWaitClass::None) return 0;
        const uint32_t destination = (static_cast<uint32_t>(afterPb) << 16) | afterPc;
        const bool romDestination = (destination & 0x408000u) == 0x008000u || (destination & 0xC00000u) == 0xC00000u;
        if (!romDestination) return 0;
        if (kind == ControlWaitClass::Always) return 2;
        const bool always = opcode == 0x80 || opcode == 0x82;
        const uint16_t sequential = static_cast<uint16_t>(beforePc + (opcode == 0x82 ? 3u : 2u));
        const bool taken = always || afterPc != sequential || afterPb != beforePb;
        return taken && (afterPc & 1u) ? 2u : 0u;
    }
    static constexpr uint16_t kMaxHostContentionSlots = 0xFFFFu;
    static constexpr unsigned sharedBusIndex(SharedBus bus) {
        return static_cast<unsigned>(bus);
    }
    void queueHostCpuContention(SharedBus bus) {
        if (!cpu_running_ || (ccnt_ & 0x60u) != 0 || dmaBlocksCpu()) return;
        uint16_t& slots = host_contention_slots_[sharedBusIndex(bus)];
        if (slots < kMaxHostContentionSlots) ++slots;
    }
    uint8_t applyHostCpuContention(SharedBus bus, uint8_t nominalClocks) {
        uint16_t& slots = host_contention_slots_[sharedBusIndex(bus)];
        if (slots == 0) return nominalClocks;
        --slots;
        return static_cast<uint8_t>(nominalClocks + 4u);
    }
    void chargeHostDmaContention(SharedBus bus) {
        if (!dma_active_ || (dcnt_ & 0x80u) == 0 || (dcnt_ & 0x20u) != 0) return;
        const unsigned source = dcnt_ & 0x03u;
        const bool destBwram = (dcnt_ & 0x04u) != 0;
        uint32_t penalty = 0;
        if (source == 0x00u && !destBwram) {
            if (bus == SharedBus::Rom) penalty = 2;
            else if (bus == SharedBus::Iram) penalty = 4;
        } else if (source == 0x00u && destBwram) {
            if (bus == SharedBus::Bwram) penalty = 4;
        } else if (source == 0x01u && !destBwram) {
            if (bus == SharedBus::Bwram) penalty = 4;
            else if (bus == SharedBus::Iram) penalty = 2;
        } else if (source == 0x02u && destBwram) {
            if (bus == SharedBus::Iram) penalty = 4;
            else if (bus == SharedBus::Bwram) penalty = 2;
        }
        dma_clock_credit_ -= static_cast<int64_t>(penalty);
    }
    static bool isSystemBank(uint8_t bank) {
        return (bank & 0x40u) == 0;
    }
    static bool isPowerOfTwo(std::size_t size) {
        return size != 0 && (size & (size - 1)) == 0;
    }
    static std::size_t highestPowerOfTwo(std::size_t size) {
        if (size == 0) return 0;
        std::size_t mask = 1;
        while ((mask << 1) <= size) mask <<= 1;
        return mask;
    }
    uint8_t readRom(uint32_t offset) const {
        if (rom_.empty()) return 0xFF;
        if (rom_power2_mask_ != 0) return rom_[static_cast<std::size_t>(offset) & rom_power2_mask_];
        return rom_[mirrorAddress(offset, rom_.size(), rom_mirror_mask_)];
    }
    std::size_t bwramIndex(uint32_t offset) const {
        if (bwram_power2_mask_ != 0) return static_cast<std::size_t>(offset) & bwram_power2_mask_;
        return static_cast<std::size_t>(offset) % bwram_.size();
    }
    uint8_t readBwram(uint32_t offset) const {
        if (bwram_.empty()) return 0xFF;
        return bwram_[bwramIndex(offset)];
    }
    void writeBwram(uint32_t offset, uint8_t value, bool fromCcpu) {
        if (bwram_.empty()) return;
        const std::size_t i = bwramIndex(offset);
        (void)fromCcpu;
        if (((sbwe_ | cbwe_) & 0x80) == 0) {
            const unsigned area = bwpa_ & 0x0F;
            const std::size_t protectedBytes =
                area >= 10 ? std::size_t(0x40000) : (std::size_t(0x100) << area);
            const std::size_t protectionAddress = static_cast<std::size_t>(offset) & 0x3FFFFu;
            if (protectionAddress < protectedBytes) return;
        }
        if (bwram_[i] != value) {
            bwram_[i] = value;
            bwram_dirty_ = true;
        }
    }
    void writeIram(uint16_t offset, uint8_t value, bool fromCcpu) {
        const uint8_t block = static_cast<uint8_t>((offset >> 8) & 7);
        const uint8_t protect = fromCcpu ? ciwp_ : siwp_;
        if ((protect & (1u << block)) == 0) return;
        iram_[offset & 0x07FF] = value;
    }
    static std::size_t mirrorAddress(std::size_t address, std::size_t size, std::size_t mask) {
        if (size == 0) return 0;
        while (address >= size) {
            if (address & mask) address -= mask;
            else mask >>= 1;
            if (mask == 0) return address % size;
        }
        return address;
    }
    void rebuildRomMapCache() {
        for (unsigned b = 0; b < 256; ++b) {
            const uint8_t bank = static_cast<uint8_t>(b);
            unsigned slot = 0;
            unsigned bankWithin = 0;
            bool hiLayout = false;
            if (bank >= 0xC0) {
                slot = (bank - 0xC0) >> 4;
                bankWithin = bank & 0x0F;
                hiLayout = true;
            } else if (bank <= 0x1F) {
                slot = 0; bankWithin = bank & 0x1F;
            } else if (bank <= 0x3F) {
                slot = 1; bankWithin = bank & 0x1F;
            } else if (bank >= 0x80 && bank <= 0x9F) {
                slot = 2; bankWithin = bank & 0x1F;
            } else {
                slot = 3; bankWithin = bank & 0x1F;
            }
            const uint8_t reg = mmc_[slot & 3];
            unsigned megabyte = reg & 0x07;
            if (!hiLayout && (reg & 0x80) == 0) megabyte = slot;
            rom_bank_base_[bank] = static_cast<uint32_t>(megabyte * 0x100000u +
                bankWithin * (hiLayout ? 0x10000u : 0x8000u));
            rom_addr_mask_[bank] = static_cast<uint16_t>(hiLayout ? 0xFFFFu : 0x7FFFu);
        }
    }
    uint32_t mapRom(uint8_t bank, uint16_t addr) const {
        return rom_bank_base_[bank] + (addr & rom_addr_mask_[bank]);
    }
    uint32_t bwramMirrorAddress(uint8_t map, uint16_t addr) const {
        return static_cast<uint32_t>((map & 0x1F) * 0x2000u + (addr & 0x1FFFu));
    }
    uint8_t readCcpuBwramWindow(uint16_t addr) const {
        if ((bmap_ & 0x80u) == 0)
            return readBwram(bwramMirrorAddress(bmap_, addr));
        const uint32_t linear = static_cast<uint32_t>(bmap_ & 0x7Fu) * 0x2000u +
                                static_cast<uint32_t>(addr & 0x1FFFu);
        const uint8_t bank = static_cast<uint8_t>(0x60u + ((linear >> 16) & 0x0Fu));
        return readBitmap(bank, static_cast<uint16_t>(linear));
    }
    void writeCcpuBwramWindow(uint16_t addr, uint8_t value) {
        if ((bmap_ & 0x80u) == 0) {
            writeBwram(bwramMirrorAddress(bmap_, addr), value, true);
            return;
        }
        const uint32_t linear = static_cast<uint32_t>(bmap_ & 0x7Fu) * 0x2000u +
                                static_cast<uint32_t>(addr & 0x1FFFu);
        const uint8_t bank = static_cast<uint8_t>(0x60u + ((linear >> 16) & 0x0Fu));
        writeBitmap(bank, static_cast<uint16_t>(linear), value);
    }
    uint8_t sfr() const {
        return static_cast<uint8_t>((host_irq_pending_ ? 0x80 : 0) |
                                    (host_chdma_pending_ ? 0x20 : 0) |
                                    (scnt_ & 0x50) | (scnt_ & 0x0F));
    }
    uint8_t cfr() const {
        return static_cast<uint8_t>((sa1_irq_pending_ ? 0x80 : 0) |
                                    (timer_irq_pending_ ? 0x40 : 0) |
                                    (dma_irq_pending_ ? 0x20 : 0) |
                                    (sa1_nmi_pending_ ? 0x10 : 0) | (ccnt_ & 0x0F));
    }
    void resetCcpuDomain() {
        cie_ = cic_ = 0;
        bmap_ = 0;
        cbwe_ = 0;
        ciwp_ = 0;
        dcnt_ = cdma_ = bbf_ = 0;
        sda_ = dda_ = 0;
        dtc_ = 0;
        mcnt_ = 0;
        ma_ = mb_ = 0;
        math_result_ = 0;
        math_overflow_ = false;
        math_pending_ = false;
        math_pending_result_ = 0;
        math_pending_overflow_ = false;
        math_clocks_remaining_ = 0;
        tmc_ = 0;
        hcnt_target_ = vcnt_target_ = 0;
        restartTimer();
        sa1_irq_pending_ = false;
        sa1_nmi_pending_ = false;
        sa1_nmi_signaled_ = false;
        timer_irq_pending_ = false;
        dma_irq_pending_ = false;
        vbd_ = 0; vda_ = 0; variable_bit_pos_ = 0; variable_data_ = 0;
        brf_.fill(0);
        cc2_pixels_.fill(0);
        cc2_group_ = 0;
        cc2_line_ = 0;
        dma_active_ = false;
        cc1_active_ = false;
        dma_clock_credit_ = 0;
        dma_remaining_ = 0;
        cpu_.setIRQLine(false);
    }
    void startCcpu() {
        CPU::State s{};
        s.pc = crv_;
        s.pb = 0;
        s.s = 0x01FF;
        s.p = Flag_I | Flag_X | Flag_M;
        s.e = true;
        cpu_.setStateForTesting(s);
        cpu_running_ = true;
        clock_credit_ = 0;
    }
    void updateCcpuInterrupts() {
        const bool irq = ((cie_ & 0x80) && sa1_irq_pending_) ||
                         ((cie_ & 0x40) && timer_irq_pending_) ||
                         ((cie_ & 0x20) && dma_irq_pending_);
        cpu_.setIRQLine(irq);
        if ((cie_ & 0x10) && sa1_nmi_pending_) {
            if (!sa1_nmi_signaled_) {
                cpu_.requestNMI();
                sa1_nmi_signaled_ = true;
            }
        } else if (!sa1_nmi_pending_) {
            sa1_nmi_signaled_ = false;
        }
    }
    void restartTimer() {
        timer_dot_remainder_ = 0;
        timer_deferred_clocks_ = 0;
        timer_h_ = timer_v_ = 0;
    }
    void advanceTimer(uint64_t masterClocks) {
        timer_dot_remainder_ += masterClocks;
        const uint64_t dots = timer_dot_remainder_ >> 2;
        timer_dot_remainder_ &= 3;
        if (dots == 0) return;
        const uint32_t hPeriod = (tmc_ & 0x80) ? 512u : 341u;
        const uint32_t vPeriod = (tmc_ & 0x80) ? 512u : (pal_region_ ? 312u : 262u);
        const uint64_t period = static_cast<uint64_t>(hPeriod) * vPeriod;
        const uint64_t start = static_cast<uint64_t>(timer_v_) * hPeriod + timer_h_;
        if (!timer_irq_pending_ && (tmc_ & 0x03) != 0) {
            const bool hEnabled = (tmc_ & 0x01) != 0;
            const bool vEnabled = (tmc_ & 0x02) != 0;
            const uint32_t hTarget = hcnt_target_ & 0x1FFu;
            const uint32_t vTarget = vcnt_target_ & 0x1FFu;
            bool matchReached = false;
            if (hEnabled && vEnabled) {
                if (hTarget < hPeriod && vTarget < vPeriod) {
                    const uint64_t target = static_cast<uint64_t>(vTarget) * hPeriod + hTarget;
                    uint64_t distance = (target + period - start) % period;
                    if (distance == 0) distance = period;
                    matchReached = distance <= dots;
                }
            } else if (hEnabled) {
                if (hTarget < hPeriod) {
                    uint64_t distance = (hTarget + hPeriod - timer_h_) % hPeriod;
                    if (distance == 0) distance = hPeriod;
                    matchReached = distance <= dots;
                }
            } else if (vEnabled && vTarget < vPeriod) {
                const uint64_t blockStart = static_cast<uint64_t>(vTarget) * hPeriod;
                const uint64_t blockEnd = blockStart + hPeriod - 1;
                if (start >= blockStart && start < blockEnd) {
                    matchReached = true;
                } else {
                    uint64_t distance = (blockStart + period - start) % period;
                    if (distance == 0) distance = period;
                    matchReached = distance <= dots;
                }
            }
            if (matchReached) {
                timer_irq_pending_ = true;
                updateCcpuInterrupts();
            }
        }
        const uint64_t end = (start + (dots % period)) % period;
        timer_v_ = static_cast<uint16_t>(end / hPeriod);
        timer_h_ = static_cast<uint16_t>(end % hPeriod);
    }
    void materializeTimer() {
        if (timer_deferred_clocks_ == 0) return;
        const uint64_t clocks = timer_deferred_clocks_;
        timer_deferred_clocks_ = 0;
        advanceTimer(clocks);
    }
    void clockTimer(uint64_t masterClocks) {
        if ((tmc_ & 0x03) == 0) {
            timer_deferred_clocks_ += masterClocks;
            return;
        }
        if (timer_deferred_clocks_ != 0) materializeTimer();
        advanceTimer(masterClocks);
    }
    uint32_t mapVariableRom(uint32_t address) const {
        address &= 0xFFFFFEu;
        const uint8_t bank = static_cast<uint8_t>(address >> 16);
        const uint16_t addr = static_cast<uint16_t>(address);
        if (bank >= 0x40u && bank <= 0x7Fu)
            return mapRom(0x00u, static_cast<uint16_t>(0x8000u | (addr & 0x7FFFu)));
        if (isSystemBank(bank))
            return mapRom(bank, static_cast<uint16_t>(0x8000u | (addr & 0x7FFFu)));
        return mapRom(bank, addr);
    }
    uint16_t readVariableWord(uint32_t address) const {
        const uint32_t mapped = mapVariableRom(address);
        return static_cast<uint16_t>(readRom(mapped) |
                                     (uint16_t(readRom((mapped + 1u) & 0xFFFFFFu)) << 8));
    }
    uint16_t currentVariableData() const {
        const uint32_t data = uint32_t(readVariableWord(vda_)) |
                              (uint32_t(readVariableWord((vda_ + 2u) & 0xFFFFFFu)) << 16);
        return static_cast<uint16_t>(data >> variable_bit_pos_);
    }
    void advanceVariableBits(unsigned shift) {
        if (shift == 0) shift = 16;
        unsigned bit = unsigned(variable_bit_pos_) + shift;
        if (bit >= 16u) {
            vda_ = (vda_ + ((bit >> 4) << 1)) & 0xFFFFFFu;
            bit &= 15u;
        }
        variable_bit_pos_ = static_cast<uint8_t>(bit);
        variable_data_ = currentVariableData();
    }
    void writeScpuReg(uint16_t addr, uint8_t value) {
        switch (addr) {
        case 0x2200: {
            const bool wasReset = (ccnt_ & 0x20) != 0;
            if (value & 0x80u) sa1_irq_pending_ = true;
            if (value & 0x10u) { sa1_nmi_pending_ = true; sa1_nmi_signaled_ = false; }
            ccnt_ = value;
            const bool nowReset = (ccnt_ & 0x20) != 0;
            if (nowReset) {
                if (!wasReset) resetCcpuDomain();
                cpu_running_ = false;
            } else if (wasReset) {
                startCcpu();
            }
            updateCcpuInterrupts();
            break;
        }
        case 0x2201: sie_ = value; break;
        case 0x2202:
            sic_ = value;
            if (value & 0x80) host_irq_pending_ = false;
            if (value & 0x20) host_chdma_pending_ = false;
            break;
        case 0x2203: crv_ = static_cast<uint16_t>((crv_ & 0xFF00) | value); break;
        case 0x2204: crv_ = static_cast<uint16_t>((crv_ & 0x00FF) | (uint16_t(value) << 8)); break;
        case 0x2205: cnv_ = static_cast<uint16_t>((cnv_ & 0xFF00) | value); break;
        case 0x2206: cnv_ = static_cast<uint16_t>((cnv_ & 0x00FF) | (uint16_t(value) << 8)); break;
        case 0x2207: civ_ = static_cast<uint16_t>((civ_ & 0xFF00) | value); break;
        case 0x2208: civ_ = static_cast<uint16_t>((civ_ & 0x00FF) | (uint16_t(value) << 8)); break;
        case 0x2220: case 0x2221: case 0x2222: case 0x2223:
            mmc_[addr - 0x2220] = value;
            rebuildRomMapCache();
            break;
        case 0x2224: bmaps_ = value; break;
        case 0x2226: sbwe_ = value; break;
        case 0x2228: bwpa_ = value; bwpa_written_ = true; break;
        case 0x2229: siwp_ = value; break;
        case 0x2231:
            cdma_ = value;
            if (value & 0x80) cc1_active_ = false;
            break;
        case 0x2232: sda_ = (sda_ & 0xFFFF00u) | value; break;
        case 0x2233: sda_ = (sda_ & 0xFF00FFu) | (uint32_t(value) << 8); break;
        case 0x2234: sda_ = (sda_ & 0x00FFFFu) | (uint32_t(value) << 16); break;
        case 0x2235: dda_ = (dda_ & 0xFFFF00u) | value; break;
        case 0x2236: dda_ = (dda_ & 0xFF00FFu) | (uint32_t(value) << 8); break;
        case 0x2237: dda_ = (dda_ & 0x00FFFFu) | (uint32_t(value) << 16); break;
        default: break;
        }
    }
    uint8_t ccpuRomReadClocks(uint32_t mappedOffset) {
        mappedOffset &= 0xFFFFFFu;
        uint8_t clocks = 2;
        if (rom_pair_valid_ && mappedOffset == rom_pair_next_) {
            rom_pair_valid_ = false;
            return clocks;
        }
        if (rom_pair_valid_) {
            clocks = static_cast<uint8_t>(clocks + 2u);
            rom_pair_valid_ = false;
        }
        if ((mappedOffset & 1u) == 0) {
            rom_pair_valid_ = true;
            rom_pair_next_ = (mappedOffset + 1u) & 0xFFFFFFu;
        } else {
            clocks = static_cast<uint8_t>(clocks + 2u);
        }
        return clocks;
    }
    void ageBwramWriteBuffer(uint8_t elapsedClocks) {
        if (elapsedClocks >= bwram_write_buffer_clocks_) bwram_write_buffer_clocks_ = 0;
        else bwram_write_buffer_clocks_ = static_cast<uint8_t>(bwram_write_buffer_clocks_ - elapsedClocks);
    }
    uint8_t ccpuBwramWriteClocks() {
        const uint8_t wait = bwram_write_buffer_clocks_;
        const uint8_t clocks = static_cast<uint8_t>(2u + wait);
        bwram_write_buffer_clocks_ = 2;
        return clocks;
    }
    uint8_t readCcpu(uint32_t address, uint8_t* accessClocks = nullptr) {
        address &= 0xFFFFFFu;
        const uint8_t bank = static_cast<uint8_t>(address >> 16);
        const uint16_t addr = static_cast<uint16_t>(address);
        const bool systemBank = isSystemBank(bank);
        if (accessClocks) {
            const bool romAccess = bank >= 0xC0 || (systemBank && addr >= 0x8000);
            const bool bwramAccess = (bank >= 0x40 && bank <= 0x7F) ||
                                     (systemBank && addr >= 0x6000 && addr <= 0x7FFF);
            uint8_t clocks = bwramAccess ? 4 : 2;
            if (romAccess) {
                clocks = ccpuRomReadClocks(mapRom(bank, addr));
                clocks = applyHostCpuContention(SharedBus::Rom, clocks);
            } else if (systemBank && (addr < 0x0800 || (addr >= 0x3000 && addr <= 0x37FF))) {
                clocks = applyHostCpuContention(SharedBus::Iram, clocks);
            } else if (bwramAccess) {
                clocks = applyHostCpuContention(SharedBus::Bwram, clocks);
            }
            ageBwramWriteBuffer(clocks);
            *accessClocks = clocks;
        }
        if (bank == 0x00) {
            uint8_t vectorValue = 0;
            bool vector = true;
            if (addr == 0xFFEA || addr == 0xFFFA) vectorValue = static_cast<uint8_t>(cnv_);
            else if (addr == 0xFFEB || addr == 0xFFFB) vectorValue = static_cast<uint8_t>(cnv_ >> 8);
            else if (addr == 0xFFEE || addr == 0xFFFE) vectorValue = static_cast<uint8_t>(civ_);
            else if (addr == 0xFFEF || addr == 0xFFFF) vectorValue = static_cast<uint8_t>(civ_ >> 8);
            else vector = false;
            if (vector) {
                ccpu_open_bus_ = vectorValue;
                return vectorValue;
            }
        }
        uint8_t value = ccpu_open_bus_;
        if (bank >= 0xC0 || (systemBank && addr >= 0x8000)) {
            value = readRom(mapRom(bank, addr));
        } else if (systemBank && addr < 0x0800) {
            value = iram_[addr];
        } else if (systemBank && addr >= 0x3000 && addr <= 0x37FF) {
            value = iram_[addr & 0x07FF];
        } else if (systemBank) {
            if (addr >= 0x2200 && addr <= 0x23FF) value = readCcpuReg(addr);
            else if (addr >= 0x6000 && addr <= 0x7FFF) value = readCcpuBwramWindow(addr);
        } else if (bank >= 0x40 && bank <= 0x5F) {
            value = readBwram(((static_cast<uint32_t>(bank) - 0x40u) << 16) | addr);
        } else if (bank >= 0x60 && bank <= 0x7F) {
            value = readBitmap(bank, addr);
        }
        ccpu_open_bus_ = value;
        return value;
    }
    void writeCcpu(uint32_t address, uint8_t value, uint8_t* accessClocks = nullptr) {
        address &= 0xFFFFFFu;
        ccpu_open_bus_ = value;
        const uint8_t bank = static_cast<uint8_t>(address >> 16);
        const uint16_t addr = static_cast<uint16_t>(address);
        if (accessClocks) {
            const bool systemBank = isSystemBank(bank);
            const bool bwramAccess = (bank >= 0x40 && bank <= 0x7F) ||
                                     (systemBank && addr >= 0x6000 && addr <= 0x7FFF);
            uint8_t clocks = bwramAccess ? ccpuBwramWriteClocks() : 2;
            if (systemBank && (addr < 0x0800 || (addr >= 0x3000 && addr <= 0x37FF)))
                clocks = applyHostCpuContention(SharedBus::Iram, clocks);
            else if (bwramAccess)
                clocks = applyHostCpuContention(SharedBus::Bwram, clocks);
            if (!bwramAccess) ageBwramWriteBuffer(clocks);
            *accessClocks = clocks;
        }
        if (isSystemBank(bank)) {
            if (addr < 0x0800) writeIram(addr, value, true);
            else if (addr >= 0x2200 && addr <= 0x22FF) writeCcpuReg(addr, value);
            else if (addr >= 0x3000 && addr <= 0x37FF) writeIram(addr & 0x07FF, value, true);
            else if (addr >= 0x6000 && addr <= 0x7FFF) writeCcpuBwramWindow(addr, value);
        } else if (bank >= 0x40 && bank <= 0x5F) {
            writeBwram(((static_cast<uint32_t>(bank) - 0x40u) << 16) | addr, value, true);
        } else if (bank >= 0x60 && bank <= 0x7F) {
            writeBitmap(bank, addr, value);
        }
    }
    uint8_t accessCcpu(uint32_t address) const {
        const uint8_t bank = static_cast<uint8_t>(address >> 16);
        const uint16_t addr = static_cast<uint16_t>(address);
        if ((isSystemBank(bank) && (addr < 0x0800 || (addr >= 0x3000 && addr <= 0x37FF))) ||
            (isSystemBank(bank) && addr >= 0x2200 && addr <= 0x23FF)) return 2;
        if ((bank >= 0x40 && bank <= 0x7F) || (isSystemBank(bank) && addr >= 0x6000 && addr <= 0x7FFF)) return 4;
        return 2;
    }
    uint8_t readCcpuReg(uint16_t addr) {
        if (addr < 0x2300) return ccpu_open_bus_;
        if (addr >= 0x2302 && addr <= 0x2305) materializeTimer();
        switch (addr) {
        case 0x2301: return cfr();
        case 0x2302: return static_cast<uint8_t>(timer_h_);
        case 0x2303: return static_cast<uint8_t>((timer_h_ >> 8) & 1);
        case 0x2304: return static_cast<uint8_t>(timer_v_);
        case 0x2305: return static_cast<uint8_t>((timer_v_ >> 8) & 1);
        case 0x2306: case 0x2307: case 0x2308: case 0x2309: case 0x230A:
            return static_cast<uint8_t>((static_cast<uint64_t>(math_result_) >> ((addr - 0x2306) * 8)) & 0xFF);
        case 0x230B: return math_overflow_ ? 0x80 : 0x00;
        case 0x230C:
            variable_data_ = currentVariableData();
            return static_cast<uint8_t>(variable_data_);
        case 0x230D: {
            variable_data_ = currentVariableData();
            const uint8_t value = static_cast<uint8_t>(variable_data_ >> 8);
            if (vbd_ & 0x80u) advanceVariableBits(vbd_ & 0x0Fu);
            return value;
        }
        default: return ccpu_open_bus_;
        }
    }
    void writeCcpuReg(uint16_t addr, uint8_t value) {
        switch (addr) {
        case 0x2209:
            scnt_ = value;
            if (value & 0x80) host_irq_pending_ = true;
            break;
        case 0x220A: cie_ = value; updateCcpuInterrupts(); break;
        case 0x220B:
            cic_ = value;
            if (value & 0x80) sa1_irq_pending_ = false;
            if (value & 0x40) timer_irq_pending_ = false;
            if (value & 0x20) dma_irq_pending_ = false;
            if (value & 0x10) { sa1_nmi_pending_ = false; sa1_nmi_signaled_ = false; }
            updateCcpuInterrupts();
            break;
        case 0x220C: snv_ = static_cast<uint16_t>((snv_ & 0xFF00) | value); break;
        case 0x220D: snv_ = static_cast<uint16_t>((snv_ & 0x00FF) | (uint16_t(value) << 8)); break;
        case 0x220E: siv_ = static_cast<uint16_t>((siv_ & 0xFF00) | value); break;
        case 0x220F: siv_ = static_cast<uint16_t>((siv_ & 0x00FF) | (uint16_t(value) << 8)); break;
        case 0x2210:
            materializeTimer();
            tmc_ = value & 0x83;
            break;
        case 0x2211: restartTimer(); break;
        case 0x2212: hcnt_target_ = static_cast<uint16_t>((hcnt_target_ & 0x100) | value); break;
        case 0x2213: hcnt_target_ = static_cast<uint16_t>((hcnt_target_ & 0x0FF) | ((value & 1) << 8)); break;
        case 0x2214: vcnt_target_ = static_cast<uint16_t>((vcnt_target_ & 0x100) | value); break;
        case 0x2215: vcnt_target_ = static_cast<uint16_t>((vcnt_target_ & 0x0FF) | ((value & 1) << 8)); break;
        case 0x2225: bmap_ = value; break;
        case 0x2227: cbwe_ = value; break;
        case 0x222A: ciwp_ = value; break;
        case 0x2230:
            dcnt_ = value;
            if ((dcnt_ & 0x80u) == 0) cc2_line_ = 0;
            break;
        case 0x2231:
            cdma_ = value;
            if (value & 0x80) cc1_active_ = false;
            break;
        case 0x2232: sda_ = (sda_ & 0xFFFF00u) | value; break;
        case 0x2233: sda_ = (sda_ & 0xFF00FFu) | (uint32_t(value) << 8); break;
        case 0x2234: sda_ = (sda_ & 0x00FFFFu) | (uint32_t(value) << 16); break;
        case 0x2235: dda_ = (dda_ & 0xFFFF00u) | value; break;
        case 0x2236: dda_ = (dda_ & 0xFF00FFu) | (uint32_t(value) << 8); maybeStartDma(false); break;
        case 0x2237: dda_ = (dda_ & 0x00FFFFu) | (uint32_t(value) << 16); maybeStartDma(true); break;
        case 0x2238: dtc_ = static_cast<uint16_t>((dtc_ & 0xFF00) | value); break;
        case 0x2239: dtc_ = static_cast<uint16_t>((dtc_ & 0x00FF) | (uint16_t(value) << 8)); break;
        case 0x223F: bbf_ = value; break;
        case 0x2240: case 0x2241: case 0x2242: case 0x2243:
        case 0x2244: case 0x2245: case 0x2246: case 0x2247:
        case 0x2248: case 0x2249: case 0x224A: case 0x224B:
        case 0x224C: case 0x224D: case 0x224E: case 0x224F:
            brf_[addr - 0x2240] = value;
            if ((addr == 0x2247 || addr == 0x224F) && (dcnt_ & 0xB0) == 0xA0) {
                runCharacterConversion2();
            }
            break;
        case 0x2250:
            mcnt_ = value & 0x03;
            if ((mcnt_ & 2) != 0) {
                math_result_ = 0;
                math_pending_ = false; math_clocks_remaining_ = 0;
            }
            break;
        case 0x2251: ma_ = static_cast<uint16_t>((ma_ & 0xFF00) | value); break;
        case 0x2252: ma_ = static_cast<uint16_t>((ma_ & 0x00FF) | (uint16_t(value) << 8)); break;
        case 0x2253: mb_ = static_cast<uint16_t>((mb_ & 0xFF00) | value); break;
        case 0x2254:
            mb_ = static_cast<uint16_t>((mb_ & 0x00FF) | (uint16_t(value) << 8));
            scheduleArithmetic();
            if ((mcnt_ & 0x02) == 0 && (mcnt_ & 0x01) != 0) ma_ = 0;
            mb_ = 0;
            break;
        case 0x2258:
            vbd_ = value;
            if ((vbd_ & 0x80u) == 0) advanceVariableBits(vbd_ & 0x0Fu);
            else variable_data_ = currentVariableData();
            break;
        case 0x2259:
            vda_ = (vda_ & 0xFFFF00u) | value;
            vbd_ = 0;
            variable_bit_pos_ = 0;
            variable_data_ = currentVariableData();
            break;
        case 0x225A:
            vda_ = (vda_ & 0xFF00FFu) | (uint32_t(value) << 8);
            vbd_ = 0;
            variable_bit_pos_ = 0;
            variable_data_ = currentVariableData();
            break;
        case 0x225B:
            vda_ = (vda_ & 0x00FFFFu) | (uint32_t(value) << 16);
            vbd_ = 0;
            variable_bit_pos_ = 0;
            variable_data_ = currentVariableData();
            break;
        default: break;
        }
    }
    void scheduleArithmetic() {
        const int16_t a = static_cast<int16_t>(ma_);
        int64_t result = math_result_;
        bool overflow = math_overflow_;
        if (mcnt_ & 0x02) {
            const int64_t add = static_cast<int64_t>(a) * static_cast<int16_t>(mb_);
            const uint64_t sum = (static_cast<uint64_t>(math_result_) & 0xFFFFFFFFFFull) + static_cast<uint64_t>(add);
            overflow = (sum >> 40) != 0;
            result = static_cast<int64_t>(sum & 0xFFFFFFFFFFull);
        } else if (mcnt_ & 0x01) {
            const uint16_t divisor = mb_;
            uint16_t q = 0, r = 0;
            if (divisor == 0) {
                q = a < 0 ? 0x0001u : 0xFFFFu;
                r = static_cast<uint16_t>(a < 0 ? -static_cast<int32_t>(a) : a);
            } else {
                const int32_t dividend = a;
                int32_t rem = dividend % static_cast<int32_t>(divisor);
                if (rem < 0) rem += divisor;
                const int32_t quot = (dividend - rem) / static_cast<int32_t>(divisor);
                q = static_cast<uint16_t>(quot);
                r = static_cast<uint16_t>(rem);
            }
            result = static_cast<uint32_t>(q) | (static_cast<uint32_t>(r) << 16);
        } else {
            result = static_cast<uint32_t>(static_cast<int32_t>(a) * static_cast<int16_t>(mb_));
        }
        math_pending_result_ = result;
        math_pending_overflow_ = overflow;
        math_pending_ = true;
        math_clocks_remaining_ = (mcnt_ & 0x02) ? 12 : 10;
    }
    void clockArithmetic(uint64_t masterClocks) {
        if (!math_pending_) return;
        if (masterClocks < static_cast<uint64_t>(math_clocks_remaining_)) {
            math_clocks_remaining_ -= static_cast<int32_t>(masterClocks);
            return;
        }
        math_clocks_remaining_ = 0;
        math_pending_ = false;
        math_result_ = math_pending_result_;
        math_overflow_ = math_pending_overflow_;
    }
    bool dmaBlocksCpu() const {
        if (!dma_active_) return false;
        if (dcnt_ & 0x40) return true;
        const unsigned source = dcnt_ & 0x03u;
        const bool destBwram = (dcnt_ & 0x04u) != 0;
        return !((source == 0x01u && !destBwram) || (source == 0x02u && destBwram));
    }
    uint8_t dmaReadSource(uint32_t address) const {
        switch (dcnt_ & 0x03) {
        case 0x00: {
            const uint8_t bank = static_cast<uint8_t>(address >> 16);
            return readRom(mapRom(bank, static_cast<uint16_t>(address)));
        }
        case 0x01: return readBwram(address);
        case 0x02: return iram_[address & 0x07FFu];
        default: return 0xFF;
        }
    }
    void dmaWriteDestination(uint32_t address, uint8_t value) {
        if (dcnt_ & 0x04) writeBwram(address, value, true);
        else iram_[address & 0x07FFu] = value;
    }
    uint32_t dmaByteMasterClocks(uint32_t src, uint32_t dst) const {
        (void)src; (void)dst;
        const unsigned source = dcnt_ & 0x03u;
        const bool destBwram = (dcnt_ & 0x04u) != 0;
        if (source == 0x00u && !destBwram) return 2u;
        return 4u;
    }
    void finishDma() {
        dma_active_ = false;
        sda_ = dma_src_ & 0xFFFFFFu;
        dda_ = dma_dst_ & 0xFFFFFFu;
        dtc_ = 0;
        dma_irq_pending_ = true;
        updateCcpuInterrupts();
    }
    void clockDma(uint64_t masterClocks) {
        if (!dma_active_) return;
        dma_clock_credit_ += static_cast<int64_t>(masterClocks);
        while (dma_active_ && dma_remaining_ != 0) {
            const uint32_t cost = dmaByteMasterClocks(dma_src_, dma_dst_);
            if (dma_clock_credit_ < static_cast<int64_t>(cost)) break;
            dma_clock_credit_ -= static_cast<int64_t>(cost);
            const uint8_t v = dmaReadSource(dma_src_);
            dmaWriteDestination(dma_dst_, v);
            dma_src_ = (dma_src_ + 1) & 0xFFFFFFu;
            dma_dst_ = (dma_dst_ + 1) & 0xFFFFFFu;
            --dma_remaining_;
            dtc_ = static_cast<uint16_t>(dma_remaining_ & 0xFFFFu);
            sda_ = dma_src_;
            dda_ = dma_dst_;
            if (dma_remaining_ == 0) finishDma();
        }
    }
    void maybeStartDma(bool highWrite) {
        if ((dcnt_ & 0x80) == 0) return;
        if ((dcnt_ & 0xB0) == 0xB0) {
            if (!highWrite) startCharacterConversion1();
            return;
        }
        if (dcnt_ & 0x20) return;
        const bool destBwram = (dcnt_ & 0x04) != 0;
        if ((destBwram && !highWrite) || (!destBwram && highWrite)) return;
        dma_src_ = sda_ & 0xFFFFFFu;
        dma_dst_ = dda_ & 0xFFFFFFu;
        dma_remaining_ = dtc_ ? dtc_ : 0x10000u;
        dma_clock_credit_ = 0;
        dma_irq_pending_ = false;
        dma_active_ = true;
        updateCcpuInterrupts();
    }
    unsigned characterDepth() const {
        switch (cdma_ & 3) {
        case 0: return 8;
        case 1: return 4;
        default: return 2;
        }
    }
    void writePlanarTile(const uint8_t* pixels, uint16_t base, unsigned depth) {
        for (unsigned y = 0; y < 8; ++y) {
            for (unsigned plane = 0; plane < depth; ++plane) {
                uint8_t out = 0;
                for (unsigned x = 0; x < 8; ++x)
                    out = static_cast<uint8_t>((out << 1) | ((pixels[y * 8 + x] >> plane) & 1));
                const unsigned pair = plane >> 1;
                const unsigned within = plane & 1;
                const uint16_t off = static_cast<uint16_t>(base + pair * 16 + y * 2 + within);
                iram_[off & 0x07FF] = out;
            }
        }
    }
    uint8_t readCharacterConversion1(uint32_t address) {
        const unsigned dmacb = std::min<unsigned>(cdma_ & 3u, 2u);
        const unsigned dmasize = std::min<unsigned>((cdma_ >> 2) & 7u, 5u);
        const uint32_t charmask = (1u << (6u - dmacb)) - 1u;
        if ((address & charmask) == 0) {
            const unsigned bpp = 2u << (2u - dmacb);
            const uint32_t bpl = (8u << dmasize) >> dmacb;
            const uint32_t bwSize = static_cast<uint32_t>(bwram_.size());
            if (bwSize != 0) {
                const uint32_t rel = static_cast<uint32_t>(bwramIndex(address - sda_));
                const uint32_t tile = rel >> (6u - dmacb);
                const uint32_t ty = tile >> dmasize;
                const uint32_t tx = tile & ((1u << dmasize) - 1u);
                uint32_t bwaddr = sda_ + ty * 8u * bpl + tx * bpp;
                for (unsigned y = 0; y < 8; ++y) {
                    uint64_t data = 0;
                    for (unsigned byte = 0; byte < bpp; ++byte)
                        data |= uint64_t(readBwram(bwaddr + byte)) << (byte * 8u);
                    bwaddr += bpl;
                    std::array<uint8_t, 8> out{};
                    for (unsigned x = 0; x < 8; ++x) {
                        out[0] |= static_cast<uint8_t>((data & 1u) << (7u - x)); data >>= 1;
                        out[1] |= static_cast<uint8_t>((data & 1u) << (7u - x)); data >>= 1;
                        if (dmacb == 2) continue;
                        out[2] |= static_cast<uint8_t>((data & 1u) << (7u - x)); data >>= 1;
                        out[3] |= static_cast<uint8_t>((data & 1u) << (7u - x)); data >>= 1;
                        if (dmacb == 1) continue;
                        out[4] |= static_cast<uint8_t>((data & 1u) << (7u - x)); data >>= 1;
                        out[5] |= static_cast<uint8_t>((data & 1u) << (7u - x)); data >>= 1;
                        out[6] |= static_cast<uint8_t>((data & 1u) << (7u - x)); data >>= 1;
                        out[7] |= static_cast<uint8_t>((data & 1u) << (7u - x)); data >>= 1;
                    }
                    for (unsigned byte = 0; byte < bpp; ++byte) {
                        const uint32_t p = dda_ + (y << 1) + ((byte & 6u) << 3) + (byte & 1u);
                        iram_[p & 0x07ffu] = out[byte];
                    }
                }
            }
        }
        return iram_[(dda_ + (address & charmask)) & 0x07ffu];
    }
    void startCharacterConversion1() {
        cc1_active_ = true;
        host_chdma_pending_ = true;
        updateCcpuInterrupts();
    }
    void runCharacterConversion2() {
        const unsigned dmacb = std::min<unsigned>(cdma_ & 3u, 2u);
        const unsigned bpp = 2u << (2u - dmacb);
        const uint8_t* row = brf_.data() + ((cc2_line_ & 1u) << 3);
        uint32_t address = dda_ & 0x07ffu;
        address &= ~((1u << (7u - dmacb)) - 1u);
        address += (cc2_line_ & 8u) * bpp;
        address += (cc2_line_ & 7u) * 2u;
        for (unsigned byte = 0; byte < bpp; ++byte) {
            uint8_t output = 0;
            for (unsigned bit = 0; bit < 8; ++bit)
                output |= static_cast<uint8_t>(((row[bit] >> byte) & 1u) << (7u - bit));
            iram_[(address + ((byte & 6u) << 3) + (byte & 1u)) & 0x07ffu] = output;
        }
        cc2_line_ = static_cast<uint8_t>((cc2_line_ + 1u) & 15u);
    }
    uint8_t readBitmap(uint8_t bank, uint16_t addr) const {
        if (bwram_.empty()) return 0xFF;
        const uint32_t linear = ((static_cast<uint32_t>(bank) - 0x60u) << 16) | addr;
        if (bbf_ & 0x80) {
            const uint8_t byte = readBwram(linear >> 2);
            return static_cast<uint8_t>((byte >> ((linear & 3) * 2)) & 3);
        }
        const uint8_t byte = readBwram(linear >> 1);
        return static_cast<uint8_t>((byte >> ((linear & 1) * 4)) & 0x0F);
    }
    void writeBitmap(uint8_t bank, uint16_t addr, uint8_t value) {
        if (bwram_.empty() || (cbwe_ & 0x80) == 0) return;
        const uint32_t linear = ((static_cast<uint32_t>(bank) - 0x60u) << 16) | addr;
        if (bbf_ & 0x80) {
            const std::size_t off = bwramIndex(linear >> 2);
            const unsigned shift = (linear & 3) * 2;
            const uint8_t nv = static_cast<uint8_t>((bwram_[off] & ~(3u << shift)) | ((value & 3u) << shift));
            if (bwram_[off] != nv) { bwram_[off] = nv; bwram_dirty_ = true; }
        } else {
            const std::size_t off = bwramIndex(linear >> 1);
            const unsigned shift = (linear & 1) * 4;
            const uint8_t nv = static_cast<uint8_t>((bwram_[off] & ~(0x0Fu << shift)) | ((value & 0x0Fu) << shift));
            if (bwram_[off] != nv) { bwram_[off] = nv; bwram_dirty_ = true; }
        }
    }
    const std::vector<uint8_t>& rom_;
    std::vector<uint8_t>& bwram_;
    bool& bwram_dirty_;
    bool pal_region_=false;
    const std::size_t rom_mirror_mask_;
    const std::size_t rom_power2_mask_;
    const std::size_t bwram_power2_mask_;
    CPU cpu_{};
    std::array<uint8_t, 0x800> iram_{};
    std::array<uint8_t, 4> mmc_{{0,1,2,3}};
    std::array<uint32_t, 256> rom_bank_base_{};
    std::array<uint16_t, 256> rom_addr_mask_{};
    uint8_t ccnt_=0x20, sie_=0, sic_=0, scnt_=0, cie_=0, cic_=0;
    uint16_t crv_=0, cnv_=0, civ_=0, snv_=0, siv_=0;
    uint8_t bmaps_=0, bmap_=0, sbwe_=0, cbwe_=0, bwpa_=0, siwp_=0, ciwp_=0;
    bool bwpa_written_=false;
    uint8_t dcnt_=0, cdma_=0, bbf_=0;
    uint32_t sda_=0, dda_=0;
    uint16_t dtc_=0;
    uint8_t mcnt_=0;
    uint16_t ma_=0, mb_=0;
    int64_t math_result_=0;
    bool math_overflow_=false;
    bool math_pending_=false;
    int64_t math_pending_result_=0;
    bool math_pending_overflow_=false;
    int32_t math_clocks_remaining_=0;
    bool cpu_running_=false;
    uint8_t host_open_bus_=0xFF;
    int64_t clock_credit_=0;
    uint8_t ccpu_open_bus_=0;
    std::array<uint16_t, 3> host_contention_slots_{};
    uint8_t host_fastrom_pause_clocks_=0;
    bool rom_pair_valid_=false;
    uint32_t rom_pair_next_=0;
    uint8_t bwram_write_buffer_clocks_=0;
    bool host_irq_pending_=false, host_chdma_pending_=false;
    uint64_t last_host_reg_sequence_=0;
    uint16_t last_host_reg_address_=0;
    uint8_t last_host_reg_value_=0;
    bool last_host_reg_write_=false;
    bool sa1_irq_pending_=false, sa1_nmi_pending_=false, sa1_nmi_signaled_=false;
    bool timer_irq_pending_=false, dma_irq_pending_=false;
    uint8_t tmc_=0;
    uint16_t hcnt_target_=0, vcnt_target_=0;
    uint64_t timer_dot_remainder_=0;
    uint64_t timer_deferred_clocks_=0;
    uint16_t timer_h_=0, timer_v_=0;
    uint8_t vbd_=0, variable_bit_pos_=0;
    uint32_t vda_=0;
    uint16_t variable_data_=0;
    std::array<uint8_t, 16> brf_{};
    std::array<uint8_t, 64> cc2_pixels_{};
    uint8_t cc2_group_=0;
    uint8_t cc2_line_=0;
    bool cc1_active_=false;
    bool dma_active_=false;
    int64_t dma_clock_credit_=0;
    uint32_t dma_src_=0, dma_dst_=0, dma_remaining_=0;
};
}
std::unique_ptr<CartridgeDevice> createSA1Device(
    const std::vector<uint8_t>& rom,
    std::vector<uint8_t>& bwram,
    bool& bwramDirty,
    bool palRegion) {
    return std::make_unique<SA1Device>(rom, bwram, bwramDirty, palRegion);
}

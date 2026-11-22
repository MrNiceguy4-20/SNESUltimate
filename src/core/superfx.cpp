#include "superfx.hpp"
#include "state_io.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
namespace {
constexpr uint16_t F_Z   = 0x0002;
constexpr uint16_t F_C   = 0x0004;
constexpr uint16_t F_S   = 0x0008;
constexpr uint16_t F_OV  = 0x0010;
constexpr uint16_t F_G   = 0x0020;
constexpr uint16_t F_R   = 0x0040;
constexpr uint16_t F_A1  = 0x0100;
constexpr uint16_t F_A2  = 0x0200;
constexpr uint16_t F_B   = 0x1000;
constexpr uint16_t F_IRQ = 0x8000;
constexpr auto kCommonOneByteOpcode = [] {
    std::array<uint8_t, 256> table{};
    for (unsigned op = 0; op < 256; ++op) {
        table[op] = static_cast<uint8_t>(
            op != 0x00 && op != 0x02 &&
            !(op >= 0x05 && op <= 0x0f) &&
            !(op >= 0x30 && op <= 0x3b) &&
            !(op >= 0x40 && op <= 0x4c) &&
            !(op >= 0x80 && op <= 0x8f) &&
            op != 0x90 && !(op >= 0x98 && op <= 0x9d) && op != 0x9f &&
            !(op >= 0xa0 && op <= 0xaf) &&
            op != 0xdf && op != 0xef && op < 0xf0);
    }
    return table;
}();
constexpr uint8_t BUS_RAM = 0x01;
constexpr uint8_t BUS_ROM = 0x02;
constexpr auto kBusRequirements = [] {
    std::array<std::array<uint8_t, 256>, 4> table{};
    for (unsigned a = 0; a < 4; ++a) {
        for (unsigned op = 0; op < 256; ++op) {
            uint8_t flags = 0;
            if ((op >= 0x30 && op <= 0x3b) ||
                (op >= 0x40 && op <= 0x4b) || op == 0x90 ||
                (op >= 0xa0 && op <= 0xaf && a != 0) ||
                (op >= 0xf0 && a != 0)) flags |= BUS_RAM;
            if (op == 0xef || (op == 0xdf && a < 2)) flags |= BUS_ROM;
            table[a][op] = flags;
        }
    }
    return table;
}();
class SuperFXDevice final : public CartridgeDevice {
public:
    SuperFXDevice(const std::vector<uint8_t>& rom, std::size_t ramBytes, GSURevision revision,
                  std::vector<uint8_t>* cartridgeRam, bool* cartridgeRamDirty)
        : rom_(rom), ram_(ramBytes ? ramBytes : 64 * 1024, 0), revision_(revision),
          cartridge_ram_(cartridgeRam), cartridge_ram_dirty_(cartridgeRamDirty) {
        if (cartridge_ram_ != nullptr) {
            cartridge_ram_->resize(ram_.size(), 0);
            ram_ = *cartridge_ram_;
        }
        rom_mask_ = powerOfTwoMask(rom_.size());
        ram_mask_ = powerOfTwoMask(ram_.size());
        reset();
    }
    void reset() override {
        regs_.fill(0);
        cache_.fill(0);
        cache_valid_lines_ = 0;
        sfr_ = 0;
        pbr_ = rombr_ = rambr_ = bramr_ = 0;
        cfgr_ = scbr_ = clsr_ = scmr_ = colr_ = por_ = 0;
        pixel_planes_ = 2;
        screen_height_mode_ = 0;
        pixel_tile_bytes_ = 16;
        screen_base_ = 0;
        cbr_ = 0;
        src_ = dst_ = 0;
        pipe_ = 0x01;
        pipe_fetch_cached_ = true;
        cache_fill_active_ = false;
        cache_fill_bank_ = 0;
        cache_fill_source_pc_ = 0;
        cache_fill_line_start_ = 0;
        cache_fill_next_byte_ = 0;
        cache_fill_cycle_credit_ = 0;
        rom_buffer_ = 0;
        register_latch_ = 0;
        rom_read_pending_ = false;
        rom_delay_ = 0;
        ram_write_pending_ = false;
        ram_delay_ = 0;
        ram_write_address_ = 0;
        ram_write_value_ = 0;
        ram_write_queued_ = false;
        ram_write_queued_address_ = 0;
        ram_write_queued_value_ = 0;
        primary_pixel_cache_ = {};
        secondary_pixel_cache_ = {};
        carry_ = false;
        sign_ = false;
        zero_ = false;
        overflow_ = false;
        last_ram_addr_ = 0;
        running_ = false;
        clock_credit_ = 0;
        clocks_until_step_ = 0;
        program_fetch_debt_cycles_ = 0;
        pixel_bus_start_delay_ = 0;
        pixel_bus_cycles_remaining_ = 0;
        pixel_bus_slice_remaining_ = 0;
        pixel_bus_ops_ = {};
        pixel_bus_head_ = 0;
        pixel_bus_count_ = 0;
        pixel_bus_merge_latch_ = 0;
        pixel_bus_rpix_result_ = 0;
        pixel_bus_rpix_result_valid_ = false;
        pixel_bus_waiting_for_ran_ = false;
        dma_registers_ = {};
    }
    bool implemented() const override { return true; }
    CartridgeSupportLevel supportLevel() const override {
        return CartridgeSupportLevel::Full;
    }
    uint32_t stateTag() const override{return 0x20555347u;}
    std::vector<uint8_t> saveState() const override {
        stateio::Writer w; uint32_t v = 10; w.pod(v);
        w.vector(ram_); w.array(regs_); w.array(cache_); w.pod(cache_valid_lines_);
        w.pod(sfr_); w.pod(cbr_); w.pod(last_ram_addr_); w.pod(pbr_); w.pod(rombr_); w.pod(rambr_);
        w.pod(bramr_); w.pod(cfgr_); w.pod(scbr_); w.pod(clsr_); w.pod(scmr_); w.pod(colr_); w.pod(por_);
        w.pod(pipe_); w.pod(src_); w.pod(dst_); w.pod(carry_); w.pod(sign_); w.pod(zero_); w.pod(overflow_);
        w.pod(running_); w.pod(revision_); w.pod(clock_credit_); w.pod(clocks_until_step_);
        w.pod(rom_buffer_); w.pod(register_latch_); w.pod(pipe_fetch_cached_);
        w.pod(rom_read_pending_); w.pod(rom_delay_);
        w.pod(ram_write_pending_); w.pod(ram_delay_); w.pod(ram_write_address_); w.pod(ram_write_value_);
        w.pod(ram_write_queued_); w.pod(ram_write_queued_address_); w.pod(ram_write_queued_value_);
        w.pod(primary_pixel_cache_); w.pod(secondary_pixel_cache_);
        w.pod(program_fetch_debt_cycles_);
        w.pod(cache_fill_active_); w.pod(cache_fill_bank_); w.pod(cache_fill_source_pc_);
        w.pod(cache_fill_line_start_); w.pod(cache_fill_next_byte_); w.pod(cache_fill_cycle_credit_);
        w.pod(pixel_bus_start_delay_); w.pod(pixel_bus_cycles_remaining_); w.pod(pixel_bus_slice_remaining_);
        w.array(pixel_bus_ops_); w.pod(pixel_bus_head_); w.pod(pixel_bus_count_);
        w.pod(pixel_bus_merge_latch_); w.pod(pixel_bus_rpix_result_); w.pod(pixel_bus_rpix_result_valid_);
        w.pod(pixel_bus_waiting_for_ran_);
        return w.take();
    }
    bool loadState(const std::vector<uint8_t>& b) override {
        const std::vector<uint8_t> backup = saveState();
        if (loadStateImpl(b)) return true;
        loadStateImpl(backup);
        return false;
    }
    bool loadStateImpl(const std::vector<uint8_t>& b) {
        stateio::Reader r(b); uint32_t v = 0; GSURevision savedRevision = revision_;
        if (!r.pod(v) || (v < 1 || v > 10) || !r.vector(ram_, 8*1024*1024) || !r.array(regs_) || !r.array(cache_)) return false;
        if (v >= 5) {
            if (!r.pod(cache_valid_lines_)) return false;
        } else {
            std::array<bool,512> legacyCacheWritten{};
            if (!r.array(legacyCacheWritten)) return false;
            cache_valid_lines_ = 0;
            for (unsigned line = 0; line < 32; ++line)
                if (legacyCacheWritten[line * 16 + 15]) cache_valid_lines_ |= (uint32_t(1) << line);
        }
        if (!r.pod(sfr_) || !r.pod(cbr_) || !r.pod(last_ram_addr_) ||
            !r.pod(pbr_) || !r.pod(rombr_) || !r.pod(rambr_) || !r.pod(bramr_) || !r.pod(cfgr_) || !r.pod(scbr_) ||
            !r.pod(clsr_) || !r.pod(scmr_) || !r.pod(colr_) || !r.pod(por_) || !r.pod(pipe_) || !r.pod(src_) ||
            !r.pod(dst_) || !r.pod(carry_) || !r.pod(sign_) || !r.pod(zero_) || !r.pod(overflow_) || !r.pod(running_) ||
            !r.pod(savedRevision) || !r.pod(clock_credit_) || !r.pod(clocks_until_step_)) return false;
        if (v >= 2) {
            if (!r.pod(rom_buffer_) || !r.pod(register_latch_)) return false;
            if (v >= 3) { if (!r.pod(pipe_fetch_cached_)) return false; }
            else pipe_fetch_cached_ = static_cast<uint16_t>(regs_[15] - cbr_) < 512 && cacheLineValid(static_cast<uint16_t>(regs_[15] - cbr_));
            if (!r.pod(rom_read_pending_) || !r.pod(rom_delay_) ||
                !r.pod(ram_write_pending_) || !r.pod(ram_delay_) || !r.pod(ram_write_address_) || !r.pod(ram_write_value_)) return false;
            if (v >= 9) {
                if (!r.pod(ram_write_queued_) || !r.pod(ram_write_queued_address_) || !r.pod(ram_write_queued_value_)) return false;
            } else {
                ram_write_queued_ = false;
                ram_write_queued_address_ = 0;
                ram_write_queued_value_ = 0;
            }
            if (!r.pod(primary_pixel_cache_) || !r.pod(secondary_pixel_cache_)) return false;
            if (v >= 6) {
                if (!r.pod(program_fetch_debt_cycles_)) return false;
                if (v >= 7 && (!r.pod(cache_fill_active_) || !r.pod(cache_fill_bank_) ||
                               !r.pod(cache_fill_source_pc_) || !r.pod(cache_fill_line_start_) ||
                               !r.pod(cache_fill_next_byte_) || !r.pod(cache_fill_cycle_credit_))) return false;
                if (v >= 8) {
                    if (!r.pod(pixel_bus_start_delay_) || !r.pod(pixel_bus_cycles_remaining_) ||
                        !r.pod(pixel_bus_slice_remaining_)) return false;
                } else {
                    pixel_bus_start_delay_ = 0;
                    pixel_bus_cycles_remaining_ = 0;
                    pixel_bus_slice_remaining_ = 0;
                }
                if (v >= 10) {
                    if (!r.array(pixel_bus_ops_) || !r.pod(pixel_bus_head_) || !r.pod(pixel_bus_count_) ||
                        !r.pod(pixel_bus_merge_latch_) || !r.pod(pixel_bus_rpix_result_) ||
                        !r.pod(pixel_bus_rpix_result_valid_) || !r.pod(pixel_bus_waiting_for_ran_)) return false;
                    if (pixel_bus_head_ > pixel_bus_ops_.size() ||
                        pixel_bus_count_ > pixel_bus_ops_.size() - pixel_bus_head_) return false;
                } else {
                    pixel_bus_ops_ = {};
                    pixel_bus_head_ = 0;
                    pixel_bus_count_ = 0;
                    pixel_bus_merge_latch_ = 0;
                    pixel_bus_rpix_result_ = 0;
                    pixel_bus_rpix_result_valid_ = false;
                    pixel_bus_waiting_for_ran_ = false;
                }
                if (v < 7) {
                    cache_fill_active_ = false;
                    cache_fill_bank_ = 0;
                    cache_fill_source_pc_ = 0;
                    cache_fill_line_start_ = 0;
                    cache_fill_next_byte_ = 0;
                    cache_fill_cycle_credit_ = 0;
                }
            } else if (v >= 4) {
                bool legacyFetchValid = false;
                uint8_t legacyFetchBank = 0;
                uint16_t legacyFetchPc = 0, legacyFetchCbr = 0;
                if (!r.pod(program_fetch_debt_cycles_) || !r.pod(legacyFetchValid) ||
                    !r.pod(legacyFetchBank) || !r.pod(legacyFetchPc) || !r.pod(legacyFetchCbr)) return false;
            } else {
                program_fetch_debt_cycles_ = 0;
                cache_fill_active_ = false;
                cache_fill_bank_ = 0;
                cache_fill_source_pc_ = 0;
                cache_fill_line_start_ = 0;
                cache_fill_next_byte_ = 0;
                cache_fill_cycle_credit_ = 0;
            }
        } else {
            rom_buffer_ = romRead(regs_[14]); register_latch_ = 0;
            rom_read_pending_ = false; rom_delay_ = 0; sfr_ &= ~F_R;
            ram_write_pending_ = false; ram_delay_ = 0; ram_write_address_ = 0; ram_write_value_ = 0;
            ram_write_queued_ = false; ram_write_queued_address_ = 0; ram_write_queued_value_ = 0;
            primary_pixel_cache_ = {}; secondary_pixel_cache_ = {};
            cache_fill_active_ = false;
            cache_fill_bank_ = 0;
            cache_fill_source_pc_ = 0;
            cache_fill_line_start_ = 0;
            cache_fill_next_byte_ = 0;
            cache_fill_cycle_credit_ = 0;
        }
        if (v < 8) {
            pixel_bus_start_delay_ = 0;
            pixel_bus_cycles_remaining_ = 0;
            pixel_bus_slice_remaining_ = 0;
        }
        if (v < 10) {
            pixel_bus_ops_ = {};
            pixel_bus_head_ = 0;
            pixel_bus_count_ = 0;
            pixel_bus_merge_latch_ = 0;
            pixel_bus_rpix_result_ = 0;
            pixel_bus_rpix_result_valid_ = false;
            pixel_bus_waiting_for_ran_ = false;
        }
        refreshPixelDerivedState();
        return r.done() && savedRevision == revision_ && src_ < 16 && dst_ < 16;
    }
    bool needsClock() const override { return true; }
    bool hostIrqPending() const override { return (sfr_ & F_IRQ) != 0 && (cfgr_ & 0x80u) == 0; }
    bool hostIrqMayChangeWithoutHostAccess() const override {
        return running_ && (cfgr_ & 0x80u) == 0;
    }
    bool hostAccessRequiresClock(uint32_t address, bool write) const override {
        (void)write;
        address &= 0xFFFFFFu;
        const uint8_t bank = static_cast<uint8_t>(address >> 16);
        const uint16_t a = static_cast<uint16_t>(address);
        if (systemBank(bank) && a >= 0x3000 && a <= 0x3FFF) return true;
        if (hostGamePakRam(address)) return true;
        if (hostGamePakRom(address)) return true;
        return false;
    }
    void dmaRegisterWrite(int channel, int offset, uint8_t value) override {
        if (channel < 0 || channel >= 8 || offset < 0 || offset >= 16) return;
        dma_registers_[static_cast<size_t>(channel)][static_cast<size_t>(offset)] = value;
    }
    bool clockAndHostIrq(uint64_t masterClocks) override {
        SuperFXDevice::clock(masterClocks);
        return (sfr_ & F_IRQ) != 0 && (cfgr_ & 0x80u) == 0;
    }
    void clock(uint64_t masterClocks) override {
        if (masterClocks == 0) return;
        if (!running_) {
            if (!ram_write_pending_ && pixel_bus_count_ == 0 && !pixel_bus_waiting_for_ran_)
                return;
            clock_credit_ += masterClocks;
            const uint64_t gsu_cycle_master_clocks = clsr_ ? 1u : 2u;
            while (clock_credit_ >= gsu_cycle_master_clocks &&
                   (ram_write_pending_ || pixel_bus_count_ != 0 || pixel_bus_waiting_for_ran_)) {
                if (servicePixelBusGrantWait(gsu_cycle_master_clocks)) {
                    if ((scmr_ & 0x08u) != 0) continue;
                    break;
                }
                if (servicePixelBusRamBufferWait(gsu_cycle_master_clocks)) continue;
                const uint64_t availableCycles = clock_credit_ / gsu_cycle_master_clocks;
                if (availableCycles == 0) break;
                uint64_t eventCycles = availableCycles;
                if (ram_write_pending_ && (scmr_ & 0x08) && ram_delay_)
                    eventCycles = std::min<uint64_t>(eventCycles, ram_delay_);
                if (pixel_bus_start_delay_)
                    eventCycles = std::min<uint64_t>(eventCycles, pixel_bus_start_delay_);
                else if (pixel_bus_slice_remaining_)
                    eventCycles = std::min<uint64_t>(eventCycles, pixel_bus_slice_remaining_);
                if (eventCycles == 0) eventCycles = 1;
                const uint64_t elapsedMaster = eventCycles * gsu_cycle_master_clocks;
                if (elapsedMaster > clock_credit_) break;
                clock_credit_ -= elapsedMaster;
                advancePendingOperations(static_cast<uint32_t>(eventCycles));
                bool pixelBoundary = false;
                advancePixelBusTimeline(static_cast<uint32_t>(eventCycles), pixelBoundary);
            }
            return;
        }
        clock_credit_ += masterClocks;
        const uint64_t gsu_cycle_master_clocks = clsr_ ? 1u : 2u;
        while (running_) {
            if (servicePixelBusGrantWait(gsu_cycle_master_clocks)) {
                if ((scmr_ & 0x08u) != 0) {
                    if (clocks_until_step_ == 0 && pixel_bus_count_ == 0) step();
                    return;
                }
                break;
            }
            if (servicePixelBusRamBufferWait(gsu_cycle_master_clocks)) return;
            if (!instructionBusGranted(pipe_)) {
                const uint64_t availableCycles = clock_credit_ / gsu_cycle_master_clocks;
                if (availableCycles == 0) break;
                const bool romCanAdvance = rom_read_pending_ && (scmr_ & 0x10) && rom_delay_;
                const bool ramCanAdvance = ram_write_pending_ && (scmr_ & 0x08) && ram_delay_;
                if (!romCanAdvance && !ramCanAdvance) {
                    clock_credit_ = 0;
                    break;
                }
                uint64_t eventCycles = availableCycles;
                if (romCanAdvance) eventCycles = std::min<uint64_t>(eventCycles, rom_delay_);
                if (ramCanAdvance) eventCycles = std::min<uint64_t>(eventCycles, ram_delay_);
                const uint64_t elapsedMaster = eventCycles * gsu_cycle_master_clocks;
                clock_credit_ -= elapsedMaster;
                advancePendingOperations(static_cast<uint32_t>(eventCycles));
                continue;
            }
            if (clocks_until_step_ == 0) {
                const uint32_t instruction = instructionCycles(pipe_);
                const uint32_t graphics = pipe_ == 0x4c ? graphicsMemoryCycles(pipe_, instruction) : 0u;
                const uint32_t gsuCycles = instruction + graphics;
                clocks_until_step_ = uint64_t(gsuCycles) * gsu_cycle_master_clocks;
            }
            if (!rom_read_pending_ && !ram_write_pending_) {
                const uint64_t usableMaster = clsr_ ? clock_credit_ : (clock_credit_ & ~uint64_t{1});
                if (usableMaster == 0) break;
                uint64_t elapsedMaster = std::min<uint64_t>(usableMaster, clocks_until_step_);
                if (pixel_bus_start_delay_)
                    elapsedMaster = std::min<uint64_t>(elapsedMaster, uint64_t(pixel_bus_start_delay_) * gsu_cycle_master_clocks);
                else if (pixel_bus_slice_remaining_)
                    elapsedMaster = std::min<uint64_t>(elapsedMaster, uint64_t(pixel_bus_slice_remaining_) * gsu_cycle_master_clocks);
                clock_credit_ -= elapsedMaster;
                clocks_until_step_ -= elapsedMaster;
                const uint32_t elapsedGsu = static_cast<uint32_t>(elapsedMaster / gsu_cycle_master_clocks);
                advanceCacheFill(elapsedGsu);
                bool pixelBoundary = false;
                advancePixelBusTimeline(elapsedGsu, pixelBoundary);
                if (clocks_until_step_ == 0 && !pixel_bus_waiting_for_ran_) {
                    step();
                    if (!running_ || ((sfr_ & F_IRQ) != 0 && (cfgr_ & 0x80u) == 0))
                        return;
                }
                (void)pixelBoundary;
                continue;
            }
            const uint64_t availableCycles = clock_credit_ / gsu_cycle_master_clocks;
            if (availableCycles == 0) break;
            uint64_t eventCycles = clocks_until_step_ / gsu_cycle_master_clocks;
            eventCycles = std::min(eventCycles, availableCycles);
            if (rom_read_pending_ && (scmr_ & 0x10) && rom_delay_) eventCycles = std::min<uint64_t>(eventCycles, rom_delay_);
            if (ram_write_pending_ && (scmr_ & 0x08) && ram_delay_) eventCycles = std::min<uint64_t>(eventCycles, ram_delay_);
            if (pixel_bus_start_delay_) eventCycles = std::min<uint64_t>(eventCycles, pixel_bus_start_delay_);
            else if (pixel_bus_slice_remaining_) eventCycles = std::min<uint64_t>(eventCycles, pixel_bus_slice_remaining_);
            if (eventCycles == 0) eventCycles = 1;
            const uint64_t elapsedMaster = eventCycles * gsu_cycle_master_clocks;
            clock_credit_ -= elapsedMaster;
            clocks_until_step_ -= elapsedMaster;
            advancePendingOperations(static_cast<uint32_t>(eventCycles));
            advanceCacheFill(static_cast<uint32_t>(eventCycles));
            bool pixelBoundary = false;
            advancePixelBusTimeline(static_cast<uint32_t>(eventCycles), pixelBoundary);
            if (clocks_until_step_ == 0 && !pixel_bus_waiting_for_ran_) {
                step();
                if (!running_ || ((sfr_ & F_IRQ) != 0 && (cfgr_ & 0x80u) == 0))
                    return;
            }
            (void)pixelBoundary;
        }
    }
    bool readHost(uint32_t address, uint8_t openBus, uint8_t& value) override {
        address &= 0xFFFFFFu;
        if (hostGamePakRam(address) && running_ && (scmr_ & 0x08)) {
            value = openBus;
            return true;
        }
        if (hostGamePakRom(address)) {
            if (running_ && (scmr_ & 0x10)) value = deniedRomByte(static_cast<uint16_t>(address));
            else if (hostHiRomMirror(address)) value = hostRomByte(address);
            else return read(address, value);
            return true;
        }
        return read(address, value);
    }
    uint8_t versionCode() const {
        if (isMc1()) return 0x01;
        return 0x04;
    }
    bool isMc1() const { return revision_ == GSURevision::MC1; }
    bool isGsu2() const { return revision_ == GSURevision::GSU2; }
    bool isGsu1Family() const {
        return revision_ == GSURevision::GSU1 ||
               revision_ == GSURevision::GSU1A ||
               revision_ == GSURevision::GSU2;
    }
    bool supportsFastMultiply() const { return !isMc1(); }
    bool drainsRamBufferOnStop() const { return isGsu1Family(); }
    uint8_t ramBankMask() const { return isGsu2() ? 0x01 : 0x00; }
    bool usesMc1LoadBodies() const { return isMc1(); }
    enum class IoMapKind : uint8_t { Canonical, Cache, OpenBus, Unmapped };
    struct IoMapResult { IoMapKind kind; uint16_t address; };
    IoMapResult mapHostIo(uint16_t a) const {
        if (a >= 0x3100 && a <= 0x32ff) return {IoMapKind::Cache, a};
        if (isMc1()) {
            if (a >= 0x3000 && a <= 0x301f) return {IoMapKind::Canonical, a};
            if (a >= 0x3020 && a <= 0x302f) return {IoMapKind::OpenBus, a};
            if (a >= 0x3030 && a <= 0x303f)
                return {IoMapKind::Canonical, a};
            if (a >= 0x3040 && a <= 0x305f)
                return {IoMapKind::Canonical, static_cast<uint16_t>(0x3000 + (a - 0x3040))};
            if (a >= 0x3060 && a <= 0x307f) {
                if ((a & 0x001f) == 0x001b) return {IoMapKind::Canonical, 0x303b};
                return {IoMapKind::Canonical, static_cast<uint16_t>(0x3030 + (a & 1))};
            }
            if (a >= 0x3080 && a <= 0x30ff) return {IoMapKind::OpenBus, a};
            if (a >= 0x3300 && a <= 0x332f) return {IoMapKind::OpenBus, a};
            if (a >= 0x3330 && a <= 0x333f) {
                if ((a & 0x000f) == 0x000b) return {IoMapKind::Canonical, 0x303b};
                return {IoMapKind::Canonical, static_cast<uint16_t>(0x3030 + (a & 1))};
            }
            if (a >= 0x3340 && a <= 0x335f)
                return {IoMapKind::Canonical, static_cast<uint16_t>(0x3000 + (a - 0x3340))};
            if (a >= 0x3360 && a <= 0x337f) {
                if ((a & 0x001f) == 0x001b) return {IoMapKind::Canonical, 0x303b};
                return {IoMapKind::Canonical, static_cast<uint16_t>(0x3030 + (a & 1))};
            }
            if (a >= 0x3380 && a <= 0x342f) return {IoMapKind::OpenBus, a};
            if (a >= 0x3430 && a <= 0x343f) {
                if ((a & 0x000f) == 0x000b) return {IoMapKind::Canonical, 0x303b};
                return {IoMapKind::Canonical, static_cast<uint16_t>(0x3030 + (a & 1))};
            }
            if (a >= 0x3440 && a <= 0x345f)
                return {IoMapKind::Canonical, static_cast<uint16_t>(0x3000 + (a - 0x3440))};
            if (a >= 0x3460 && a <= 0x347f) {
                if ((a & 0x001f) == 0x001b) return {IoMapKind::Canonical, 0x303b};
                return {IoMapKind::Canonical, static_cast<uint16_t>(0x3030 + (a & 1))};
            }
            if (a >= 0x3480 && a <= 0x3fff) return {IoMapKind::OpenBus, a};
            return {IoMapKind::Unmapped, a};
        }
        if (a >= 0x3000 && a <= 0x301f) return {IoMapKind::Canonical, a};
        if (a >= 0x3020 && a <= 0x302f)
            return {IoMapKind::Canonical, static_cast<uint16_t>(0x3030 + (a - 0x3020))};
        if (a >= 0x3030 && a <= 0x303f) return {IoMapKind::Canonical, a};
        if (a >= 0x3040 && a <= 0x30ff)
            return {IoMapKind::Canonical, static_cast<uint16_t>(0x3000 + ((a - 0x3000) & 0x003f))};
        if (a >= 0x3300 && a <= 0x34ff)
            return {IoMapKind::Canonical, static_cast<uint16_t>(0x3000 + ((a - 0x3300) & 0x003f))};
        if (a >= 0x3500 && a <= 0x3fff) return {IoMapKind::OpenBus, a};
        return {IoMapKind::Unmapped, a};
    }
    bool read(uint32_t address, uint8_t& value) override {
        const uint8_t bank = static_cast<uint8_t>(address >> 16);
        const uint16_t a = static_cast<uint16_t>(address);
        if (!systemBank(bank)) {
            if ((bank >= 0x70 && bank <= 0x7d) || bank >= 0xf0) {
                value = readRam(ramIndex(bank, a));
                return true;
            }
            return false;
        }
        if (a >= 0x6000 && a <= 0x7fff) {
            value = readRam(a - 0x6000);
            return true;
        }
        const IoMapResult io = mapHostIo(a);
        if (io.kind == IoMapKind::Unmapped) return false;
        if (io.kind == IoMapKind::OpenBus) { value = 0; return true; }
        const uint16_t ioa = io.address;
        if (ioa <= 0x301f) {
            const unsigned r = (ioa - 0x3000) >> 1;
            value = (ioa & 1) ? static_cast<uint8_t>(regs_[r] >> 8)
                            : static_cast<uint8_t>(regs_[r]);
            return true;
        }
        switch (ioa) {
        case 0x3030: value = static_cast<uint8_t>(status()); return true;
        case 0x3031: value = static_cast<uint8_t>(status() >> 8); sfr_ &= ~F_IRQ; return true;
        case 0x3034: value = pbr_; return true;
        case 0x3036: value = rombr_; return true;
        case 0x303b: value = versionCode(); return true;
        case 0x303c: value = rambr_ & 1; return true;
        case 0x303e: value = static_cast<uint8_t>(cbr_); return true;
        case 0x303f: value = static_cast<uint8_t>(cbr_ >> 8); return true;
        default:
            if (io.kind == IoMapKind::Cache) { value = cache_[(cbr_ + (ioa - 0x3100)) & 0x01ff]; return true; }
            value = 0;
            return true;
        }
    }
    bool write(uint32_t address, uint8_t value) override {
        address &= 0xFFFFFFu;
        const uint8_t bank = static_cast<uint8_t>(address >> 16);
        const uint16_t a = static_cast<uint16_t>(address);
        if (!systemBank(bank)) {
            if ((bank >= 0x70 && bank <= 0x71) || (bank >= 0xf0 && bank <= 0xf1)) {
                writeRam(ramIndex(bank, a), value);
                return true;
            }
            if ((bank >= 0x78 && bank <= 0x79) || (bank >= 0xf8 && bank <= 0xf9)) {
                if (bramr_) writeRam(ramIndex(bank, a), value);
                return true;
            }
            if ((bank >= 0x72 && bank <= 0x7d) || bank >= 0xf2) {
                if (!bramr_) writeRam(ramIndex(bank, a), value);
                return true;
            }
            return false;
        }
        if (a >= 0x6000 && a <= 0x7fff) {
            writeRam(a - 0x6000, value);
            return true;
        }
        const IoMapResult io = mapHostIo(a);
        if (io.kind == IoMapKind::Unmapped) return false;
        if (io.kind == IoMapKind::OpenBus) return true;
        const uint16_t ioa = io.address;
        if (ioa <= 0x301f) {
            const unsigned r = (ioa - 0x3000) >> 1;
            if (ioa & 1) {
                regs_[r] = static_cast<uint16_t>((regs_[r] & 0x00ffu) | (uint16_t(value) << 8));
                if (r == 14) scheduleRomRead();
                if (r == 15) start();
            } else {
                regs_[r] = static_cast<uint16_t>((regs_[r] & 0xff00u) | value);
            }
            return true;
        }
        switch (ioa) {
        case 0x3030: {
            sfr_ = static_cast<uint16_t>((sfr_ & 0xff00) | value);
            syncFlagsFromSfr();
            running_ = (sfr_ & F_G) != 0;
            if (!running_) {
                cbr_ = 0;
                invalidateCache();
            }
            return true;
        }
        case 0x3031: sfr_ = static_cast<uint16_t>((sfr_ & 0x00ff) | (uint16_t(value) << 8)); syncFlagsFromSfr(); running_ = (sfr_ & F_G) != 0; return true;
        case 0x3033: bramr_ = value & 0x01; return true;
        case 0x3034: pbr_ = value & 0x7f; invalidateCache(); return true;
        case 0x3037: cfgr_ = value; return true;
        case 0x3038:
            if (scbr_ != value) {
                scbr_ = value;
                refreshPixelDerivedState();
            }
            return true;
        case 0x3039: clsr_ = value & 1; return true;
        case 0x303a: {
            scmr_ = value;
            refreshPixelDerivedState();
            return true;
        }
        default:
            if (io.kind == IoMapKind::Cache) {
                const size_t i = (cbr_ + (ioa - 0x3100)) & 0x01ff;
                cache_[i] = value;
                if ((i & 0x0f) == 0x0f) markCacheLineValid(static_cast<uint16_t>(i));
                return true;
            }
            return true;
        }
    }
private:
    static size_t powerOfTwoMask(size_t size) {
        return size && (size & (size - 1)) == 0 ? size - 1 : 0;
    }
    size_t romIndex(size_t address) const {
        if (rom_.empty()) return 0;
        return rom_mask_ ? (address & rom_mask_) : (address % rom_.size());
    }
    size_t ramIndexLinear(size_t address) const {
        if (ram_.empty()) return 0;
        return ram_mask_ ? (address & ram_mask_) : (address % ram_.size());
    }
    uint8_t readRam(size_t address) const { return ram_[address]; }
    void writeRam(size_t address, uint8_t value) {
        ram_[address] = value;
        if (cartridge_ram_ != nullptr) {
            (*cartridge_ram_)[address] = value;
            if (cartridge_ram_dirty_ != nullptr) *cartridge_ram_dirty_ = true;
        }
    }
    static bool systemBank(uint8_t b) { return b <= 0x3f || (b >= 0x80 && b <= 0xbf); }
    static bool hostGamePakRam(uint32_t address) {
        const uint8_t bank = static_cast<uint8_t>(address >> 16);
        const uint16_t a = static_cast<uint16_t>(address);
        return ((systemBank(bank) && a >= 0x6000 && a <= 0x7fff) ||
                (bank >= 0x70 && bank <= 0x7d) || bank >= 0xf0);
    }
    static bool hostGamePakRom(uint32_t address) {
        const uint8_t bank = static_cast<uint8_t>(address >> 16);
        const uint16_t a = static_cast<uint16_t>(address);
        if (systemBank(bank) && a >= 0x8000) return true;
        if (bank >= 0x40 && bank <= 0x5f) return true;
        return bank >= 0xc0 && bank <= 0xdf;
    }
    static bool hostHiRomMirror(uint32_t address) {
        const uint8_t bank = static_cast<uint8_t>(address >> 16);
        return (bank >= 0x40 && bank <= 0x5f) || (bank >= 0xc0 && bank <= 0xdf);
    }
    uint8_t hostRomByte(uint32_t address) const {
        if (rom_.empty()) return 0xff;
        const uint8_t bank = static_cast<uint8_t>(address >> 16);
        const uint16_t a = static_cast<uint16_t>(address);
        const size_t off = size_t((bank & 0x3f) - 0x00) * 0x10000u + a;
        return rom_[romIndex(off)];
    }
    static uint8_t deniedRomByte(uint16_t address) {
        switch (address & 0x0f) {
        case 0x0: case 0x2: case 0x6: case 0x8: case 0xc: return 0x00;
        case 0x4: return 0x04;
        case 0xa: return 0x08;
        case 0xe: return 0x0c;
        default: return 0x01;
        }
    }
    bool programFetchGranted() const {
        if (pipe_fetch_cached_) return true;
        if (programRamBank(pbr_)) return (scmr_ & 0x08) != 0;
        return (scmr_ & 0x10) != 0;
    }
    bool instructionBusGranted(uint8_t op) const {
        if (!programFetchGranted()) return false;
        const unsigned a = alt();
        uint8_t requirements = kBusRequirements[a][op];
        if (op == 0xdf && a == 3 && rom_read_pending_) requirements |= BUS_ROM;
        if (op == 0xdf && a == 2 && ram_write_pending_) requirements |= BUS_RAM;
        if ((requirements & BUS_RAM) && (scmr_ & 0x08) == 0) return false;
        if ((requirements & BUS_ROM) && (scmr_ & 0x10) == 0) return false;
        return true;
    }
    size_t ramIndex(uint8_t bank, uint16_t a) const {
        const size_t base = (size_t(bank - 0x70) << 16);
        return ramIndexLinear(base + a);
    }
    static bool programRamBank(uint8_t bank) {
        return bank >= 0x60 && bank <= 0x7f;
    }
    size_t programRamIndex(uint8_t bank, uint16_t a) const {
        return ramIndexLinear((size_t(bank & 0x1fu) << 16) + a);
    }
    static size_t programRomOffset(uint8_t bank, uint16_t pc) {
        if (bank <= 0x3f) return size_t(bank) * 0x8000u + (pc & 0x7fffu);
        if (bank >= 0x40 && bank <= 0x5f) return size_t(bank - 0x40) * 0x10000u + pc;
        return size_t(bank & 0x3f) * 0x8000u + (pc & 0x7fffu);
    }
    uint8_t programBackingByte(uint8_t bank, uint16_t pc) const {
        if (programRamBank(bank)) return readRam(programRamIndex(bank, pc));
        if (rom_.empty()) return 0xff;
        return rom_[romIndex(programRomOffset(bank, pc))];
    }
    bool cacheLineValid(uint16_t delta) const {
        return (cache_valid_lines_ & (uint32_t(1) << ((delta & 0x01f0u) >> 4))) != 0;
    }
    void invalidateCache() {
        cache_valid_lines_ = 0;
        cache_fill_active_ = false;
        cache_fill_cycle_credit_ = 0;
    }
    void markCacheLineValid(uint16_t cacheIndex) {
        cache_valid_lines_ |= uint32_t(1) << ((cacheIndex & 0x01f0u) >> 4);
    }
    uint32_t ramPendingCycles() const {
        if (!ram_write_pending_) return 0;
        return uint32_t(ram_delay_) + (ram_write_queued_ ? uint32_t(ramBufferedCycles()) : 0u);
    }
    void serializeProgramBus(uint8_t bank) {
        if (programRamBank(bank)) {
            if (ram_write_pending_) {
                const uint32_t pending = ramPendingCycles();
                program_fetch_debt_cycles_ += pending;
            }
        } else if (rom_read_pending_) {
            program_fetch_debt_cycles_ += rom_delay_;
        }
    }
    void fillAutomaticCacheLine(uint8_t bank, uint16_t base, uint16_t delta) {
        serializeProgramBus(bank);
        const uint16_t lineStart = static_cast<uint16_t>(delta & 0x01f0u);
        const uint16_t sourcePc = static_cast<uint16_t>(base + lineStart);
        cache_fill_active_ = true;
        cache_fill_bank_ = bank;
        cache_fill_source_pc_ = sourcePc;
        cache_fill_line_start_ = lineStart;
        cache_fill_next_byte_ = 0;
        cache_fill_cycle_credit_ = 0;
        program_fetch_debt_cycles_ += uint32_t(programBusCycles()) * 15u;
    }
    void advanceCacheFill(uint32_t gsuCycles) {
        if (!cache_fill_active_) return;
        cache_fill_cycle_credit_ += gsuCycles;
        const uint8_t transferCycles = programBusCycles();
        while (cache_fill_active_ && cache_fill_cycle_credit_ >= transferCycles) {
            cache_fill_cycle_credit_ -= transferCycles;
            const uint16_t offset = static_cast<uint16_t>(cache_fill_line_start_ + cache_fill_next_byte_);
            const uint16_t pc = static_cast<uint16_t>(cache_fill_source_pc_ + cache_fill_next_byte_);
            cache_[offset] = programBackingByte(cache_fill_bank_, pc);
            if (++cache_fill_next_byte_ == 16) {
                markCacheLineValid(cache_fill_line_start_);
                cache_fill_active_ = false;
            }
        }
    }
    uint8_t programByte(uint8_t bank, uint16_t pc) {
        const uint16_t delta = static_cast<uint16_t>(pc - cbr_);
        if (delta < 512) {
            if (!cacheLineValid(delta)) fillAutomaticCacheLine(bank, cbr_, delta);
            return programBackingByte(bank, pc);
        }
        serializeProgramBus(bank);
        return programBackingByte(bank, pc);
    }
    void fetchPipe() {
        const uint16_t pc = regs_[15];
        const uint16_t delta = static_cast<uint16_t>(pc - cbr_);
        if (delta < 512 && cacheLineValid(delta)) {
            pipe_fetch_cached_ = true;
            pipe_ = cache_[delta];
            return;
        }
        pipe_fetch_cached_ = false;
        pipe_ = programByte(pbr_, pc);
    }
    uint16_t status() const {
        uint16_t s = sfr_ & ~(F_Z | F_C | F_S | F_OV | F_G);
        if (zero_) s |= F_Z;
        if (carry_) s |= F_C;
        if (sign_) s |= F_S;
        if (overflow_) s |= F_OV;
        if (running_) s |= F_G;
        return s;
    }
    void syncFlagsFromSfr() {
        zero_ = (sfr_ & F_Z) != 0;
        carry_ = (sfr_ & F_C) != 0;
        sign_ = (sfr_ & F_S) != 0;
        overflow_ = (sfr_ & F_OV) != 0;
    }
    void clearPrefixes() {
        sfr_ &= ~(F_A1 | F_A2 | F_B);
        src_ = dst_ = 0;
    }
    unsigned alt() const { return ((sfr_ & F_A1) ? 1u : 0u) | ((sfr_ & F_A2) ? 2u : 0u); }
    uint16_t S() const { return regs_[src_]; }
    uint8_t programBusCycles() const {
        return clsr_ ? 5u : 3u;
    }
    uint8_t romBufferedCycles() const {
        return programBusCycles();
    }
    uint8_t ramBufferedCycles() const {
        return programBusCycles();
    }
    uint8_t pixelRamCycles() const {
        return programBusCycles();
    }
    void scheduleRomRead() {
        rom_read_pending_ = true;
        rom_delay_ = romBufferedCycles();
        sfr_ |= F_R;
    }
    void completeRomRead() {
        if (!rom_read_pending_) return;
        rom_buffer_ = romRead(regs_[14]);
        rom_read_pending_ = false;
        rom_delay_ = 0;
        sfr_ &= ~F_R;
    }
    void commitPendingRamWrite() {
        if (!ram_write_pending_) return;
        writeRam(ramIndexLinear((size_t(rambr_ & 1u) << 16) + ram_write_address_), ram_write_value_);
        if (ram_write_queued_) {
            ram_write_address_ = ram_write_queued_address_;
            ram_write_value_ = ram_write_queued_value_;
            ram_write_queued_ = false;
            ram_write_queued_address_ = 0;
            ram_write_queued_value_ = 0;
            ram_delay_ = ramBufferedCycles();
            ram_write_pending_ = true;
        } else {
            ram_write_pending_ = false;
            ram_delay_ = 0;
        }
    }
    void advancePendingOperations(uint32_t gsuCycles) {
        if (rom_read_pending_ && (scmr_ & 0x10)) {
            if (gsuCycles >= rom_delay_) completeRomRead();
            else rom_delay_ = static_cast<uint8_t>(rom_delay_ - gsuCycles);
        }
        if (ram_write_pending_ && (scmr_ & 0x08)) {
            uint32_t remaining = gsuCycles;
            while (ram_write_pending_ && remaining) {
                if (remaining < ram_delay_) {
                    ram_delay_ = static_cast<uint8_t>(ram_delay_ - remaining);
                    remaining = 0;
                } else {
                    remaining -= ram_delay_;
                    ram_delay_ = 0;
                    commitPendingRamWrite();
                }
            }
        }
    }
    uint8_t readRomBuffer() {
        assert(!rom_read_pending_ && "SuperFX ROM buffer consumed before R14 transfer completed");
        return rom_buffer_;
    }
    void setD(uint32_t v) {
        regs_[dst_] = static_cast<uint16_t>(v);
        if (dst_ == 14) scheduleRomRead();
    }
    void setSZ(uint32_t v) { const uint16_t w = static_cast<uint16_t>(v); sign_ = (w & 0x8000) != 0; zero_ = (w == 0); }
    bool add(uint16_t rhs, bool withCarry) {
        const uint16_t lhs = S();
        const uint32_t sum = uint32_t(lhs) + rhs + (withCarry && carry_ ? 1u : 0u);
        carry_ = sum > 0xffff;
        overflow_ = ((~(lhs ^ rhs) & (rhs ^ uint16_t(sum)) & 0x8000) != 0);
        const bool pcDest = dst_ == 15;
        setD(sum); setSZ(sum); clearPrefixes();
        return pcDest;
    }
    bool sub(uint16_t rhs, bool withCarry, bool compare) {
        const uint16_t lhs = S();
        const int32_t borrow = withCarry ? (carry_ ? 0 : 1) : 0;
        const int32_t d = int32_t(uint32_t(lhs)) - int32_t(uint32_t(rhs)) - borrow;
        carry_ = d >= 0;
        overflow_ = (((lhs ^ rhs) & (lhs ^ uint16_t(d)) & 0x8000) != 0);
        setSZ(uint16_t(d));
        const bool pcDest = !compare && dst_ == 15;
        if (!compare) setD(uint16_t(d));
        clearPrefixes();
        return pcDest;
    }
    void start() {
        running_ = true;
        sfr_ &= ~F_IRQ;
        clock_credit_ = 0;
        clocks_until_step_ = 0;
        program_fetch_debt_cycles_ = 0;
    }
    uint32_t instructionCycles(uint8_t op) {
        const uint32_t fetchDebt = program_fetch_debt_cycles_;
        program_fetch_debt_cycles_ = 0;
        auto charged = [fetchDebt](uint32_t cycles) { return cycles + fetchDebt; };
        const bool cached = pipe_fetch_cached_;
        const bool commonOneByte = kCommonOneByteOpcode[op] != 0;
        const uint32_t fetchByte = cached ? 1u : programBusCycles();
        if (commonOneByte) return charged(fetchByte);
        const unsigned a = alt();
        if (op == 0x00 && drainsRamBufferOnStop() && ram_write_pending_)
            return charged(std::max(fetchByte, uint32_t(ram_delay_)));
        if (op == 0x02 && !cached) {
            const uint16_t nextBase = regs_[15] & 0xfff0u;
            return charged(fetchByte + (cbr_ != nextBase ? 1u : 0u));
        }
        if (op >= 0x05 && op <= 0x0f) return charged(fetchByte * 2u);
        if (op >= 0xa0 && op <= 0xaf && a == 0) return charged(fetchByte * 2u);
        if (op >= 0xf0 && a == 0) return charged(fetchByte * 3u);
        if (op >= 0x98 && op <= 0x9d) return charged(fetchByte);
        if (op >= 0x80 && op <= 0x8f) {
            const bool fastMultiply = supportsFastMultiply() && (cfgr_ & 0x20) != 0;
            if (fastMultiply) return charged(fetchByte);
            return charged(fetchByte + (cached ? 1u : 2u));
        }
        if (op == 0x9f) {
            const bool fastMultiply = supportsFastMultiply() && (cfgr_ & 0x20) != 0;
            if (cached) return charged(fastMultiply ? 4u : 8u);
            return charged(fetchByte + (fastMultiply ? 4u : 8u));
        }
        if (op >= 0x40 && op <= 0x4b) {
            const bool byteLoad = (a & 1u) != 0;
            if (cached) return charged(byteLoad ? 6u : 7u);
            const bool programInRam = programRamBank(pbr_);
            uint32_t lowTotal = byteLoad ? (programInRam ? 13u : 11u)
                                         : (programInRam ? 12u : 10u);
            if (clsr_) lowTotal += 2u;
            return charged(lowTotal);
        }
        auto overlapTransfer = [](uint32_t instructionFetch, uint32_t remaining) {
            return std::max(instructionFetch, remaining);
        };
        auto ramOverlap = [&](uint32_t instructionFetch, uint32_t remaining) {
            const uint32_t total = overlapTransfer(instructionFetch, remaining);
            return total;
        };
        auto romOverlap = [&](uint32_t instructionFetch, uint32_t remaining) {
            const uint32_t total = overlapTransfer(instructionFetch, remaining);
            return total;
        };
        if (usesMc1LoadBodies() && !clsr_ && a == 1 &&
            op >= 0xa0 && op <= 0xaf) {
            const uint32_t body = cached ? 9u : 14u;
            return charged(ramOverlap(body, ramPendingCycles()));
        }
        if (usesMc1LoadBodies() && !clsr_ && a == 1 && op >= 0xf0) {
            const bool programInRam = programRamBank(pbr_);
            const uint32_t body = cached ? 10u : (programInRam ? 18u : 17u);
            return charged(ramOverlap(body, ramPendingCycles()));
        }
        if (a == 1 && ((op >= 0xa0 && op <= 0xaf) || op >= 0xf0))
            return charged(ramOverlap(fetchByte, ramPendingCycles()));
        if (op >= 0x30 && op <= 0x3b) {
            const uint32_t olderTail = ramPendingCycles();
            if (programRamBank(pbr_)) {
                const bool byteStore = (a & 1u) != 0;
                const uint32_t sameBusPenalty = clsr_ ? (byteStore ? 4u : 8u)
                                                      : (byteStore ? 2u : 4u);
                return charged(ramOverlap(fetchByte + sameBusPenalty, olderTail));
            }
            return charged(ramOverlap(fetchByte, olderTail));
        }
        if (op == 0x90) {
            const uint32_t olderTail = ramPendingCycles();
            if (programRamBank(pbr_)) {
                const uint32_t sameBusPenalty = clsr_ ? 8u : 4u;
                return charged(ramOverlap(fetchByte + sameBusPenalty, olderTail));
            }
            return charged(ramOverlap(fetchByte, olderTail));
        }
        if (op >= 0xa0 && op <= 0xaf && a == 2) {
            uint32_t bytes = fetchByte * 2u;
            if (programRamBank(pbr_))
                bytes += clsr_ ? 8u : 4u;
            return charged(ramOverlap(bytes, ramPendingCycles()));
        }
        if (op >= 0xf0 && a == 2) {
            uint32_t bytes = fetchByte * 3u;
            if (programRamBank(pbr_))
                bytes += clsr_ ? 8u : 4u;
            return charged(ramOverlap(bytes, ramPendingCycles()));
        }
        if (op == 0xdf && a == 2)
            return charged(ramOverlap(fetchByte, ramPendingCycles()));
        if (op == 0xef || (op == 0xdf && a < 2))
            return charged(romOverlap(fetchByte, rom_read_pending_ ? rom_delay_ : 0u));
        if (op == 0xdf && a == 3)
            return charged(romOverlap(fetchByte, rom_read_pending_ ? rom_delay_ : 0u));
        return charged(fetchByte);
    }
    uint8_t ramRead(uint16_t address) {
        assert(!ram_write_pending_ && !ram_write_queued_ && "SuperFX RAM read retired before buffered write completed");
        return readRam(ramIndexLinear((size_t(rambr_ & 1u) << 16) + address));
    }
    void ramWrite(uint16_t address, uint8_t value) {
        if (ram_write_pending_) {
            assert(!ram_write_queued_ && "SuperFX RAM write queue overflow");
            if (!ram_write_queued_) {
                ram_write_queued_ = true;
                ram_write_queued_address_ = address;
                ram_write_queued_value_ = value;
            }
        } else {
            ram_write_pending_ = true;
            ram_delay_ = ramBufferedCycles();
            ram_write_address_ = address;
            ram_write_value_ = value;
        }
        last_ram_addr_ = address;
    }
    uint8_t romRead(uint16_t address) const {
        if (rom_.empty()) return 0xff;
        const uint8_t bank = rombr_;
        size_t off;
        if (bank <= 0x3f) off = size_t(bank) * 0x8000 + (address & 0x7fff);
        else if (bank >= 0x40 && bank <= 0x5f) off = size_t(bank - 0x40) * 0x10000 + address;
        else off = size_t(bank & 0x3f) * 0x8000 + (address & 0x7fff);
        return rom_[romIndex(off)];
    }
    void refreshPixelDerivedState() {
        static constexpr uint8_t planes[4] = {2, 4, 4, 8};
        pixel_planes_ = planes[scmr_ & 0x03u];
        pixel_tile_bytes_ = static_cast<uint8_t>(pixel_planes_ << 3);
        screen_base_ = size_t(scbr_) << 10;
        screen_height_mode_ = (por_ & 0x10u) ? 3u
            : static_cast<uint8_t>(((scmr_ & 0x04u) ? 1u : 0u) | ((scmr_ & 0x20u) ? 2u : 0u));
    }
    void loadPlotColor(uint8_t source) {
        if (por_ & 0x04u) {
            colr_ = static_cast<uint8_t>((colr_ & 0xf0u) | (source >> 4));
        } else if (por_ & 0x08u) {
            colr_ = static_cast<uint8_t>((colr_ & 0xf0u) | (source & 0x0fu));
        } else {
            colr_ = source;
        }
    }
    uint8_t plotDrawingColor(uint8_t x, uint8_t y) const {
        uint8_t c = colr_;
        if ((por_ & 0x02u) && pixelPlanes() != 8) {
            if ((x ^ y) & 1u) c >>= 4;
            c &= 0x0fu;
        }
        return c;
    }
    bool plotColorIsTransparent() const {
        if (por_ & 0x01u) return false;
        if (pixelMode() != 3) return (colr_ & 0x0fu) == 0;
        if ((por_ & 0x08u) != 0) return (colr_ & 0x0fu) == 0;
        return colr_ == 0;
    }
    unsigned screenHeight() const {
        static constexpr unsigned heights[4] = {128, 160, 192, 256};
        return heights[screen_height_mode_];
    }
    unsigned pixelMode() const { return scmr_ & 0x03; }
    unsigned pixelPlanes() const { return pixel_planes_; }
    struct PixelCache {
        uint8_t x = 0, y = 0, valid = 0;
        std::array<uint8_t, 8> pixels{};
    };
    enum class PixelBusKind : uint8_t {
        None = 0,
        MergeRead,
        FullWrite,
        MergedWrite,
        RpixRead,
    };
    struct PixelBusOp {
        uint32_t address = 0;
        uint8_t kind = static_cast<uint8_t>(PixelBusKind::None);
        uint8_t plane = 0;
        uint8_t value = 0;
        uint8_t mask_or_bit = 0;
        uint8_t clear_cache = 0;
        uint8_t reserved0 = 0;
        uint8_t reserved1 = 0;
        uint8_t reserved2 = 0;
    };
    static constexpr size_t kPixelBusQueueCapacity = 48;
    void clearPixelBusQueue() {
        pixel_bus_ops_ = {};
        pixel_bus_head_ = 0;
        pixel_bus_count_ = 0;
        pixel_bus_merge_latch_ = 0;
        pixel_bus_waiting_for_ran_ = false;
    }
    bool enqueuePixelBusOp(PixelBusKind kind, size_t address, uint8_t value = 0,
                           uint8_t maskOrBit = 0, uint8_t plane = 0, uint8_t clearCache = 0) {
        const size_t slot = size_t(pixel_bus_head_) + size_t(pixel_bus_count_);
        assert(slot < pixel_bus_ops_.size() && "SuperFX pixel transaction queue overflow");
        if (slot >= pixel_bus_ops_.size()) return false;
        PixelBusOp& op = pixel_bus_ops_[slot];
        op.address = static_cast<uint32_t>(address);
        op.kind = static_cast<uint8_t>(kind);
        op.plane = plane;
        op.value = value;
        op.mask_or_bit = maskOrBit;
        op.clear_cache = clearCache;
        op.reserved0 = op.reserved1 = op.reserved2 = 0;
        ++pixel_bus_count_;
        return true;
    }
    uint8_t pixelPlaneByte(const PixelCache& cache, unsigned plane) const {
        uint64_t bits = 0;
        static_assert(sizeof(cache.pixels) == sizeof(bits));
        std::memcpy(&bits, cache.pixels.data(), sizeof(bits));
        uint64_t t = (bits ^ (bits >> 7)) & 0x00AA00AA00AA00AAull;
        bits ^= t ^ (t << 7);
        t = (bits ^ (bits >> 14)) & 0x0000CCCC0000CCCCull;
        bits ^= t ^ (t << 14);
        t = (bits ^ (bits >> 28)) & 0x00000000F0F0F0F0ull;
        bits ^= t ^ (t << 28);
        return static_cast<uint8_t>(bits >> (plane * 8u));
    }
    size_t pixelPlaneAddress(const PixelCache& cache, unsigned plane) const {
        const size_t base = tileAddress(cache.x, cache.y);
        return ramIndexLinear(base + ((plane >> 1) << 4) + (plane & 1u));
    }
    void enqueuePixelCacheFlush(PixelCache& cache, uint8_t clearCache) {
        (void)clearCache;
        if (!cache.valid) return;
        const unsigned planes = pixelPlanes();
        const bool full = cache.valid == 0xff;
        for (unsigned plane = 0; plane < planes; ++plane) {
            const size_t off = pixelPlaneAddress(cache, plane);
            const uint8_t planeByte = pixelPlaneByte(cache, plane);
            const uint8_t finalMarker = 0;
            if (full) {
                enqueuePixelBusOp(PixelBusKind::FullWrite, off, planeByte, 0, static_cast<uint8_t>(plane), finalMarker);
            } else {
                enqueuePixelBusOp(PixelBusKind::MergeRead, off, 0, cache.valid, static_cast<uint8_t>(plane));
                enqueuePixelBusOp(PixelBusKind::MergedWrite, off, planeByte, cache.valid,
                                  static_cast<uint8_t>(plane), finalMarker);
            }
        }
        cache.valid = 0;
    }
    void enqueueRpixReads(uint8_t x, uint8_t y) {
        const size_t base = tileAddress(x, y);
        const uint8_t bit = static_cast<uint8_t>((x & 7u) ^ 7u);
        for (unsigned plane = 0; plane < pixelPlanes(); ++plane) {
            const size_t off = ramIndexLinear(base + ((plane >> 1) << 4) + (plane & 1u));
            enqueuePixelBusOp(PixelBusKind::RpixRead, off, 0, bit, static_cast<uint8_t>(plane));
        }
    }
    void commitPixelBusTransaction() {
        if (pixel_bus_count_ == 0) return;
        PixelBusOp& op = pixel_bus_ops_[pixel_bus_head_];
        const auto kind = static_cast<PixelBusKind>(op.kind);
        switch (kind) {
        case PixelBusKind::MergeRead:
            pixel_bus_merge_latch_ = readRam(ramIndexLinear(op.address));
            break;
        case PixelBusKind::FullWrite:
            writeRam(ramIndexLinear(op.address), op.value);
            break;
        case PixelBusKind::MergedWrite: {
            const uint8_t merged = static_cast<uint8_t>((op.value & op.mask_or_bit) |
                                                        (pixel_bus_merge_latch_ & static_cast<uint8_t>(~op.mask_or_bit)));
            writeRam(ramIndexLinear(op.address), merged);
            break;
        }
        case PixelBusKind::RpixRead: {
            const uint8_t byte = readRam(ramIndexLinear(op.address));
            pixel_bus_rpix_result_ = static_cast<uint8_t>(pixel_bus_rpix_result_ |
                ((((byte >> op.mask_or_bit) & 1u) << op.plane) & 0xffu));
            break;
        }
        case PixelBusKind::None:
        default:
            break;
        }
        op = {};
        ++pixel_bus_head_;
        --pixel_bus_count_;
        pixel_bus_waiting_for_ran_ = false;
        if (pixel_bus_count_ == 0) pixel_bus_head_ = 0;
    }
    void advancePixelBusTimeline(uint32_t gsuCycles, bool& transactionBoundary) {
        transactionBoundary = false;
        if (gsuCycles == 0 || (pixel_bus_start_delay_ == 0 && pixel_bus_cycles_remaining_ == 0)) return;
        uint32_t remaining = gsuCycles;
        if (pixel_bus_start_delay_) {
            const uint32_t n = std::min(remaining, pixel_bus_start_delay_);
            pixel_bus_start_delay_ -= n;
            remaining -= n;
            if (pixel_bus_start_delay_ == 0 && pixel_bus_cycles_remaining_ && pixel_bus_slice_remaining_ == 0)
                pixel_bus_slice_remaining_ = std::min<uint32_t>(pixel_bus_cycles_remaining_, pixelRamCycles());
        }
        while (remaining && pixel_bus_cycles_remaining_) {
            if (pixel_bus_waiting_for_ran_) break;
            if (pixel_bus_slice_remaining_ == 0)
                pixel_bus_slice_remaining_ = std::min<uint32_t>(pixel_bus_cycles_remaining_, pixelRamCycles());
            const uint32_t n = std::min(remaining, pixel_bus_slice_remaining_);
            pixel_bus_slice_remaining_ -= n;
            pixel_bus_cycles_remaining_ -= n;
            remaining -= n;
            if (pixel_bus_slice_remaining_ == 0) {
                if ((scmr_ & 0x08u) == 0) {
                    pixel_bus_waiting_for_ran_ = true;
                    transactionBoundary = true;
                    return;
                }
                commitPixelBusTransaction();
                transactionBoundary = true;
                if (pixel_bus_cycles_remaining_)
                    pixel_bus_slice_remaining_ = std::min<uint32_t>(pixel_bus_cycles_remaining_, pixelRamCycles());
                return;
            }
        }
    }
    bool servicePixelBusGrantWait(uint64_t gsuCycleMasterClocks) {
        if (!pixel_bus_waiting_for_ran_) return false;
        if ((scmr_ & 0x08u) != 0) {
            commitPixelBusTransaction();
            if (pixel_bus_cycles_remaining_)
                pixel_bus_slice_remaining_ = std::min<uint32_t>(pixel_bus_cycles_remaining_, pixelRamCycles());
            return true;
        }
        const uint64_t availableCycles = clock_credit_ / gsuCycleMasterClocks;
        if (availableCycles == 0) return true;
        clock_credit_ -= availableCycles * gsuCycleMasterClocks;
        if (rom_read_pending_ && (scmr_ & 0x10u))
            advancePendingOperations(static_cast<uint32_t>(std::min<uint64_t>(availableCycles, 0xffffffffu)));
        return true;
    }
    bool servicePixelBusRamBufferWait(uint64_t gsuCycleMasterClocks) {
        if (pixel_bus_start_delay_ != 0 || pixel_bus_count_ == 0 || !ram_write_pending_) return false;
        const uint64_t availableCycles = clock_credit_ / gsuCycleMasterClocks;
        if (availableCycles == 0) return true;
        if ((scmr_ & 0x08u) == 0) {
            clock_credit_ -= availableCycles * gsuCycleMasterClocks;
            if (rom_read_pending_ && (scmr_ & 0x10u))
                advancePendingOperations(static_cast<uint32_t>(std::min<uint64_t>(availableCycles, 0xffffffffu)));
            return true;
        }
        const uint64_t eventCycles = std::min<uint64_t>(availableCycles, std::max<uint32_t>(1u, ram_delay_));
        clock_credit_ -= eventCycles * gsuCycleMasterClocks;
        advancePendingOperations(static_cast<uint32_t>(eventCycles));
        return true;
    }
    uint32_t pixelCacheTransactions(const PixelCache& cache) const {
        if (!cache.valid) return 0;
        const uint32_t transactionsPerPlane = cache.valid == 0xff ? 1u : 2u;
        return pixelPlanes() * transactionsPerPlane;
    }
    uint32_t chargePixelTransactions(uint32_t transactions) {
        const uint32_t cycles = transactions * pixelRamCycles();
        return cycles;
    }
    uint32_t graphicsMemoryCycles(uint8_t op, uint32_t instructionCycles) {
        clearPixelBusQueue();
        pixel_bus_rpix_result_ = 0;
        pixel_bus_rpix_result_valid_ = false;
        pixel_bus_start_delay_ = 0;
        pixel_bus_cycles_remaining_ = 0;
        pixel_bus_slice_remaining_ = 0;
        if (op != 0x4c) return 0;
        const unsigned a = alt();
        if (a & 1u) {
            enqueuePixelCacheFlush(secondary_pixel_cache_, 1);
            enqueuePixelCacheFlush(primary_pixel_cache_, 2);
            enqueueRpixReads(static_cast<uint8_t>(regs_[1]), static_cast<uint8_t>(regs_[2]));
            pixel_bus_rpix_result_valid_ = true;
        } else {
            const uint8_t x = static_cast<uint8_t>(regs_[1]);
            const uint8_t y = static_cast<uint8_t>(regs_[2]);
            if (y < screenHeight()) {
                if (!plotColorIsTransparent()) {
                    const bool changesGroup = primary_pixel_cache_.x != (x & 0xf8u) || primary_pixel_cache_.y != y;
                    const uint8_t bit = static_cast<uint8_t>(1u << ((x & 7u) ^ 7u));
                    const bool completesGroup = !changesGroup &&
                        static_cast<uint8_t>(primary_pixel_cache_.valid | bit) == 0xff;
                    if (changesGroup || completesGroup)
                        enqueuePixelCacheFlush(secondary_pixel_cache_, 1);
                }
            }
        }
        if (pixel_bus_count_ == 0) return 0;
        const uint32_t transactionCycles = chargePixelTransactions(pixel_bus_count_);
        pixel_bus_start_delay_ = instructionCycles;
        pixel_bus_cycles_remaining_ = transactionCycles;
        pixel_bus_slice_remaining_ = 0;
        return transactionCycles;
    }
    uint16_t tileIndex(uint8_t x, uint8_t y) const {
        switch (screen_height_mode_) {
        default:
        case 0: return static_cast<uint16_t>(((x & 0xf8u) << 1) + ((y & 0xf8u) >> 3));
        case 1: return static_cast<uint16_t>(((x & 0xf8u) << 1) + ((x & 0xf8u) >> 1) + ((y & 0xf8u) >> 3));
        case 2: return static_cast<uint16_t>(((x & 0xf8u) << 1) + (x & 0xf8u) + ((y & 0xf8u) >> 3));
        case 3: return static_cast<uint16_t>(((y & 0x80u) << 2) + ((x & 0x80u) << 1) + ((y & 0x78u) << 1) + ((x & 0x78u) >> 3));
        }
    }
    size_t tileAddress(uint8_t x, uint8_t y) const {
        const size_t requested = screen_base_ + size_t(tileIndex(x, y)) * pixel_tile_bytes_ + size_t(y & 7u) * 2u;
        return ramIndexLinear(requested);
    }
    void writePixelCache(PixelCache& cache) {
        if (!cache.valid) return;
        const size_t base = tileAddress(cache.x, cache.y);
        const unsigned planes = pixelPlanes();
        if (running_ && pixel_bus_count_ == 0 && !pixel_bus_rpix_result_valid_) {
            const uint64_t transactions = uint64_t(planes) * (cache.valid == 0xff ? 1u : 2u);
        }
        uint64_t bits = 0;
        static_assert(sizeof(cache.pixels) == sizeof(bits));
        std::memcpy(&bits, cache.pixels.data(), sizeof(bits));
        uint64_t t = (bits ^ (bits >> 7)) & 0x00AA00AA00AA00AAull;
        bits ^= t ^ (t << 7);
        t = (bits ^ (bits >> 14)) & 0x0000CCCC0000CCCCull;
        bits ^= t ^ (t << 14);
        t = (bits ^ (bits >> 28)) & 0x00000000F0F0F0F0ull;
        bits ^= t ^ (t << 28);
        auto planeByte = [&](unsigned plane) -> uint8_t {
            return static_cast<uint8_t>(bits >> (plane * 8u));
        };
        auto planeOffset = [&](unsigned plane) -> size_t {
            const size_t requested = base + ((plane >> 1) << 4) + (plane & 1u);
            return ram_mask_ ? (requested & ram_mask_) : (requested % ram_.size());
        };
        if (cache.valid == 0xff) {
            writeRam(planeOffset(0), planeByte(0));
            writeRam(planeOffset(1), planeByte(1));
            if (planes >= 4) {
                writeRam(planeOffset(2), planeByte(2));
                writeRam(planeOffset(3), planeByte(3));
            }
            if (planes == 8) {
                writeRam(planeOffset(4), planeByte(4));
                writeRam(planeOffset(5), planeByte(5));
                writeRam(planeOffset(6), planeByte(6));
                writeRam(planeOffset(7), planeByte(7));
            }
            cache.valid = 0;
            return;
        }
        const uint8_t preserveMask = static_cast<uint8_t>(~cache.valid);
        auto mergePlane = [&](unsigned plane) {
            const size_t off = planeOffset(plane);
            writeRam(off, static_cast<uint8_t>((planeByte(plane) & cache.valid) |
                                               (readRam(off) & preserveMask)));
        };
        mergePlane(0); mergePlane(1);
        if (planes >= 4) { mergePlane(2); mergePlane(3); }
        if (planes == 8) { mergePlane(4); mergePlane(5); mergePlane(6); mergePlane(7); }
        cache.valid = 0;
    }
    void flushPrimaryPixelCache(uint8_t x, uint8_t y) {
        writePixelCache(secondary_pixel_cache_);
        secondary_pixel_cache_ = primary_pixel_cache_;
        primary_pixel_cache_.valid = 0;
        primary_pixel_cache_.x = x & 0xf8u;
        primary_pixel_cache_.y = y;
    }
    uint8_t readPixel(uint8_t x, uint8_t y) {
        writePixelCache(secondary_pixel_cache_);
        writePixelCache(primary_pixel_cache_);
        if (pixel_bus_rpix_result_valid_) {
            assert(pixel_bus_count_ == 0 && !pixel_bus_waiting_for_ran_ &&
                   "SuperFX RPIX retired before all pixel RAM transactions completed");
            const uint8_t staged = pixel_bus_rpix_result_;
            pixel_bus_rpix_result_ = 0;
            pixel_bus_rpix_result_valid_ = false;
            return staged;
        }
        const size_t base = tileAddress(x, y);
        const unsigned bit = (x & 7u) ^ 7u;
        auto readPlane = [&](unsigned plane) -> uint8_t {
            const size_t off = ramIndexLinear(base + ((plane >> 1) << 4) + (plane & 1u));
            return static_cast<uint8_t>(((readRam(off) >> bit) & 1u) << plane);
        };
        uint8_t c = static_cast<uint8_t>(readPlane(0) | readPlane(1));
        if (pixel_planes_ >= 4) c = static_cast<uint8_t>(c | readPlane(2) | readPlane(3));
        if (pixel_planes_ == 8) c = static_cast<uint8_t>(c | readPlane(4) | readPlane(5) | readPlane(6) | readPlane(7));
        return c;
    }
    void plotPixel(uint8_t x, uint8_t y) {
        if (y >= screenHeight()) { regs_[1]++; return; }
        if (plotColorIsTransparent()) { regs_[1]++; return; }
        const uint8_t c = plotDrawingColor(x, y);
        if (primary_pixel_cache_.x != (x & 0xf8u) || primary_pixel_cache_.y != y) flushPrimaryPixelCache(x, y);
        const uint8_t bit = static_cast<uint8_t>((x & 7u) ^ 7u);
        primary_pixel_cache_.pixels[bit] = c;
        primary_pixel_cache_.valid |= static_cast<uint8_t>(1u << bit);
        if (primary_pixel_cache_.valid == 0xff) flushPrimaryPixelCache(x, y);
        regs_[1]++;
    }
    void fetchImmediateByte(uint8_t& out) {
        out = pipe_;
        regs_[15]++;
        fetchPipe();
    }
    uint16_t fetchImmediateWord() {
        uint8_t lo = pipe_; regs_[15]++; fetchPipe();
        uint8_t hi = pipe_; regs_[15]++; fetchPipe();
        return static_cast<uint16_t>(lo | (uint16_t(hi) << 8));
    }
    bool branchCondition(uint8_t op) const {
        switch (op) {
        case 0x05: return true;
        case 0x06: return sign_ == overflow_;
        case 0x07: return sign_ != overflow_;
        case 0x08: return !zero_;
        case 0x09: return zero_;
        case 0x0a: return !sign_;
        case 0x0b: return sign_;
        case 0x0c: return !carry_;
        case 0x0d: return carry_;
        case 0x0e: return !overflow_;
        case 0x0f: return overflow_;
        default: return false;
        }
    }
    void step() {
        const uint8_t op = pipe_;
        fetchPipe();
        const unsigned a = alt();
        auto finish = [&]() { clearPrefixes(); regs_[15]++; };
        auto finishResult = [&]() { const bool pcDest = dst_ == 15; clearPrefixes(); if (!pcDest) regs_[15]++; };
        auto rhsFor = [&](unsigned reg, bool immediate) -> uint16_t {
            return immediate ? static_cast<uint16_t>(reg) : regs_[reg];
        };
        switch (op >> 4) {
        case 0x5: {
            const unsigned r=op&15; const uint16_t rhs=rhsFor(r, a>=2);
            if (!add(rhs, a==1 || a==3)) regs_[15]++;
            return;
        }
        case 0x6: {
            const unsigned r=op&15;
            if (a==3) { sub(regs_[r], false, true); regs_[15]++; }
            else if (!sub(rhsFor(r, a==2), a==1, false)) regs_[15]++;
            return;
        }
        case 0x7: {
            if (op == 0x70) {
                const uint16_t v=static_cast<uint16_t>((regs_[7]&0xff00)|((regs_[8]>>8)&0x00ff));
                setD(v); sign_=(v&0x8080u)!=0; zero_=(v&0xf0f0u)!=0; carry_=(v&0xe0e0u)!=0; overflow_=(v&0xc0c0u)!=0;
                finishResult();
            } else {
                const unsigned r=op&15; const uint16_t rhs=rhsFor(r, a>=2);
                const uint16_t v=(a&1u)?uint16_t(S()&~rhs):uint16_t(S()&rhs); setD(v); setSZ(v); finishResult();
            }
            return;
        }
        case 0x8: {
            const unsigned r=op&15; const uint8_t rhs=static_cast<uint8_t>(a>=2 ? r : regs_[r]);
            uint16_t v;
            if (a&1u) v=uint16_t(uint8_t(S())*rhs);
            else v=uint16_t(int16_t(int8_t(S()))*int16_t(int8_t(rhs)));
            setD(v); setSZ(v); finishResult();
            return;
        }
        case 0xb: {
            const unsigned r=op&15;
            if (sfr_&F_B) {
                const uint16_t v=regs_[r];
                const bool pcDest = dst_ == 15;
                if (!pcDest) regs_[15]++;
                setD(v); setSZ(v); overflow_=(v&0x80)!=0; clearPrefixes();
            } else { src_=r; regs_[15]++; }
            return;
        }
        case 0xc: {
            if (op == 0xc0) {
                const uint16_t v=uint8_t(S()>>8); setD(v); sign_=(v&0x80)!=0; zero_=(v==0); finishResult();
            } else {
                const unsigned r=op&15; const uint16_t rhs=rhsFor(r,a>=2);
                const uint16_t v=(a&1u)?uint16_t(S()^rhs):uint16_t(S()|rhs); setD(v); setSZ(v); finishResult();
            }
            return;
        }
        default: break;
        }
        if (op == 0x00) {
            while (ram_write_pending_)
                commitPendingRamWrite();
            while (pixel_bus_count_ != 0)
                commitPixelBusTransaction();
            writePixelCache(secondary_pixel_cache_);
            writePixelCache(primary_pixel_cache_);
            pixel_bus_start_delay_ = 0;
            pixel_bus_cycles_remaining_ = 0;
            pixel_bus_slice_remaining_ = 0;
            pixel_bus_waiting_for_ran_ = false;
            running_ = false;
            if ((cfgr_ & 0x80u) == 0) sfr_ |= F_IRQ;
            pipe_ = 0x01;
            clearPrefixes();
            regs_[15]++;
            return;
        }
        if (op == 0x01) { finish(); return; }
        if (op == 0x02) {
            const uint16_t nextBase = regs_[15] & 0xfff0;
            if (cbr_ != nextBase) {
                cbr_ = nextBase;
                invalidateCache();
            }
            finish();
            return;
        }
        if (op == 0x03) { carry_ = (S() & 1) != 0; const uint16_t v = S() >> 1; setD(v); setSZ(v); finishResult(); return; }
        if (op == 0x04) { const uint16_t old=S(); const uint16_t v = uint16_t((old << 1) | (carry_ ? 1 : 0)); carry_=(old&0x8000)!=0; setD(v); setSZ(v); finishResult(); return; }
        if (op >= 0x05 && op <= 0x0f) {
            const int8_t disp = static_cast<int8_t>(pipe_);
            regs_[15]++;
            fetchPipe();
            if (branchCondition(op)) regs_[15] = static_cast<uint16_t>(regs_[15] + disp);
            else regs_[15]++;
            return;
        }
        if (op >= 0x10 && op <= 0x1f) {
            const unsigned r = op & 15;
            if (sfr_ & F_B) {
                const uint16_t v = S();
                regs_[r] = v;
                if (r == 14) scheduleRomRead();
                clearPrefixes();
                if (r != 15) regs_[15]++;
            } else {
                dst_ = r;
                regs_[15]++;
            }
            return;
        }
        if (op >= 0x20 && op <= 0x2f) { src_ = dst_ = op & 15; sfr_ |= F_B; regs_[15]++; return; }
        if (op >= 0x30 && op <= 0x3b) {
            const uint16_t addr = regs_[op & 15];
            last_ram_addr_ = addr;
            if (a & 1u) ramWrite(addr, static_cast<uint8_t>(S()));
            else {
                ramWrite(addr, static_cast<uint8_t>(S()));
                ramWrite(static_cast<uint16_t>(addr ^ 1u), static_cast<uint8_t>(S() >> 8));
                last_ram_addr_ = addr;
            }
            finish();
            return;
        }
        if (op == 0x3c) {
            regs_[12] = static_cast<uint16_t>(regs_[12] - 1);
            setSZ(regs_[12]);
            regs_[15] = regs_[12] ? regs_[13] : static_cast<uint16_t>(regs_[15] + 1);
            clearPrefixes();
            return;
        }
        if (op == 0x3d) { sfr_ = static_cast<uint16_t>((sfr_ | F_A1) & ~F_B); regs_[15]++; return; }
        if (op == 0x3e) { sfr_ = static_cast<uint16_t>((sfr_ | F_A2) & ~F_B); regs_[15]++; return; }
        if (op == 0x3f) { sfr_ = static_cast<uint16_t>((sfr_ | F_A1 | F_A2) & ~F_B); regs_[15]++; return; }
        if (op >= 0x40 && op <= 0x4b) {
            const uint16_t addr = regs_[op & 15];
            last_ram_addr_ = addr;
            uint16_t v = ramRead(addr);
            if (!(a & 1u)) v |= uint16_t(ramRead(static_cast<uint16_t>(addr ^ 1u))) << 8;
            setD(v); finishResult(); return;
        }
        if (op == 0x4c) {
            if (a & 1u) {
                const uint16_t v = readPixel(static_cast<uint8_t>(regs_[1]), static_cast<uint8_t>(regs_[2]));
                setD(v); zero_ = (v == 0); sign_ = false; finishResult();
            } else {
                plotPixel(static_cast<uint8_t>(regs_[1]), static_cast<uint8_t>(regs_[2])); finish();
            }
            return;
        }
        if (op == 0x4d) { const uint16_t v=uint16_t((S()<<8)|(S()>>8)); setD(v); setSZ(v); finishResult(); return; }
        if (op == 0x4e) {
            if (a & 1u) { por_ = static_cast<uint8_t>(S() & 0x1f); refreshPixelDerivedState(); }
            else {
                loadPlotColor(static_cast<uint8_t>(S()));
            }
            finish(); return;
        }
        if (op == 0x4f) { const uint16_t v=uint16_t(~S()); setD(v); setSZ(v); finishResult(); return; }
        if (op == 0x90) {
            ramWrite(last_ram_addr_, static_cast<uint8_t>(S()));
            ramWrite(static_cast<uint16_t>(last_ram_addr_ ^ 1u), static_cast<uint8_t>(S() >> 8));
            finish(); return;
        }
        if (op >= 0x91 && op <= 0x94) { regs_[11]=static_cast<uint16_t>(regs_[15]+(op-0x90)); finish(); return; }
        if (op == 0x95) { const uint16_t v=uint16_t(int16_t(int8_t(S()))); setD(v); setSZ(v); finishResult(); return; }
        if (op == 0x96) {
            const int16_t sv=int16_t(S()); carry_=(sv&1)!=0;
            const uint16_t v=(a&1u)&&sv==-1?0:uint16_t(sv>>1); setD(v); setSZ(v); finishResult(); return;
        }
        if (op == 0x97) { const uint16_t old=S(); const uint16_t v=uint16_t((old>>1)|(carry_?0x8000:0)); carry_=(old&1)!=0; setD(v); setSZ(v); finishResult(); return; }
        if (op >= 0x98 && op <= 0x9d) {
            const uint16_t jump=regs_[op&15];
            if (a&1u) { pbr_=static_cast<uint8_t>(regs_[op&15]&0x7f); regs_[15]=S(); invalidateCache(); cbr_=regs_[15]&0xfff0; }
            else regs_[15]=jump;
            clearPrefixes(); return;
        }
        if (op == 0x9e) { const uint16_t v=uint8_t(S()); setD(v); sign_=(v&0x80)!=0; zero_=(v==0); finishResult(); return; }
        if (op == 0x9f) {
            const int32_t product=int32_t(int16_t(S()))*int32_t(int16_t(regs_[6])); const uint32_t bits=uint32_t(product);
            if (a&1u) regs_[4]=uint16_t(bits);
            const uint16_t hi=uint16_t(bits>>16); setD(hi); setSZ(hi); carry_=(bits&0x8000)!=0; finishResult(); return;
        }
        if (op >= 0xa0 && op <= 0xaf) {
            const unsigned r=op&15;
            if (a==0) {
                const int8_t imm=static_cast<int8_t>(pipe_); regs_[15]++; fetchPipe(); regs_[r]=uint16_t(int16_t(imm)); if (r == 14) scheduleRomRead(); clearPrefixes(); if (r != 15) regs_[15]++; return;
            }
            const uint16_t addr=static_cast<uint16_t>(uint16_t(pipe_)<<1); regs_[15]++; fetchPipe();
            if (a==2) {
                const uint16_t value=regs_[r]; ramWrite(addr, uint8_t(value)); ramWrite(uint16_t(addr^1u), uint8_t(value>>8)); last_ram_addr_=addr;
            } else {
                last_ram_addr_=addr; regs_[r]=uint16_t(ramRead(addr)|(uint16_t(ramRead(uint16_t(addr^1u)))<<8)); if (r == 14) scheduleRomRead();
            }
            clearPrefixes(); if (!(a != 2 && r == 15)) regs_[15]++; return;
        }
        if (op >= 0xd0 && op <= 0xde) { const unsigned r=op&15; regs_[r]++; if (r == 14) scheduleRomRead(); setSZ(regs_[r]); finish(); return; }
        if (op == 0xdf) {
            if (a>=2) {
                if (a==2) {
                    assert(!ram_write_pending_ && !ram_write_queued_ && "SuperFX RAMB retired before RAM buffer drained");
                    rambr_ = static_cast<uint8_t>(S() & ramBankMask());
                }
                else {
                    assert(!rom_read_pending_ && "SuperFX ROMB retired before R14 transfer completed");
                    rombr_=static_cast<uint8_t>(S()&0x7f);
                }
            } else {
                loadPlotColor(readRomBuffer());
            }
            finish(); return;
        }
        if (op >= 0xe0 && op <= 0xee) { const unsigned r=op&15; regs_[r]--; if (r == 14) scheduleRomRead(); setSZ(regs_[r]); finish(); return; }
        if (op == 0xef) {
            const uint8_t b=readRomBuffer(); uint16_t v=0;
            if (a==0) v=b;
            else if (a==1) v=uint16_t((S()&0x00ff)|(uint16_t(b)<<8));
            else if (a==2) v=uint16_t((S()&0xff00)|b);
            else v=uint16_t(int16_t(int8_t(b)));
            setD(v); finishResult(); return;
        }
        if (op >= 0xf0) {
            const unsigned r=op&15;
            if (a==0) {
                const uint8_t lo=pipe_; regs_[15]++; fetchPipe(); const uint8_t hi=pipe_; regs_[15]++; fetchPipe(); regs_[r]=uint16_t(lo|(uint16_t(hi)<<8)); if (r == 14) scheduleRomRead(); clearPrefixes(); if (r != 15) regs_[15]++; return;
            }
            const uint8_t lo=pipe_; regs_[15]++; fetchPipe(); const uint8_t hi=pipe_; regs_[15]++; fetchPipe(); const uint16_t addr=uint16_t(lo|(uint16_t(hi)<<8));
            if (a==2) {
                const uint16_t value=regs_[r]; ramWrite(addr,uint8_t(value)); ramWrite(uint16_t(addr^1u),uint8_t(value>>8)); last_ram_addr_=addr;
            } else {
                last_ram_addr_=addr; regs_[r]=uint16_t(ramRead(addr)|(uint16_t(ramRead(uint16_t(addr^1u)))<<8)); if (r == 14) scheduleRomRead();
            }
            clearPrefixes(); if (!((a == 0 || a == 1 || a == 3) && r == 15)) regs_[15]++; return;
        }
        running_=false;
        clearPrefixes();
    }
    const std::vector<uint8_t>& rom_;
    std::vector<uint8_t> ram_;
    std::array<uint16_t,16> regs_{};
    std::array<uint8_t,512> cache_{};
    uint32_t cache_valid_lines_ = 0;
    uint16_t sfr_=0, cbr_=0, last_ram_addr_=0;
    uint8_t pbr_=0, rombr_=0, rambr_=0, bramr_=0, cfgr_=0, scbr_=0, clsr_=0, scmr_=0, colr_=0, por_=0;
    uint8_t pixel_planes_=2, screen_height_mode_=0, pixel_tile_bytes_=16;
    size_t screen_base_=0;
    uint8_t pipe_=1, rom_buffer_=0, register_latch_=0;
    bool pipe_fetch_cached_=true;
    bool rom_read_pending_=false;
    uint8_t rom_delay_=0;
    bool ram_write_pending_=false;
    uint8_t ram_delay_=0;
    uint16_t ram_write_address_=0;
    uint8_t ram_write_value_=0;
    bool ram_write_queued_=false;
    uint16_t ram_write_queued_address_=0;
    uint8_t ram_write_queued_value_=0;
    PixelCache primary_pixel_cache_{}, secondary_pixel_cache_{};
    unsigned src_=0, dst_=0;
    bool carry_=false, sign_=false, zero_=false, overflow_=false, running_=false;
    GSURevision revision_ = GSURevision::GSU1;
    uint32_t program_fetch_debt_cycles_ = 0;
    uint32_t pixel_bus_start_delay_ = 0;
    uint32_t pixel_bus_cycles_remaining_ = 0;
    uint32_t pixel_bus_slice_remaining_ = 0;
    std::array<PixelBusOp, kPixelBusQueueCapacity> pixel_bus_ops_{};
    uint8_t pixel_bus_head_ = 0;
    uint8_t pixel_bus_count_ = 0;
    uint8_t pixel_bus_merge_latch_ = 0;
    uint8_t pixel_bus_rpix_result_ = 0;
    bool pixel_bus_rpix_result_valid_ = false;
    bool pixel_bus_waiting_for_ran_ = false;
    bool cache_fill_active_ = false;
    uint8_t cache_fill_bank_ = 0;
    uint16_t cache_fill_source_pc_ = 0;
    uint16_t cache_fill_line_start_ = 0;
    uint8_t cache_fill_next_byte_ = 0;
    uint8_t cache_fill_cycle_credit_ = 0;
    uint64_t clock_credit_ = 0;
    uint64_t clocks_until_step_ = 0;
    std::array<std::array<uint8_t, 16>, 8> dma_registers_{};
    size_t rom_mask_ = 0;
    size_t ram_mask_ = 0;
    std::vector<uint8_t>* cartridge_ram_ = nullptr;
    bool* cartridge_ram_dirty_ = nullptr;
};
}
std::unique_ptr<CartridgeDevice> createGSUDevice(const std::vector<uint8_t>& rom, std::size_t expansionRamBytes, const GSUTimingProfile& profile,
                                                 std::vector<uint8_t>* cartridgeRam, bool* cartridgeRamDirty) {
    return std::make_unique<SuperFXDevice>(rom, expansionRamBytes, profile.revision,
                                           cartridgeRam, cartridgeRamDirty);
}
std::unique_ptr<CartridgeDevice> createSuperFXDevice(const std::vector<uint8_t>& rom, std::size_t expansionRamBytes) {
    static constexpr GSUTimingProfile kGSU1Profile{GSURevision::GSU1};
    return createGSUDevice(rom, expansionRamBytes, kGSU1Profile, nullptr, nullptr);
}

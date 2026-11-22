#include "snes.hpp"
#include "archive_loader.hpp"
#include "state_io.hpp"
#include <algorithm>
#include <iostream>
#include <fstream>
#include <limits>
#include <chrono>
namespace {
constexpr uint64_t DRAM_REFRESH_MASTER_CLOCKS = static_cast<uint64_t>(SNES::DRAM_REFRESH_MASTER_CLOCKS);
constexpr uint64_t APU_TIME_FRAC_BITS = 16;
constexpr uint64_t APU_TIME_ONE = uint64_t{1} << APU_TIME_FRAC_BITS;
constexpr uint64_t NTSC_MASTER_CLOCKS_PER_APU_CYCLE_FP =
    (945000000ULL << APU_TIME_FRAC_BITS) / (44ULL * 1024000ULL);
constexpr uint64_t PAL_MASTER_CLOCKS_PER_APU_CYCLE_FP =
    (21281370ULL << APU_TIME_FRAC_BITS) / 1024000ULL;
constexpr uint64_t apuTimeCeil(uint64_t fixedPointTime) {
    return (fixedPointTime + APU_TIME_ONE - 1) >> APU_TIME_FRAC_BITS;
}
uint64_t fnv1a64(const uint8_t* data, std::size_t size) {
    uint64_t hash = 1469598103934665603ull;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 1099511628211ull;
    }
    return hash;
}
void appendU64LE(std::vector<uint8_t>& data, uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) data.push_back(static_cast<uint8_t>(value >> (i * 8)));
}
uint64_t readU64LE(const uint8_t* data) {
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= static_cast<uint64_t>(data[i]) << (i * 8);
    return value;
}
}
SNES::SNES() {
    connectSystems();
}
SNES::~SNES() = default;
void SNES::connectSystems() {
    bus_.attachCPU(&cpu_);
    bus_.attachPPU(&ppu_);
    bus_.attachAPU(&apu_);
    bus_.attachDMA(&dma_);
    bus_.attachCartridge(&cart_);
    bus_.attachController1(&controller1_);
    bus_.attachController2(&controller2_);
    cpu_.attachBus(&bus_);
    cpu_.setCycleStepping(true);
    cpu_.setInternalCycleCoalescing(true);
    ppu_.attachBus(&bus_);
    apu_.attachBus(&bus_);
    dma_.attachBus(&bus_);
    scheduler_.attachCPU(&cpu_);
    scheduler_.attachPPU(&ppu_);
    scheduler_.attachAPU(&apu_);
    scheduler_.attachDMA(&dma_);
}
bool SNES::loadROM(const std::string& path) {
    bool loaded = false;
    if (ArchiveLoader::isArchivePath(path)) {
        ArchivedRom archivedRom;
        std::string error;
        if (!ArchiveLoader::loadFirstRom(path, archivedRom, error)) {
            std::cerr << "[SNES] Failed to load archive: " << path << " (" << error << ")" << std::endl;
            return false;
        }
        loaded = cart_.loadData(std::move(archivedRom.data), path, archivedRom.entryName);
        if (loaded) {
        }
    } else {
        loaded = cart_.load(path);
    }
    if (!loaded) {
        std::cerr << "[SNES] Failed to load ROM: " << path << std::endl;
        return false;
    }
    powerCycle();
    return true;
}
void SNES::powerCycle() {
    cart_.reset();
    bus_.power();
    ppu_.power();
    ppu_.setPalRegion(cart_.isPalRegion());
    apu_.power();
    dma_.reset();
    scheduler_.reset();
    controller1_.reset();
    controller2_.reset();
    cpu_.reset();
    frame_count_ = 0;
    next_cpu_time_ = 0;
    next_ppu_dot_time_ = static_cast<uint64_t>(ppu_.masterClocksThisDot());
    master_clocks_per_apu_cycle_fp_ = ppu_.palRegion()
        ? PAL_MASTER_CLOCKS_PER_APU_CYCLE_FP
        : NTSC_MASTER_CLOCKS_PER_APU_CYCLE_FP;
    next_apu_time_fp_ = master_clocks_per_apu_cycle_fp_;
    next_apu_time_ = apuTimeCeil(next_apu_time_fp_);
    next_dma_time_ = std::numeric_limits<uint64_t>::max();
    next_hdma_time_ = std::numeric_limits<uint64_t>::max();
    paused_dma_remaining_ = 0;
    paused_cpu_remaining_ = 0;
    pending_hdma_line_ = false;
    pending_hdma_mask_ = 0;
    pending_hdma_trigger_clock_ = 0;
    mdma_cpu_remaining_ = 0;
    mdma_cpu_paused_ = false;
    hdma_nested_in_mdma_ = false;
    dram_refresh_count_ = 0;
    next_dram_refresh_time_ = 538;
    current_line_start_time_ = 0;
    next_timer_irq_time_ = 2;
    seen_irq_config_generation_ = bus_.irqConfigGeneration();
    bus_.setMasterClock(scheduler_.now());
    bus_.updateVideoTiming(ppu_.scanline(), ppu_.dot(), ppu_.hblank(), ppu_.vblank());
}
void SNES::reset() {
    cart_.reset();
    bus_.reset();
    ppu_.reset();
    ppu_.setPalRegion(cart_.isPalRegion());
    apu_.reset();
    dma_.reset();
    scheduler_.reset();
    controller1_.reset();
    controller2_.reset();
    cpu_.reset();
    frame_count_ = 0;
    next_cpu_time_ = 0;
    next_ppu_dot_time_ = static_cast<uint64_t>(ppu_.masterClocksThisDot());
    master_clocks_per_apu_cycle_fp_ = ppu_.palRegion()
        ? PAL_MASTER_CLOCKS_PER_APU_CYCLE_FP
        : NTSC_MASTER_CLOCKS_PER_APU_CYCLE_FP;
    next_apu_time_fp_ = master_clocks_per_apu_cycle_fp_;
    next_apu_time_ = apuTimeCeil(next_apu_time_fp_);
    next_dma_time_ = std::numeric_limits<uint64_t>::max();
    next_hdma_time_ = std::numeric_limits<uint64_t>::max();
    paused_dma_remaining_ = 0;
    paused_cpu_remaining_ = 0;
    pending_hdma_line_ = false;
    pending_hdma_mask_ = 0;
    pending_hdma_trigger_clock_ = 0;
    mdma_cpu_remaining_ = 0;
    mdma_cpu_paused_ = false;
    hdma_nested_in_mdma_ = false;
    dram_refresh_count_ = 0;
    next_dram_refresh_time_ = 538;
    current_line_start_time_ = 0;
    next_timer_irq_time_ = 2;
    seen_irq_config_generation_ = bus_.irqConfigGeneration();
    bus_.setMasterClock(scheduler_.now());
    bus_.updateVideoTiming(ppu_.scanline(), ppu_.dot(), ppu_.hblank(), ppu_.vblank());
}
void SNES::scheduleTimerIrqForCurrentLine(uint64_t ) {
}
void SNES::refreshTimerIrqSchedule(uint64_t ) {
    seen_irq_config_generation_ = bus_.irqConfigGeneration();
}
void SNES::runCycles(uint64_t master_cycles) {
    if (master_cycles == 0) {
        return;
    }
    const uint64_t target = scheduler_.now() + master_cycles;
    const bool scheduler_has_callbacks = scheduler_.hasPendingEvents();
    uint64_t local_now = scheduler_.now();
    const bool clock_cartridge = cart_.hardwareNeedsClock();
    uint64_t pending_cartridge_clocks = 0;
    auto cpu_bus_access_can_skip_cartridge_sync = [](uint32_t address) {
        address &= 0xFFFFFFu;
        const uint8_t bank = static_cast<uint8_t>(address >> 16);
        const uint16_t addr = static_cast<uint16_t>(address);
        if (bank == 0x7E || bank == 0x7F) return true;
        const bool system_bank = (bank & 0x40u) == 0;
        if (!system_bank) return false;
        if (addr < 0x2000) return true;
        if (addr >= 0x2100 && addr <= 0x2183) return true;
        if (addr == 0x4016 || addr == 0x4017) return true;
        if (addr >= 0x4200 && addr <= 0x437F) return true;
        return false;
    };
    auto begin_cartridge_host_access = [&]() {
        if (!cart_.hasEnhancementHardware() || !cpu_.hasPendingCycles()) return;
        CPU::PendingCycleInfo pending{};
        if (!cpu_.peekNextCycle(pending) || pending.kind == CPU::PendingCycleInfo::Kind::Internal) return;
        cart_.beginHardwareHostAccess(pending.address, pending.kind == CPU::PendingCycleInfo::Kind::Write, pending.master_clocks);
    };
    auto update_video_timing_if_needed = [&]() {
        const bool vblankBoundary = ppu_.dot() == 0 &&
            (ppu_.scanline() == 0 || ppu_.scanline() == ppu_.vblankStartLine());
        if (!vblankBoundary && !bus_.auto_joy_active_ && !bus_.auto_joy_pending_ &&
            !controller1_.lightGunActive() && !controller2_.lightGunActive()) {
            return;
        }
        bus_.updateVideoTiming(ppu_.scanline(), ppu_.dot(), ppu_.hblank(), ppu_.vblank());
    };
    auto flush_cartridge = [&]() {
        if (!clock_cartridge || pending_cartridge_clocks == 0) return;
        const bool irq = cart_.clockHardware(pending_cartridge_clocks);
        pending_cartridge_clocks = 0;
        bus_.setCartridgeIrq(irq);
    };
    refreshTimerIrqSchedule(local_now);
    auto service_timer_circuit_through = [&](uint64_t through) {
        while (next_timer_irq_time_ <= through) {
            const uint64_t tick_time = next_timer_irq_time_;
            bus_.setMasterClock(tick_time);
            const uint64_t line_clock = tick_time >= current_line_start_time_
                ? tick_time - current_line_start_time_
                : 0;
            bus_.processNmiCircuitTick(static_cast<uint16_t>(line_clock),
                                       static_cast<uint16_t>(ppu_.scanline()));
            bus_.processTimerIrqCircuitTick(static_cast<uint16_t>(line_clock),
                                            static_cast<uint16_t>(ppu_.scanline()));
            next_timer_irq_time_ = tick_time + 4;
        }
    };
    auto arm_mdma_ownership = [&](uint64_t at) {
        if (!dma_.dmaActive() || next_dma_time_ != std::numeric_limits<uint64_t>::max()) return;
        if (!mdma_cpu_paused_) {
            uint64_t remaining = 0;
            if (next_cpu_time_ != std::numeric_limits<uint64_t>::max() && next_cpu_time_ > at) {
                remaining = next_cpu_time_ - at;
            } else if (cpu_.hasPendingCycles()) {
                remaining = cpu_.nextCycleDelay();
            }
            mdma_cpu_remaining_ = remaining;
            mdma_cpu_paused_ = true;
            uint8_t resume_speed = 8;
            if (remaining == 6 || remaining == 8 || remaining == 12) {
                resume_speed = static_cast<uint8_t>(remaining);
            } else if (cpu_.hasPendingCycles()) {
                const uint32_t next = cpu_.nextCycleDelay();
                if (next == 6 || next == 8 || next == 12) resume_speed = static_cast<uint8_t>(next);
            }
            dma_.setResumeCpuSpeed(resume_speed);
            next_cpu_time_ = std::numeric_limits<uint64_t>::max();
        }
        next_dma_time_ = at + dma_.dmaNextDelay();
    };
    auto begin_hdma_window = [&](uint64_t at, bool init, uint8_t captured_mask) {
        if (dma_.hdmaActive()) return;
        uint64_t cpu_remaining = 0;
        if (next_cpu_time_ != std::numeric_limits<uint64_t>::max() && next_cpu_time_ > at) {
            cpu_remaining = next_cpu_time_ - at;
        } else if (cpu_.hasPendingCycles()) {
            cpu_remaining = cpu_.nextCycleDelay();
        }
        uint8_t resume_speed = 8;
        if (cpu_remaining == 6 || cpu_remaining == 8 || cpu_remaining == 12) {
            resume_speed = static_cast<uint8_t>(cpu_remaining);
        } else if (cpu_.hasPendingCycles()) {
            const uint32_t next = cpu_.nextCycleDelay();
            if (next == 6 || next == 8 || next == 12) resume_speed = static_cast<uint8_t>(next);
        }
        if (init) dma_.startHDMAInit(at, resume_speed);
        else dma_.startHDMALineCaptured(captured_mask, at, resume_speed);
        if (!dma_.hdmaActive()) return;
        hdma_nested_in_mdma_ = dma_.dmaActive();
        if (!hdma_nested_in_mdma_) {
            paused_cpu_remaining_ = cpu_remaining;
            next_cpu_time_ = std::numeric_limits<uint64_t>::max();
        } else {
            paused_cpu_remaining_ = 0;
        }
        if (dma_.dmaActive() && next_dma_time_ != std::numeric_limits<uint64_t>::max()) {
            paused_dma_remaining_ = next_dma_time_ > at ? next_dma_time_ - at : 0;
            next_dma_time_ = std::numeric_limits<uint64_t>::max();
        } else {
            paused_dma_remaining_ = 0;
        }
        next_hdma_time_ = at + dma_.hdmaNextDelay();
    };
    while (local_now < target) {
        const uint64_t now = local_now;
        if (next_timer_irq_time_ <= now) {
            service_timer_circuit_through(now);
        }
        if (next_dram_refresh_time_ <= now) {
            if (dma_.hdmaActive() && next_hdma_time_ != std::numeric_limits<uint64_t>::max()) {
                next_hdma_time_ += DRAM_REFRESH_MASTER_CLOCKS;
            } else if (dma_.dmaActive() && next_dma_time_ != std::numeric_limits<uint64_t>::max()) {
                next_dma_time_ += DRAM_REFRESH_MASTER_CLOCKS;
            } else {
                next_cpu_time_ = (next_cpu_time_ > now ? next_cpu_time_ : now) + DRAM_REFRESH_MASTER_CLOCKS;
            }
            ++dram_refresh_count_;
            const bool odd_field = (ppu_.frameCounter() & 1u) != 0;
            const uint64_t next_line_start = current_line_start_time_ +
                static_cast<uint64_t>(ppu_.masterClocksForLine(ppu_.scanline(), odd_field));
            const uint64_t ideal = next_line_start + 536u;
            const uint64_t c0 = next_dram_refresh_time_ + 1360u;
            const uint64_t c1 = next_dram_refresh_time_ + 1368u;
            const uint64_t d0 = c0 > ideal ? c0 - ideal : ideal - c0;
            const uint64_t d1 = c1 > ideal ? c1 - ideal : ideal - c1;
            next_dram_refresh_time_ = d0 <= d1 ? c0 : c1;
        }
        arm_mdma_ownership(now);
        if (next_ppu_dot_time_ <= now) {
            ppu_.tick();
            update_video_timing_if_needed();
            if (ppu_.dot() == 0) {
                current_line_start_time_ = now;
                if (next_timer_irq_time_ == std::numeric_limits<uint64_t>::max()) {
                    scheduleTimerIrqForCurrentLine(now);
                }
            }
            if (ppu_.dot() == PPU::HDMA_INIT_DOT && ppu_.scanline() == 0)
                begin_hdma_window(now, true, dma_.hdmaEnable());
            if (ppu_.dot() == PPU::HDMA_START_DOT && ppu_.scanline() < ppu_.vblankStartLine()) {
                const uint8_t captured_mask = dma_.hdmaEnable();
                const bool cpu_cycle_in_flight = !dma_.dmaActive() && cpu_.hasPendingCycles() &&
                    next_cpu_time_ != std::numeric_limits<uint64_t>::max() && next_cpu_time_ > now;
                if (cpu_cycle_in_flight) {
                    pending_hdma_line_ = true;
                    pending_hdma_mask_ = captured_mask;
                    pending_hdma_trigger_clock_ = now;
                } else {
                    begin_hdma_window(now, false, captured_mask);
                }
            }
            if (ppu_.dot() == 0 && ppu_.scanline() == ppu_.vblankStartLine()) dma_.deactivateHDMAForVBlank();
            next_ppu_dot_time_ += static_cast<uint64_t>(ppu_.masterClocksThisDot());
            continue;
        }
        if (dma_.hdmaActive() && next_hdma_time_ <= now) {
            flush_cartridge();
            bus_.setMasterClock(now);
            const uint64_t delay = dma_.stepHDMA();
            if (dma_.hdmaActive()) {
                next_hdma_time_ = now + delay;
            } else {
                next_hdma_time_ = std::numeric_limits<uint64_t>::max();
                if (hdma_nested_in_mdma_) {
                    if (dma_.dmaActive())
                        next_dma_time_ = now + dma_.dmaResumeDelayAfterHDMA(paused_dma_remaining_);
                    next_cpu_time_ = std::numeric_limits<uint64_t>::max();
                } else {
                    next_cpu_time_ = now + paused_cpu_remaining_;
                }
                hdma_nested_in_mdma_ = false;
                paused_cpu_remaining_ = 0;
                paused_dma_remaining_ = 0;
            }
            continue;
        }
        if (!dma_.hdmaActive() && dma_.dmaActive() && next_dma_time_ <= now) {
            flush_cartridge();
            bus_.setMasterClock(now);
            const uint64_t delay = dma_.stepDMA();
            if (dma_.dmaActive()) {
                next_dma_time_ = now + delay;
            } else {
                next_dma_time_ = std::numeric_limits<uint64_t>::max();
                if (mdma_cpu_paused_) {
                    next_cpu_time_ = now + mdma_cpu_remaining_;
                    mdma_cpu_remaining_ = 0;
                    mdma_cpu_paused_ = false;
                } else if (next_cpu_time_ < now) {
                    next_cpu_time_ = now;
                }
            }
            continue;
        }
        if (apu_.nextPortEventMasterClock() <= now) {
            bus_.setMasterClock(now);
            apu_.synchronizePorts(now);
            continue;
        }
        if (next_apu_time_ <= now) {
            apu_.beginScheduledInstruction(next_apu_time_fp_, master_clocks_per_apu_cycle_fp_);
            const uint8_t apu_cycles = apu_.tick();
            next_apu_time_fp_ += static_cast<uint64_t>(apu_cycles) * master_clocks_per_apu_cycle_fp_;
            next_apu_time_ = apuTimeCeil(next_apu_time_fp_);
            continue;
        }
        if (!dma_.dmaActive() && !dma_.hdmaActive() && next_cpu_time_ <= now) {
            bus_.setMasterClock(now);
            if (cpu_.hasPendingCycles()) {
                CPU::PendingCycleInfo pending{};
                bool cartridge_host_access = false;
                if (!cpu_.peekNextCycle(pending)) {
                    if (cart_.hardwareHostIrqMayChangeWithoutHostAccess()) flush_cartridge();
                } else if (pending.kind != CPU::PendingCycleInfo::Kind::Internal &&
                           !cpu_bus_access_can_skip_cartridge_sync(pending.address)) {
                    const bool isWrite = pending.kind == CPU::PendingCycleInfo::Kind::Write;
                    if (cart_.hardwareHostAccessRequiresClock(pending.address, isWrite)) {
                        flush_cartridge();
                        cartridge_host_access = true;
                    }
                }
                bus_.completeCpuMachineCycle();
                cpu_.completeCycle();
                if (cartridge_host_access && clock_cartridge)
                    bus_.setCartridgeIrq(cart_.hardwareIrqPending());
                if (pending_hdma_line_) {
                    const uint8_t captured_mask = pending_hdma_mask_;
                    pending_hdma_line_ = false;
                    pending_hdma_mask_ = 0;
                    begin_hdma_window(now, false, captured_mask);
                }
                if (!dma_.hdmaActive()) {
                    if (cpu_.hasPendingCycles()) {
                        cpu_.beginMachineCycle();
                        begin_cartridge_host_access();
                    }
                    next_cpu_time_ = cpu_.hasPendingCycles()
                        ? now + cpu_.nextCycleDelay()
                        : now;
                }
                if (dma_.dmaActive() && !dma_.hdmaActive()) arm_mdma_ownership(now);
            } else {
                if (cart_.hardwareHostIrqMayChangeWithoutHostAccess()) flush_cartridge();
                cpu_.step();
                if (cpu_.hasPendingCycles()) {
                    cpu_.beginMachineCycle();
                    begin_cartridge_host_access();
                    next_cpu_time_ = now + cpu_.nextCycleDelay();
                } else {
                    next_cpu_time_ = now + 6;
                }
            }
            refreshTimerIrqSchedule(now);
            continue;
        }
        uint64_t next_event = std::min(target, std::min(next_apu_time_, next_ppu_dot_time_));
        next_event = std::min(next_event, apu_.nextPortEventMasterClock());
        if (scheduler_has_callbacks)
            next_event = std::min(next_event, next_timer_irq_time_);
        next_event = std::min(next_event, next_dram_refresh_time_);
        if (dma_.hdmaActive()) {
            next_event = std::min(next_event, next_hdma_time_);
        } else if (dma_.dmaActive()) {
            next_event = std::min(next_event, next_dma_time_);
        } else {
            next_event = std::min(next_event, next_cpu_time_);
        }
        const uint64_t elapsed = next_event - now;
        if (clock_cartridge) {
            if (scheduler_has_callbacks) {
                pending_cartridge_clocks += elapsed;
                flush_cartridge();
            } else {
                pending_cartridge_clocks += elapsed;
            }
        }
        if (scheduler_has_callbacks) {
            scheduler_.advanceTo(next_event);
            local_now = scheduler_.now();
        } else {
            local_now = next_event;
        }
    }
    if (!scheduler_has_callbacks) scheduler_.advanceToNoEvents(target);
    flush_cartridge();
    bus_.setMasterClock(target);
    refreshTimerIrqSchedule(target);
    service_timer_circuit_through(target);
    if (next_dram_refresh_time_ == target) {
        if (dma_.hdmaActive() && next_hdma_time_ != std::numeric_limits<uint64_t>::max())
            next_hdma_time_ += DRAM_REFRESH_MASTER_CLOCKS;
        else if (dma_.dmaActive() && next_dma_time_ != std::numeric_limits<uint64_t>::max())
            next_dma_time_ += DRAM_REFRESH_MASTER_CLOCKS;
        else
            next_cpu_time_ = (next_cpu_time_ > target ? next_cpu_time_ : target) + DRAM_REFRESH_MASTER_CLOCKS;
        ++dram_refresh_count_;
        const bool odd_field = (ppu_.frameCounter() & 1u) != 0;
        const uint64_t next_line_start = current_line_start_time_ +
            static_cast<uint64_t>(ppu_.masterClocksForLine(ppu_.scanline(), odd_field));
        const uint64_t ideal = next_line_start + 536u;
        const uint64_t c0 = next_dram_refresh_time_ + 1360u;
        const uint64_t c1 = next_dram_refresh_time_ + 1368u;
        const uint64_t d0 = c0 > ideal ? c0 - ideal : ideal - c0;
        const uint64_t d1 = c1 > ideal ? c1 - ideal : ideal - c1;
        next_dram_refresh_time_ = d0 <= d1 ? c0 : c1;
    }
    if (next_ppu_dot_time_ == target) {
        ppu_.tick();
        update_video_timing_if_needed();
        if (ppu_.dot() == 0) {
            current_line_start_time_ = target;
            if (next_timer_irq_time_ == std::numeric_limits<uint64_t>::max()) scheduleTimerIrqForCurrentLine(target);
        }
        if (ppu_.dot() == PPU::HDMA_INIT_DOT && ppu_.scanline() == 0)
            begin_hdma_window(target, true, dma_.hdmaEnable());
        if (ppu_.dot() == PPU::HDMA_START_DOT && ppu_.scanline() < ppu_.vblankStartLine()) {
            const uint8_t captured_mask = dma_.hdmaEnable();
            const bool cpu_cycle_in_flight = !dma_.dmaActive() && cpu_.hasPendingCycles() &&
                next_cpu_time_ != std::numeric_limits<uint64_t>::max() && next_cpu_time_ > target;
            if (cpu_cycle_in_flight) {
                pending_hdma_line_ = true;
                pending_hdma_mask_ = captured_mask;
                pending_hdma_trigger_clock_ = target;
            } else {
                begin_hdma_window(target, false, captured_mask);
            }
        }
        if (ppu_.dot() == 0 && ppu_.scanline() == ppu_.vblankStartLine()) dma_.deactivateHDMAForVBlank();
        next_ppu_dot_time_ += static_cast<uint64_t>(ppu_.masterClocksThisDot());
    }
    if (!dma_.hdmaActive() && dma_.dmaActive() && next_dma_time_ == target) {
        bus_.setMasterClock(target);
        const uint64_t delay = dma_.stepDMA();
        if (dma_.dmaActive()) {
            next_dma_time_ = target + delay;
        } else {
            next_dma_time_ = std::numeric_limits<uint64_t>::max();
            if (mdma_cpu_paused_) {
                next_cpu_time_ = target + mdma_cpu_remaining_;
                mdma_cpu_remaining_ = 0;
                mdma_cpu_paused_ = false;
            } else if (next_cpu_time_ < target) {
                next_cpu_time_ = target;
            }
        }
    }
    if (!dma_.dmaActive() && !dma_.hdmaActive() &&
        next_cpu_time_ == target && cpu_.hasPendingCycles()) {
        bus_.setMasterClock(target);
        bus_.completeCpuMachineCycle();
        cpu_.completeCycle();
        if (pending_hdma_line_) {
            const uint8_t captured_mask = pending_hdma_mask_;
            pending_hdma_line_ = false;
            pending_hdma_mask_ = 0;
            begin_hdma_window(target, false, captured_mask);
        }
        if (!dma_.hdmaActive()) {
            next_cpu_time_ = cpu_.hasPendingCycles()
                ? target + cpu_.nextCycleDelay()
                : target;
        }
        if (dma_.dmaActive() && !dma_.hdmaActive()) arm_mdma_ownership(target);
        refreshTimerIrqSchedule(target);
    }
}
void SNES::runFrame() {
    const uint64_t now = scheduler_.now();
    const bool odd_field = (ppu_.frameCounter() & 1u) != 0;
    uint64_t clocks_to_frame = next_ppu_dot_time_ > now ? next_ppu_dot_time_ - now : 0;
    const bool current_short = !ppu_.palRegion() && !ppu_.interlaceEnabled() && odd_field && ppu_.scanline() == 240;
    for (int dot = ppu_.dot() + 1; dot < ppu_.dotsThisLine(); ++dot) {
        clocks_to_frame += (!current_short && (dot == 323 || dot == 327)) ? 6u : 4u;
    }
    for (int line = ppu_.scanline() + 1; line < ppu_.linesThisFrame(); ++line) {
        clocks_to_frame += static_cast<uint64_t>(ppu_.masterClocksForLine(line, odd_field));
    }
    if (clocks_to_frame != 0) runCycles(clocks_to_frame);
    ++frame_count_;
}
void SNES::setButton(int player, int button, bool pressed) {
    if (player == 1) {
        controller1_.setButton(button, pressed);
    } else if (controller2_.deviceType() == Controller::DeviceType::Multitap && player >= 2 && player <= 5) {
        controller2_.setMultitapButton(player - 2, button, pressed);
    } else if (player == 2) {
        controller2_.setButton(button, pressed);
    }
}
void SNES::setControllerType(int port, Controller::DeviceType type) {
    if (port == 1) controller1_.setDeviceType(type);
    else if (port == 2) controller2_.setDeviceType(type);
}
void SNES::setMultitapButton(int port, int subPort, int button, bool pressed) {
    if (port == 1) controller1_.setMultitapButton(subPort, button, pressed);
    else if (port == 2) controller2_.setMultitapButton(subPort, button, pressed);
}
void SNES::setMultitapConnected(int port, int subPort, bool connected) {
    if (port == 1) controller1_.setMultitapConnected(subPort, connected);
    else if (port == 2) controller2_.setMultitapConnected(subPort, connected);
}
void SNES::addMouseMotion(int port, int dx, int dy) {
    if (port == 1) controller1_.addMouseMotion(dx, dy);
    else if (port == 2) controller2_.addMouseMotion(dx, dy);
}
void SNES::setMouseButton(int port, Controller::MouseButton button, bool pressed) {
    if (port == 1) controller1_.setMouseButton(button, pressed);
    else if (port == 2) controller2_.setMouseButton(button, pressed);
}
void SNES::setSuperScopeAim(int x, int y, bool onScreen) {
    controller2_.setSuperScopeAim(x, y, onScreen);
}
void SNES::setSuperScopeButton(Controller::ScopeButton button, bool pressed) {
    controller2_.setSuperScopeButton(button, pressed);
}
std::vector<uint8_t> SNES::saveState() {
    if (!isLoaded() || scheduler_.hasPendingEvents()) return {};
    uint32_t guard = 0;
    while (!cpu_.checkpointBoundary()) {
        runCycles(1);
        if (++guard > 1000000u || scheduler_.hasPendingEvents()) return {};
    }
    stateio::Writer w;
    const uint32_t magic = 0x53554E53u;
    const uint32_t version = 37;
    w.pod(magic); w.pod(version);
    const uint64_t rom_size = static_cast<uint64_t>(cart_.rom_data_.size());
    const uint64_t rom_fingerprint = fnv1a64(cart_.rom_data_.data(), cart_.rom_data_.size());
    w.pod(rom_size); w.pod(rom_fingerprint); w.pod(cart_.checksum_); w.pod(cart_.checksum_complement_);
    w.pod(cart_.map_mode_); w.pod(cart_.rom_type_); w.pod(cart_.hardware_type_);
    w.pod(frame_count_); w.pod(next_cpu_time_); w.pod(next_ppu_dot_time_);
    w.pod(next_apu_time_); w.pod(next_apu_time_fp_); w.pod(master_clocks_per_apu_cycle_fp_);
    w.pod(next_dma_time_); w.pod(next_hdma_time_); w.pod(paused_dma_remaining_);
    w.pod(paused_cpu_remaining_); w.pod(pending_hdma_line_); w.pod(pending_hdma_mask_); w.pod(pending_hdma_trigger_clock_); w.pod(mdma_cpu_remaining_); w.pod(mdma_cpu_paused_); w.pod(hdma_nested_in_mdma_); w.pod(dram_refresh_count_); w.pod(next_dram_refresh_time_); w.pod(current_line_start_time_);
    w.pod(next_timer_irq_time_); w.pod(seen_irq_config_generation_);
    w.pod(scheduler_.current_time_);
    const CPU::RuntimeState cpu_state = cpu_.runtimeState();
    w.pod(cpu_state.architectural.pc); w.pod(cpu_state.architectural.s); w.pod(cpu_state.architectural.p);
    w.pod(cpu_state.architectural.a); w.pod(cpu_state.architectural.x); w.pod(cpu_state.architectural.y);
    w.pod(cpu_state.architectural.db); w.pod(cpu_state.architectural.d); w.pod(cpu_state.architectural.pb);
    w.pod(cpu_state.architectural.e); w.pod(cpu_state.nmi_transition); w.pod(cpu_state.nmi_pending); w.pod(cpu_state.irq_line);
    w.pod(cpu_state.irq_pending); w.pod(cpu_state.irq_poll_i); w.pod(cpu_state.cycle_nmi_sample);
    w.pod(cpu_state.cycle_irq_sample); w.pod(cpu_state.cycle_i_sample); w.pod(cpu_state.waiting); w.pod(cpu_state.stopped);
    w.pod(cpu_state.cycles); w.pod(cpu_state.last_master_clocks);
    w.vector(bus_.wram_); w.pod(bus_.flat_test_memory_enabled_); w.vector(bus_.flat_test_memory_);
    w.pod(bus_.wram_address_); w.pod(bus_.nmitimen_); w.pod(bus_.wrio_); w.pod(bus_.mdmaen_);
    w.pod(bus_.hdmaen_); w.pod(bus_.memsel_); w.pod(bus_.scpu_revision_); w.pod(bus_.wrmpya_); w.pod(bus_.wrdiv_);
    w.pod(bus_.quotient_); w.pod(bus_.product_remainder_); w.pod(bus_.math_mpy_counter_);
    w.pod(bus_.math_div_counter_); w.pod(bus_.math_shift_);
    w.pod(bus_.master_clock_); w.pod(bus_.open_bus_); w.pod(bus_.open_bus_decay_enabled_); w.pod(bus_.open_bus_decay_threshold_); w.pod(bus_.open_bus_decay_target_); w.array(bus_.open_bus_bit_refresh_clock_); w.pod(bus_.cpu_io_read_bus_); w.pod(bus_.joy_strobe_); w.pod(bus_.auto_joy_strobe_); w.array(bus_.auto_joy_);
    w.pod(bus_.auto_joy_active_); w.pod(bus_.auto_joy_elapsed_clocks_); w.pod(bus_.auto_joy_next_sample_clock_);
    w.pod(bus_.auto_joy_latch_released_); w.pod(bus_.auto_joy_pending_); w.pod(bus_.htime_); w.pod(bus_.vtime_);
    w.pod(bus_.rdnmi_flag_); w.pod(bus_.nmi_line_level_); w.pod(bus_.nmi_last_hclock_); w.pod(bus_.timeup_flag_); w.pod(bus_.timer_irq_active_until_);
    w.pod(bus_.timer_irq_line_due_); w.pod(bus_.timer_irq_line_asserted_); w.pod(bus_.timer_irq_edge_committed_);
    w.pod(bus_.timer_hcounter_); w.pod(bus_.timer_vcounter_); w.pod(bus_.timer_irq_level_);
    w.pod(bus_.timer_irq_need_); w.pod(bus_.timer_irq_enabled_); w.pod(bus_.timer_last_hclock_);
    w.pod(bus_.hblank_); w.pod(bus_.vblank_);
    w.pod(bus_.previous_vblank_); w.pod(bus_.irq_match_latched_); w.pod(bus_.cartridge_irq_);
    w.pod(bus_.irq_config_generation_); w.pod(bus_.last_irq_config_write_);
    w.array(ppu_.framebuffer_); w.pod(ppu_.frame_hires_output_); w.pod(ppu_.frame_overscan_output_); w.array(ppu_.vram_); w.array(ppu_.cgram_);
    w.array(ppu_.oam_); w.array(ppu_.registers_); w.pod(ppu_.inidisp_); w.pod(ppu_.objsel_);
    w.pod(ppu_.bgmode_); w.pod(ppu_.mosaic_); w.array(ppu_.bgsc_); w.pod(ppu_.bg12nba_); w.pod(ppu_.bg34nba_);
    w.array(ppu_.bg_hofs_); w.array(ppu_.bg_vofs_); w.pod(ppu_.mode1_row_generation_); w.array(ppu_.mode1_row_cache_);
    w.pod(ppu_.scroll_latch_); w.pod(ppu_.bghofs_latch_); w.pod(ppu_.m7sel_); w.pod(ppu_.m7a_); w.pod(ppu_.m7b_);
    w.pod(ppu_.m7c_); w.pod(ppu_.m7d_); w.pod(ppu_.m7x_); w.pod(ppu_.m7y_); w.pod(ppu_.m7_hofs_);
    w.pod(ppu_.m7_vofs_); w.pod(ppu_.m7_latch_); w.pod(ppu_.w12sel_); w.pod(ppu_.w34sel_);
    w.pod(ppu_.wobjsel_); w.pod(ppu_.wh0_); w.pod(ppu_.wh1_); w.pod(ppu_.wh2_); w.pod(ppu_.wh3_);
    w.pod(ppu_.wbglog_); w.pod(ppu_.wobjlog_); w.pod(ppu_.tm_); w.pod(ppu_.ts_); w.pod(ppu_.tmw_);
    w.pod(ppu_.tsw_); w.pod(ppu_.cgwsel_); w.pod(ppu_.cgadsub_); w.pod(ppu_.fixed_color_); w.pod(ppu_.setini_);
    w.pod(ppu_.pal_region_); w.pod(ppu_.ppu2_revision_); w.pod(ppu_.ppu1_open_bus_); w.pod(ppu_.ppu2_open_bus_); w.pod(ppu_.inidisp_early_pending_); w.pod(ppu_.inidisp_early_value_); w.pod(ppu_.setini_early_pending_); w.pod(ppu_.setini_early_value_); w.pod(ppu_.vmain_); w.pod(ppu_.vmadd_); w.pod(ppu_.vram_read_latch_);
    w.pod(ppu_.oam_word_address_); w.pod(ppu_.oam_reload_address_); w.pod(ppu_.oam_high_next_);
    w.pod(ppu_.oam_write_latch_); w.pod(ppu_.obj_priority_rotation_); w.pod(ppu_.obj_range_over_);
    w.pod(ppu_.obj_time_over_); w.pod(ppu_.obj_eval_target_line_); w.pod(ppu_.obj_eval_generation_);
    w.pod(ppu_.obj_eval_cache_generation_); w.pod(ppu_.obj_eval_cache_line_); w.pod(ppu_.obj_eval_cache_count_);
    w.array(ppu_.obj_eval_cache_); w.array(ppu_.obj_eval_sliver_mask_); w.array(ppu_.obj_eval_fetched_);
    w.pod(ppu_.obj_ready_line_); w.pod(ppu_.obj_ready_count_); w.array(ppu_.obj_ready_cache_);
    w.array(ppu_.obj_ready_sliver_mask_); w.array(ppu_.obj_ready_fetched_); w.pod(ppu_.obj_eval_active_);
    w.pod(ppu_.obj_eval_scan_index_); w.pod(ppu_.obj_eval_oam_index_); w.pod(ppu_.obj_eval_selected_count_); w.pod(ppu_.obj_sliver_eval_active_);
    w.pod(ppu_.obj_sliver_eval_index_); w.pod(ppu_.obj_sliver_eval_col_); w.pod(ppu_.obj_slivers_fetched_); w.pod(ppu_.cgadd_);
    w.pod(ppu_.cgram_high_next_); w.pod(ppu_.cgram_write_latch_); w.pod(ppu_.cgram_read_high_next_);
    w.pod(ppu_.pipeline_cgram_line_); w.array(ppu_.pipeline_main_cgram_); w.array(ppu_.pipeline_sub_cgram_);
    w.pod(ppu_.hires_dac_line_); w.pod(ppu_.hires_prev_main_color_); w.pod(ppu_.hires_prev_main_color_enable_);
    w.pod(ppu_.hires_prev_math_enable_); w.pod(ppu_.hires_prev_blend_mode_); w.pod(ppu_.hires_prev_color_halve_);
    w.array(ppu_.cgram_rgba_cache_); w.pod(ppu_.line_obj_line_); w.array(ppu_.line_obj_main_); w.array(ppu_.line_obj_sub_);
    w.pod(ppu_.scanline_); w.pod(ppu_.dot_); w.pod(ppu_.dot_start_master_clock_); w.pod(ppu_.frame_counter_); w.pod(ppu_.stat78_field_);
    w.pod(ppu_.latched_hcounter_); w.pod(ppu_.latched_vcounter_); w.pod(ppu_.counters_latched_);
    w.pod(ppu_.hcounter_high_next_); w.pod(ppu_.vcounter_high_next_); w.pod(ppu_.mosaic_vcounter_);
    w.array(apu_.ram_); w.array(apu_.cpu_to_apu_); w.array(apu_.apu_to_cpu_); w.array(apu_.dsp_registers_);
    w.array(apu_.timers_); w.array(apu_.voices_);
    std::vector<int16_t> audio; apu_.audio_queue_.copyTo(audio); w.vector(audio);
    w.pod(apu_.dsp_phase_); w.pod(apu_.dsp_new_kon_); w.pod(apu_.dsp_kon_); w.pod(apu_.dsp_koff_);
    w.pod(apu_.dsp_every_other_sample_); w.pod(apu_.dsp_pmon_); w.pod(apu_.dsp_non_); w.pod(apu_.dsp_eon_);
    w.pod(apu_.dsp_dir_); w.pod(apu_.dsp_srcn_); w.pod(apu_.dsp_dir_addr_); w.pod(apu_.dsp_esa_);
    w.pod(apu_.dsp_echo_enabled_); w.pod(apu_.dsp_endx_buf_); w.pod(apu_.dsp_envx_buf_); w.pod(apu_.dsp_outx_buf_);
    w.pod(apu_.noise_lfsr_); w.pod(apu_.dsp_counter_); w.pod(apu_.noise_sample_); w.pod(apu_.dsp_voice_output_latch_);
    w.array(apu_.echo_history_); w.pod(apu_.echo_history_pos_); w.pod(apu_.echo_offset_); w.pod(apu_.echo_length_);
    w.pod(apu_.echo_pointer_); w.array(apu_.dsp_main_out_); w.array(apu_.dsp_echo_out_); w.array(apu_.dsp_echo_in_);
    w.pod(apu_.dsp_output_left_); w.pod(apu_.pc_); w.pod(apu_.a_); w.pod(apu_.x_); w.pod(apu_.y_);
    w.pod(apu_.sp_); w.pod(apu_.psw_); w.pod(apu_.control_); w.pod(apu_.test_); w.pod(apu_.dsp_address_);
    w.pod(apu_.ram_f8_); w.pod(apu_.ram_f9_); w.pod(apu_.cycles_); w.pod(apu_.cpu_wait_fraction_);
    w.array(apu_.pending_port_writes_); w.pod(apu_.pending_port_write_count_);
    w.array(apu_.pending_port_reads_); w.pod(apu_.pending_port_read_count_); w.pod(apu_.pending_movw_low_);
    w.pod(apu_.sleeping_); w.pod(apu_.stopped_);
    w.array(dma_.registers_); w.array(dma_.hdma_state_); w.pod(dma_.hdma_enable_); w.pod(dma_.dma_active_);
    w.pod(dma_.dma_channel_mask_); w.pod(dma_.dma_channel_); w.pod(dma_.dma_remaining_); w.pod(dma_.dma_pattern_index_);
    w.pod(dma_.dma_phase_); w.pod(dma_.dma_next_delay_); w.pod(dma_.dma_accumulated_clocks_); w.pod(dma_.dma_resume_cpu_speed_); w.pod(dma_.hdma_active_);
    w.pod(dma_.hdma_init_operation_); w.pod(dma_.hdma_channel_); w.pod(dma_.hdma_pattern_index_);
    w.pod(dma_.hdma_indirect_low_); w.pod(dma_.hdma_phase_); w.pod(dma_.hdma_next_delay_);
    w.pod(dma_.hdma_accumulated_clocks_); w.pod(dma_.hdma_operation_mask_); w.pod(dma_.hdma_resume_cpu_speed_); w.pod(dma_.pending_master_clocks_); w.pod(dma_.last_dma_master_clocks_);
    w.pod(dma_.last_hdma_master_clocks_);
    auto writeController = [&](const Controller& c) {
        w.pod(c.device_type_);
        for (bool b : c.buttons_) w.pod(b);
        w.pod(c.shift_reg_); w.pod(c.shift_index_); w.pod(c.strobe_); w.pod(c.io_port_);
        w.array(c.multitap_buttons_); w.array(c.multitap_connected_); w.array(c.multitap_shift_); w.array(c.multitap_index_);
        for (bool b : c.mouse_buttons_) w.pod(b);
        w.pod(c.mouse_dx_accum_); w.pod(c.mouse_dy_accum_); w.pod(c.mouse_sensitivity_);
        w.pod(c.mouse_x_sign_); w.pod(c.mouse_y_sign_); w.pod(c.mouse_shift_); w.pod(c.mouse_bits_read_);
        w.pod(c.scope_fire_); w.pod(c.scope_cursor_); w.pod(c.scope_pause_); w.pod(c.scope_turbo_button_);
        w.pod(c.scope_turbo_); w.pod(c.scope_fire_edge_); w.pod(c.scope_pause_edge_);
        w.pod(c.scope_x_); w.pod(c.scope_y_); w.pod(c.scope_on_screen_); w.pod(c.scope_shift_); w.pod(c.scope_bits_read_);
    };
    writeController(controller1_);
    writeController(controller2_);
    w.vector(cart_.sram_data_); w.vector(cart_.rtc_data_); w.pod(cart_.sram_dirty_); w.pod(cart_.rtc_dirty_);
    const uint32_t hw_tag = cart_.hardwareStateTag();
    const std::vector<uint8_t> hw_state = cart_.saveHardwareState();
    w.pod(hw_tag); w.vector(hw_state);
    std::vector<uint8_t> bytes = w.take();
    appendU64LE(bytes, fnv1a64(bytes.data(), bytes.size()));
    return bytes;
}
bool SNES::loadState(const std::vector<uint8_t>& state) {
    if (!isLoaded() || scheduler_.hasPendingEvents() || state.size() < 24) return false;
    const std::size_t payload_size = state.size() - 8;
    const uint64_t expected_integrity = readU64LE(state.data() + payload_size);
    if (fnv1a64(state.data(), payload_size) != expected_integrity) return false;
    std::vector<uint8_t> payload(state.begin(), state.begin() + static_cast<std::ptrdiff_t>(payload_size));
    stateio::Reader r(payload);
    uint32_t magic = 0, version = 0;
    uint64_t rom_size = 0, rom_fingerprint = 0;
    uint16_t checksum = 0, complement = 0;
    uint8_t map_mode = 0, rom_type = 0;
    CartridgeHardware hardware = CartridgeHardware::None;
    if (!r.pod(magic) || magic != 0x53554E53u || !r.pod(version) || version != 37 ||
        !r.pod(rom_size) || !r.pod(rom_fingerprint) || !r.pod(checksum) || !r.pod(complement) || !r.pod(map_mode) ||
        !r.pod(rom_type) || !r.pod(hardware)) return false;
    if (rom_size != cart_.rom_data_.size() || rom_fingerprint != fnv1a64(cart_.rom_data_.data(), cart_.rom_data_.size()) ||
        checksum != cart_.checksum_ || complement != cart_.checksum_complement_ ||
        map_mode != cart_.map_mode_ || rom_type != cart_.rom_type_ || hardware != cart_.hardware_type_) return false;
    if (!r.pod(frame_count_) || !r.pod(next_cpu_time_) || !r.pod(next_ppu_dot_time_) ||
        !r.pod(next_apu_time_) || !r.pod(next_apu_time_fp_) || !r.pod(master_clocks_per_apu_cycle_fp_) ||
        !r.pod(next_dma_time_) || !r.pod(next_hdma_time_) || !r.pod(paused_dma_remaining_) ||
        !r.pod(paused_cpu_remaining_) || !r.pod(pending_hdma_line_) || !r.pod(pending_hdma_mask_) || !r.pod(pending_hdma_trigger_clock_) || !r.pod(mdma_cpu_remaining_) || !r.pod(mdma_cpu_paused_) || !r.pod(hdma_nested_in_mdma_) || !r.pod(dram_refresh_count_) || !r.pod(next_dram_refresh_time_) || !r.pod(current_line_start_time_) ||
        !r.pod(next_timer_irq_time_) || !r.pod(seen_irq_config_generation_)) return false;
    uint64_t scheduler_time = 0; if (!r.pod(scheduler_time)) return false;
    CPU::RuntimeState cpu_state{};
    if (!r.pod(cpu_state.architectural.pc) || !r.pod(cpu_state.architectural.s) || !r.pod(cpu_state.architectural.p) ||
        !r.pod(cpu_state.architectural.a) || !r.pod(cpu_state.architectural.x) || !r.pod(cpu_state.architectural.y) ||
        !r.pod(cpu_state.architectural.db) || !r.pod(cpu_state.architectural.d) || !r.pod(cpu_state.architectural.pb) ||
        !r.pod(cpu_state.architectural.e) || !r.pod(cpu_state.nmi_transition) || !r.pod(cpu_state.nmi_pending) || !r.pod(cpu_state.irq_line) ||
        !r.pod(cpu_state.irq_pending) || !r.pod(cpu_state.irq_poll_i) || !r.pod(cpu_state.cycle_nmi_sample) ||
        !r.pod(cpu_state.cycle_irq_sample) || !r.pod(cpu_state.cycle_i_sample) || !r.pod(cpu_state.waiting) || !r.pod(cpu_state.stopped) ||
        !r.pod(cpu_state.cycles) || !r.pod(cpu_state.last_master_clocks)) return false;
    if (!r.vector(bus_.wram_, 256 * 1024) || bus_.wram_.size() != 128 * 1024 || !r.pod(bus_.flat_test_memory_enabled_) ||
        !r.vector(bus_.flat_test_memory_, 32 * 1024 * 1024) || !r.pod(bus_.wram_address_) || !r.pod(bus_.nmitimen_) ||
        !r.pod(bus_.wrio_) || !r.pod(bus_.mdmaen_) || !r.pod(bus_.hdmaen_) || !r.pod(bus_.memsel_) ||
        !r.pod(bus_.scpu_revision_) || !r.pod(bus_.wrmpya_) || !r.pod(bus_.wrdiv_) || !r.pod(bus_.quotient_) || !r.pod(bus_.product_remainder_) ||
        !r.pod(bus_.math_mpy_counter_) || !r.pod(bus_.math_div_counter_) || !r.pod(bus_.math_shift_) || !r.pod(bus_.master_clock_) || !r.pod(bus_.open_bus_) || !r.pod(bus_.open_bus_decay_enabled_) || !r.pod(bus_.open_bus_decay_threshold_) || !r.pod(bus_.open_bus_decay_target_) || !r.array(bus_.open_bus_bit_refresh_clock_) || !r.pod(bus_.cpu_io_read_bus_) || !r.pod(bus_.joy_strobe_) || !r.pod(bus_.auto_joy_strobe_) ||
        !r.array(bus_.auto_joy_) || !r.pod(bus_.auto_joy_active_) || !r.pod(bus_.auto_joy_elapsed_clocks_) ||
        !r.pod(bus_.auto_joy_next_sample_clock_) || !r.pod(bus_.auto_joy_latch_released_) || !r.pod(bus_.auto_joy_pending_) ||
        !r.pod(bus_.htime_) || !r.pod(bus_.vtime_) || !r.pod(bus_.rdnmi_flag_) || !r.pod(bus_.nmi_line_level_) || !r.pod(bus_.nmi_last_hclock_) || !r.pod(bus_.timeup_flag_) ||
        !r.pod(bus_.timer_irq_active_until_) || !r.pod(bus_.timer_irq_line_due_) || !r.pod(bus_.timer_irq_line_asserted_) || !r.pod(bus_.timer_irq_edge_committed_) ||
        !r.pod(bus_.timer_hcounter_) || !r.pod(bus_.timer_vcounter_) || !r.pod(bus_.timer_irq_level_) || !r.pod(bus_.timer_irq_need_) || !r.pod(bus_.timer_irq_enabled_) || !r.pod(bus_.timer_last_hclock_) || !r.pod(bus_.hblank_) || !r.pod(bus_.vblank_) || !r.pod(bus_.previous_vblank_) || !r.pod(bus_.irq_match_latched_) ||
        !r.pod(bus_.cartridge_irq_) || !r.pod(bus_.irq_config_generation_) || !r.pod(bus_.last_irq_config_write_)) return false;
    if (!r.array(ppu_.framebuffer_) || !r.pod(ppu_.frame_hires_output_) || !r.pod(ppu_.frame_overscan_output_) || !r.array(ppu_.vram_) || !r.array(ppu_.cgram_) ||
        !r.array(ppu_.oam_) || !r.array(ppu_.registers_) || !r.pod(ppu_.inidisp_) || !r.pod(ppu_.objsel_) ||
        !r.pod(ppu_.bgmode_) || !r.pod(ppu_.mosaic_) || !r.array(ppu_.bgsc_) || !r.pod(ppu_.bg12nba_) ||
        !r.pod(ppu_.bg34nba_) || !r.array(ppu_.bg_hofs_) || !r.array(ppu_.bg_vofs_) || !r.pod(ppu_.mode1_row_generation_) ||
        !r.array(ppu_.mode1_row_cache_) || !r.pod(ppu_.scroll_latch_) || !r.pod(ppu_.bghofs_latch_) ||
        !r.pod(ppu_.m7sel_) || !r.pod(ppu_.m7a_) || !r.pod(ppu_.m7b_) || !r.pod(ppu_.m7c_) || !r.pod(ppu_.m7d_) ||
        !r.pod(ppu_.m7x_) || !r.pod(ppu_.m7y_) || !r.pod(ppu_.m7_hofs_) || !r.pod(ppu_.m7_vofs_) || !r.pod(ppu_.m7_latch_) ||
        !r.pod(ppu_.w12sel_) || !r.pod(ppu_.w34sel_) || !r.pod(ppu_.wobjsel_) || !r.pod(ppu_.wh0_) || !r.pod(ppu_.wh1_) ||
        !r.pod(ppu_.wh2_) || !r.pod(ppu_.wh3_) || !r.pod(ppu_.wbglog_) || !r.pod(ppu_.wobjlog_) || !r.pod(ppu_.tm_) ||
        !r.pod(ppu_.ts_) || !r.pod(ppu_.tmw_) || !r.pod(ppu_.tsw_) || !r.pod(ppu_.cgwsel_) || !r.pod(ppu_.cgadsub_) ||
        !r.pod(ppu_.fixed_color_) || !r.pod(ppu_.setini_) || !r.pod(ppu_.pal_region_) || !r.pod(ppu_.ppu2_revision_) || !r.pod(ppu_.ppu1_open_bus_) || !r.pod(ppu_.ppu2_open_bus_) || !r.pod(ppu_.inidisp_early_pending_) || !r.pod(ppu_.inidisp_early_value_) || !r.pod(ppu_.setini_early_pending_) || !r.pod(ppu_.setini_early_value_) || !r.pod(ppu_.vmain_) ||
        !r.pod(ppu_.vmadd_) || !r.pod(ppu_.vram_read_latch_) || !r.pod(ppu_.oam_word_address_) ||
        !r.pod(ppu_.oam_reload_address_) || !r.pod(ppu_.oam_high_next_) || !r.pod(ppu_.oam_write_latch_) ||
        !r.pod(ppu_.obj_priority_rotation_) || !r.pod(ppu_.obj_range_over_) || !r.pod(ppu_.obj_time_over_) ||
        !r.pod(ppu_.obj_eval_target_line_) || !r.pod(ppu_.obj_eval_generation_) || !r.pod(ppu_.obj_eval_cache_generation_) ||
        !r.pod(ppu_.obj_eval_cache_line_) || !r.pod(ppu_.obj_eval_cache_count_) || !r.array(ppu_.obj_eval_cache_) ||
        !r.array(ppu_.obj_eval_sliver_mask_) || !r.array(ppu_.obj_eval_fetched_) || !r.pod(ppu_.obj_ready_line_) ||
        !r.pod(ppu_.obj_ready_count_) || !r.array(ppu_.obj_ready_cache_) || !r.array(ppu_.obj_ready_sliver_mask_) ||
        !r.array(ppu_.obj_ready_fetched_) || !r.pod(ppu_.obj_eval_active_) || !r.pod(ppu_.obj_eval_scan_index_) ||
        !r.pod(ppu_.obj_eval_oam_index_) || !r.pod(ppu_.obj_eval_selected_count_) || !r.pod(ppu_.obj_sliver_eval_active_) || !r.pod(ppu_.obj_sliver_eval_index_) ||
        !r.pod(ppu_.obj_sliver_eval_col_) || !r.pod(ppu_.obj_slivers_fetched_) || !r.pod(ppu_.cgadd_) || !r.pod(ppu_.cgram_high_next_) ||
        !r.pod(ppu_.cgram_write_latch_) || !r.pod(ppu_.cgram_read_high_next_) || !r.pod(ppu_.pipeline_cgram_line_) ||
        !r.array(ppu_.pipeline_main_cgram_) || !r.array(ppu_.pipeline_sub_cgram_) || !r.pod(ppu_.hires_dac_line_) ||
        !r.pod(ppu_.hires_prev_main_color_) || !r.pod(ppu_.hires_prev_main_color_enable_) ||
        !r.pod(ppu_.hires_prev_math_enable_) || !r.pod(ppu_.hires_prev_blend_mode_) || !r.pod(ppu_.hires_prev_color_halve_) ||
        !r.array(ppu_.cgram_rgba_cache_) ||
        !r.pod(ppu_.line_obj_line_) || !r.array(ppu_.line_obj_main_) || !r.array(ppu_.line_obj_sub_) ||
        !r.pod(ppu_.scanline_) || !r.pod(ppu_.dot_) || !r.pod(ppu_.dot_start_master_clock_) || !r.pod(ppu_.frame_counter_) || !r.pod(ppu_.stat78_field_) ||
        !r.pod(ppu_.latched_hcounter_) || !r.pod(ppu_.latched_vcounter_) || !r.pod(ppu_.counters_latched_) ||
        !r.pod(ppu_.hcounter_high_next_) || !r.pod(ppu_.vcounter_high_next_) || !r.pod(ppu_.mosaic_vcounter_)) return false;
    ppu_.mode1_row_generation_ += 0x100000000ull;
    std::vector<int16_t> audio;
    if (!r.array(apu_.ram_) || !r.array(apu_.cpu_to_apu_) || !r.array(apu_.apu_to_cpu_) || !r.array(apu_.dsp_registers_) ||
        !r.array(apu_.timers_) || !r.array(apu_.voices_) || !r.vector(audio, 4 * 1024 * 1024) ||
        !r.pod(apu_.dsp_phase_) || !r.pod(apu_.dsp_new_kon_) || !r.pod(apu_.dsp_kon_) || !r.pod(apu_.dsp_koff_) ||
        !r.pod(apu_.dsp_every_other_sample_) || !r.pod(apu_.dsp_pmon_) || !r.pod(apu_.dsp_non_) || !r.pod(apu_.dsp_eon_) ||
        !r.pod(apu_.dsp_dir_) || !r.pod(apu_.dsp_srcn_) || !r.pod(apu_.dsp_dir_addr_) || !r.pod(apu_.dsp_esa_) ||
        !r.pod(apu_.dsp_echo_enabled_) || !r.pod(apu_.dsp_endx_buf_) || !r.pod(apu_.dsp_envx_buf_) || !r.pod(apu_.dsp_outx_buf_) ||
        !r.pod(apu_.noise_lfsr_) || !r.pod(apu_.dsp_counter_) || !r.pod(apu_.noise_sample_) || !r.pod(apu_.dsp_voice_output_latch_) ||
        !r.array(apu_.echo_history_) || !r.pod(apu_.echo_history_pos_) || !r.pod(apu_.echo_offset_) || !r.pod(apu_.echo_length_) ||
        !r.pod(apu_.echo_pointer_) || !r.array(apu_.dsp_main_out_) || !r.array(apu_.dsp_echo_out_) || !r.array(apu_.dsp_echo_in_) ||
        !r.pod(apu_.dsp_output_left_) || !r.pod(apu_.pc_) || !r.pod(apu_.a_) || !r.pod(apu_.x_) || !r.pod(apu_.y_) ||
        !r.pod(apu_.sp_) || !r.pod(apu_.psw_) || !r.pod(apu_.control_) || !r.pod(apu_.test_) || !r.pod(apu_.dsp_address_) ||
        !r.pod(apu_.ram_f8_) || !r.pod(apu_.ram_f9_) || !r.pod(apu_.cycles_) || !r.pod(apu_.cpu_wait_fraction_) ||
        !r.array(apu_.pending_port_writes_) || !r.pod(apu_.pending_port_write_count_) ||
        !r.array(apu_.pending_port_reads_) || !r.pod(apu_.pending_port_read_count_) || !r.pod(apu_.pending_movw_low_) ||
        !r.pod(apu_.sleeping_) || !r.pod(apu_.stopped_)) return false;
    if (apu_.pending_port_write_count_ > apu_.pending_port_writes_.size()) return false;
    if (apu_.pending_port_read_count_ > apu_.pending_port_reads_.size()) return false;
    apu_.staged_port_write_count_ = 0;
    apu_.scheduled_instruction_timing_valid_ = false;
    apu_.executing_instruction_ = false;
    if (!apu_.audio_queue_.assign(audio)) return false;
    if (!r.array(dma_.registers_) || !r.array(dma_.hdma_state_) || !r.pod(dma_.hdma_enable_) || !r.pod(dma_.dma_active_) ||
        !r.pod(dma_.dma_channel_mask_) || !r.pod(dma_.dma_channel_) || !r.pod(dma_.dma_remaining_) ||
        !r.pod(dma_.dma_pattern_index_) || !r.pod(dma_.dma_phase_) || !r.pod(dma_.dma_next_delay_) ||
        !r.pod(dma_.dma_accumulated_clocks_) || !r.pod(dma_.dma_resume_cpu_speed_) || !r.pod(dma_.hdma_active_) || !r.pod(dma_.hdma_init_operation_) ||
        !r.pod(dma_.hdma_channel_) || !r.pod(dma_.hdma_pattern_index_) || !r.pod(dma_.hdma_indirect_low_) ||
        !r.pod(dma_.hdma_phase_) || !r.pod(dma_.hdma_next_delay_) || !r.pod(dma_.hdma_accumulated_clocks_) || !r.pod(dma_.hdma_operation_mask_) || !r.pod(dma_.hdma_resume_cpu_speed_) ||
        !r.pod(dma_.pending_master_clocks_) || !r.pod(dma_.last_dma_master_clocks_) || !r.pod(dma_.last_hdma_master_clocks_)) return false;
    auto readController = [&](Controller& c) -> bool {
        if (!r.pod(c.device_type_)) return false;
        for (bool& b : c.buttons_) if (!r.pod(b)) return false;
        if (!r.pod(c.shift_reg_) || !r.pod(c.shift_index_) || !r.pod(c.strobe_) || !r.pod(c.io_port_) ||
            !r.array(c.multitap_buttons_) || !r.array(c.multitap_connected_) || !r.array(c.multitap_shift_) || !r.array(c.multitap_index_)) return false;
        for (bool& b : c.mouse_buttons_) if (!r.pod(b)) return false;
        return r.pod(c.mouse_dx_accum_) && r.pod(c.mouse_dy_accum_) && r.pod(c.mouse_sensitivity_) &&
            r.pod(c.mouse_x_sign_) && r.pod(c.mouse_y_sign_) && r.pod(c.mouse_shift_) && r.pod(c.mouse_bits_read_) &&
            r.pod(c.scope_fire_) && r.pod(c.scope_cursor_) && r.pod(c.scope_pause_) && r.pod(c.scope_turbo_button_) &&
            r.pod(c.scope_turbo_) && r.pod(c.scope_fire_edge_) && r.pod(c.scope_pause_edge_) &&
            r.pod(c.scope_x_) && r.pod(c.scope_y_) && r.pod(c.scope_on_screen_) && r.pod(c.scope_shift_) && r.pod(c.scope_bits_read_);
    };
    if (!readController(controller1_) || !readController(controller2_)) return false;
    std::vector<uint8_t> sram, rtc, hw_state;
    bool sram_dirty = false, rtc_dirty = false;
    uint32_t hw_tag = 0;
    if (!r.vector(sram, 32 * 1024 * 1024) || !r.vector(rtc, 1024 * 1024) || !r.pod(sram_dirty) || !r.pod(rtc_dirty) ||
        !r.pod(hw_tag) || !r.vector(hw_state, 32 * 1024 * 1024) || !r.done()) return false;
    if (sram.size() != cart_.sram_data_.size() || rtc.size() != cart_.rtc_data_.size()) return false;
    if (cart_.device_ != nullptr) {
        if (hw_tag != cart_.hardwareStateTag() || !cart_.loadHardwareState(hw_tag, hw_state)) return false;
    } else if (hw_tag != 0 || !hw_state.empty()) return false;
    cart_.sram_data_ = std::move(sram); cart_.rtc_data_ = std::move(rtc);
    cart_.sram_dirty_ = sram_dirty; cart_.rtc_dirty_ = rtc_dirty;
    cpu_.cycle_events_.clear(); cpu_.micro_active_ = false; cpu_.recording_cycles_ = false;
    if (!cpu_.restoreRuntimeState(cpu_state)) return false;
    scheduler_.reset(); scheduler_.advanceToNoEvents(scheduler_time);
    bus_.setMasterClock(bus_.master_clock_);
    for (auto& cache : ppu_.mode1_row_cache_) cache = PPU::Mode1RowCache{};
    for (auto& cache : ppu_.bg_row_cache_) cache = PPU::BgRowCache{};
    return true;
}
bool SNES::saveStateToFile(const std::string& path) {
    const std::vector<uint8_t> state = saveState();
    if (state.empty()) return false;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;
    out.write(reinterpret_cast<const char*>(state.data()), static_cast<std::streamsize>(state.size()));
    return out.good();
}
bool SNES::loadStateFromFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) return false;
    const std::streamsize size = in.tellg();
    if (size <= 0 || size > static_cast<std::streamsize>(64 * 1024 * 1024)) return false;
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> state(static_cast<std::size_t>(size));
    if (!in.read(reinterpret_cast<char*>(state.data()), size)) return false;
    return loadState(state);
}

#include "cpu.hpp"
#include <cstring>
#include "cpu_alu.hpp"
#include "bus.hpp"
#include <cstdint>
CPU::CPU() { reset(); }
CPU::CPU(Bus& bus_ref) : bus_(&bus_ref) { reset(); }
uint8_t CPU::memoryRead(uint32_t address) const {
    address &= 0xFFFFFFu;
    if (bus_) return bus_->read(address);
    if (external_bus_.read) return external_bus_.read(external_bus_.context, address);
    return 0xFF;
}
void CPU::memoryWrite(uint32_t address, uint8_t value) {
    address &= 0xFFFFFFu;
    if (bus_) { bus_->write(address, value); return; }
    if (external_bus_.write) external_bus_.write(external_bus_.context, address, value);
}
uint8_t CPU::memoryAccessMasterClocks(uint32_t address) const {
    address &= 0xFFFFFFu;
    if (bus_) return bus_->accessMasterClocks(address);
    if (external_bus_.access_master_clocks) return external_bus_.access_master_clocks(external_bus_.context, address);
    return internal_cycle_master_clocks_;
}
CPU::State CPU::state() const {
    State out;
    std::memset(&out, 0, sizeof(out));
    out.pc = pc_;
    out.s = s_;
    out.p = p_;
    out.a = a_;
    out.x = x_;
    out.y = y_;
    out.db = db_;
    out.d = dp_;
    out.pb = pb_;
    out.e = emulation_;
    return out;
}
CPU::RuntimeState CPU::runtimeState() const {
    RuntimeState out;
    std::memset(&out, 0, sizeof(out));
    const State architectural = state();
    out.architectural.pc = architectural.pc;
    out.architectural.s = architectural.s;
    out.architectural.p = architectural.p;
    out.architectural.a = architectural.a;
    out.architectural.x = architectural.x;
    out.architectural.y = architectural.y;
    out.architectural.db = architectural.db;
    out.architectural.d = architectural.d;
    out.architectural.pb = architectural.pb;
    out.architectural.e = architectural.e;
    out.nmi_transition = nmi_transition_;
    out.nmi_pending = nmi_pending_;
    out.irq_line = irq_line_;
    out.irq_pending = irq_pending_;
    out.irq_poll_i = irq_poll_i_;
    out.cycle_nmi_sample = cycle_nmi_sample_;
    out.cycle_irq_sample = cycle_irq_sample_;
    out.cycle_i_sample = cycle_i_sample_;
    out.waiting = waiting_;
    out.stopped = stopped_;
    out.cycles = cycles_;
    out.last_master_clocks = last_master_clocks_;
    return out;
}
bool CPU::restoreRuntimeState(const RuntimeState& in) {
    if (cycle_stepping_ && (!cycle_events_.empty() || micro_active_)) return false;
    setStateForTesting(in.architectural);
    nmi_transition_ = in.nmi_transition;
    nmi_pending_ = in.nmi_pending;
    irq_line_ = in.irq_line;
    irq_pending_ = in.irq_pending;
    irq_poll_i_ = in.irq_poll_i;
    cycle_nmi_sample_ = in.cycle_nmi_sample;
    cycle_irq_sample_ = in.cycle_irq_sample;
    cycle_i_sample_ = in.cycle_i_sample;
    waiting_ = in.waiting;
    stopped_ = in.stopped;
    cycles_ = in.cycles;
    last_master_clocks_ = in.last_master_clocks;
    return true;
}
void CPU::setStateForTesting(const State& in) {
    pc_ = in.pc;
    s_ = in.s;
    p_ = in.p;
    a_ = in.a;
    x_ = in.x;
    y_ = in.y;
    db_ = in.db;
    dp_ = in.d;
    pb_ = in.pb;
    emulation_ = in.e;
    normalizeMode();
    nmi_transition_ = false;
    nmi_pending_ = false;
    irq_line_ = false;
    irq_pending_ = false;
    irq_poll_i_ = flag(Flag_I);
    cycle_nmi_sample_ = false;
    cycle_irq_sample_ = false;
    cycle_i_sample_ = flag(Flag_I);
    legacy_interrupt_poll_pending_ = false;
    machine_cycle_active_ = false;
    waiting_ = false;
    stopped_ = false;
    recording_cycles_ = false;
    recorded_bus_cycles_ = 0;
    cycle_events_.clear();
    micro_active_ = false;
    micro_opcode_ = 0;
    micro_bus_cycles_ = 0;
    micro_master_clocks_ = 0;
    timing_extra_cycles_ = 0;
    page_crossed_ = false;
    last_master_clocks_ = 0;
}
bool CPU::peekNextCycle(PendingCycleInfo& info) const {
    if (cycle_events_.empty()) return false;
    const CycleEvent& event = cycle_events_.front();
    info.master_clocks = event.master_clocks;
    info.address = event.address;
    info.value = event.value;
    switch (event.kind) {
    case CycleKind::Write: info.kind = PendingCycleInfo::Kind::Write; break;
    case CycleKind::Read: info.kind = PendingCycleInfo::Kind::Read; break;
    case CycleKind::Internal: info.kind = PendingCycleInfo::Kind::Internal; break;
    }
    return true;
}
void CPU::setFlag(StatusFlags f, bool set) {
    if (set) p_ |= f; else p_ &= static_cast<uint8_t>(~f);
}
void CPU::normalizeMode() {
    if (emulation_) {
        p_ |= Flag_M | Flag_X;
        s_ = static_cast<uint16_t>(0x0100 | (s_ & 0x00FF));
    }
    if (index8()) {
        x_ &= 0x00FF;
        y_ &= 0x00FF;
    }
}
void CPU::reset() {
    a_ = x_ = y_ = dp_ = 0;
    s_ = 0x01FF;
    pb_ = db_ = 0;
    p_ = Flag_I | Flag_X | Flag_M;
    emulation_ = true;
    nmi_transition_ = nmi_pending_ = irq_line_ = irq_pending_ = waiting_ = stopped_ = false;
    machine_cycle_active_ = false;
    irq_poll_i_ = true;
    cycle_nmi_sample_ = false;
    cycle_irq_sample_ = false;
    cycle_i_sample_ = true;
    legacy_interrupt_poll_pending_ = false;
    cycles_ = 0;
    recording_cycles_ = false;
    recorded_bus_cycles_ = 0;
    cycle_events_.clear();
    micro_active_ = false;
    micro_opcode_ = 0;
    micro_bus_cycles_ = 0;
    micro_master_clocks_ = 0;
    legacy_fallback_count_ = 0;
    pc_ = hasMemoryBus() ? read16(0x00FFFC) : 0;
}
void CPU::recordBusCycle(uint32_t address, bool write, uint8_t value) const {
    if (!cycle_stepping_ || !recording_cycles_ || !hasMemoryBus()) return;
    CycleEvent event;
    event.master_clocks = memoryAccessMasterClocks(address & 0xFFFFFF);
    event.kind = write ? CycleKind::Write : CycleKind::Read;
    event.address = address & 0xFFFFFF;
    event.value = value;
    cycle_events_.push_back(event);
    ++recorded_bus_cycles_;
}
void CPU::finishCycleQueue(uint32_t cpuCycles) {
    if (!cycle_stepping_) return;
    while (recorded_bus_cycles_ < cpuCycles) {
        CycleEvent event;
        event.master_clocks = internal_cycle_master_clocks_;
        cycle_events_.push_back(event);
        ++recorded_bus_cycles_;
    }
    last_master_clocks_ = cycle_events_.totalMasterClocks();
    if (cpuCycles != 0) legacy_interrupt_poll_pending_ = true;
}
void CPU::requestNMI() {
    if (machine_cycle_active_) nmi_transition_ = true;
    else nmi_pending_ = true;
}
void CPU::beginMachineCycle() {
    machine_cycle_active_ = true;
    cycle_nmi_sample_ = nmi_transition_;
    cycle_irq_sample_ = irq_line_;
    cycle_i_sample_ = flag(Flag_I);
}
void CPU::latchInterruptRecognition() {
    if (cycle_nmi_sample_) {
        nmi_pending_ = true;
        nmi_transition_ = false;
    }
    if (cycle_irq_sample_ && !cycle_i_sample_) irq_pending_ = true;
    irq_poll_i_ = cycle_i_sample_;
}
void CPU::completeCycle() {
    if (cycle_events_.empty()) return;
    if (!machine_cycle_active_) beginMachineCycle();
    const CycleEvent event = cycle_events_.front();
    cycle_events_.pop_front();
    if (micro_active_) micro_master_clocks_ += event.master_clocks;
    uint8_t observed_value = event.value;
    PendingCycleInfo::Kind observed_kind = PendingCycleInfo::Kind::Internal;
    if (event.kind == CycleKind::Write && hasMemoryBus()) {
        observed_kind = PendingCycleInfo::Kind::Write;
        memoryWrite(event.address, event.value);
    } else if (event.kind == CycleKind::Read && hasMemoryBus()) {
        observed_kind = PendingCycleInfo::Kind::Read;
        observed_value = memoryRead(event.address);
        if (event.action != CycleAction::None) handleReadAction(event.action, observed_value);
    }
    if (event.kind != CycleKind::Read && event.action != CycleAction::None)
        handlePostCycleAction(event.action);
    if (legacy_interrupt_poll_pending_ && cycle_events_.empty() && !micro_active_) {
        legacy_interrupt_poll_pending_ = false;
        latchInterruptRecognition();
    }
    machine_cycle_active_ = false;
}
void CPU::queueDeferredReadAction(uint32_t address, CycleAction action) {
    if (!hasMemoryBus()) return;
    CycleEvent event;
    event.master_clocks = memoryAccessMasterClocks(address & 0xFFFFFF);
    event.kind = CycleKind::Read;
    event.address = address & 0xFFFFFF;
    event.action = action;
    cycle_events_.push_back(event);
    ++micro_bus_cycles_;
}
void CPU::queueDeferredWriteAction(uint32_t address, uint8_t value, CycleAction action) {
    if (!hasMemoryBus()) return;
    CycleEvent event;
    event.master_clocks = memoryAccessMasterClocks(address & 0xFFFFFF);
    event.kind = CycleKind::Write;
    event.address = address & 0xFFFFFF;
    event.value = value;
    event.action = action;
    cycle_events_.push_back(event);
    ++micro_bus_cycles_;
}
void CPU::queueInternalAction(uint32_t count, CycleAction action) {
    if (count == 0) { handlePostCycleAction(action); return; }
    if (coalesce_internal_cycles_ && count > 1) {
        CycleEvent event;
        event.master_clocks = static_cast<uint8_t>(count * internal_cycle_master_clocks_);
        event.action = action;
        cycle_events_.push_back(event);
        micro_bus_cycles_ += count;
        return;
    }
    for (uint32_t i = 0; i < count; ++i) {
        CycleEvent event;
        event.master_clocks = internal_cycle_master_clocks_;
        if (i + 1 == count) event.action = action;
        cycle_events_.push_back(event);
        ++micro_bus_cycles_;
    }
}
void CPU::startHardwareInterrupt(bool nmi) {
    micro_active_ = true;
    micro_bus_cycles_ = 0;
    micro_master_clocks_ = 0;
    micro_m8_before_ = acc8();
    micro_x8_before_ = index8();
    micro_dp_before_ = dp_;
    waiting_ = false;
    if (nmi) nmi_pending_ = false;
    else irq_pending_ = false;
    direct_address_ = nmi ? (emulation_ ? 0xFFFAu : 0xFFEAu)
                          : (emulation_ ? 0xFFFEu : 0xFFEEu);
    direct_value_ = pc_;
    const uint32_t dummy = (static_cast<uint32_t>(pb_) << 16) | pc_;
    queueDeferredReadAction(dummy, CycleAction::HardwareAfterDummy);
}
void CPU::queueOpcodeFetch() {
    micro_active_ = true;
    micro_bus_cycles_ = 0;
    micro_master_clocks_ = 0;
    micro_m8_before_ = acc8();
    micro_x8_before_ = index8();
    micro_dp_before_ = dp_;
    const uint32_t address = (static_cast<uint32_t>(pb_) << 16) | pc_;
    queueDeferredReadAction(address, CycleAction::OpcodeFetch);
}
void CPU::directFinalize() {
    const uint32_t desired = static_cast<uint32_t>(baseCycles(micro_opcode_)) +
        modePenaltyCycles(micro_opcode_, micro_m8_before_, micro_x8_before_, micro_dp_before_) +
        timing_extra_cycles_;
    cycles_ += desired;
    last_master_clocks_ = micro_master_clocks_;
    micro_active_ = false;
    latchInterruptRecognition();
    if (micro_opcode_ == 0x58 || micro_opcode_ == 0x78 ||
        micro_opcode_ == 0xC2 || micro_opcode_ == 0xE2) irq_poll_i_ = direct_old_i_;
    else irq_poll_i_ = flag(Flag_I);
}
void CPU::directFinish() {
    const uint32_t desired = static_cast<uint32_t>(baseCycles(micro_opcode_)) +
        modePenaltyCycles(micro_opcode_, micro_m8_before_, micro_x8_before_, micro_dp_before_) +
        timing_extra_cycles_;
    if (micro_bus_cycles_ < desired) {
        queueInternalAction(desired - micro_bus_cycles_, CycleAction::FinishDirect);
    } else {
        directFinalize();
    }
}
bool CPU::startDirectInstruction(uint8_t opcode) {
    direct_mode_ = DirectAddressMode::None;
    direct_address_ = 0;
    direct_value_ = 0;
    direct_index_ = 0;
    direct_target_ = 0;
    direct_temp8_ = 0;
    direct_wide_ = false;
    direct_indexed_ = false;
    direct_store_ = false;
    direct_rmw_ = false;
    direct_old_i_ = flag(Flag_I);
    if (opcode == 0x20) {
        queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | pc_, CycleAction::JsrLo);
        return true;
    }
    if (opcode == 0x60) {
        queueInternalAction(2, CycleAction::RtsPullLow);
        return true;
    }
    switch (opcode) {
    case 0x0A: case 0x2A: case 0x4A: case 0x6A: case 0x1A: case 0x3A:
    case 0x18: case 0x38: case 0x58: case 0x78: case 0xB8: case 0xD8: case 0xF8:
    case 0x88: case 0xC8: case 0xCA: case 0xE8:
    case 0xAA: case 0xA8: case 0x8A: case 0x98: case 0x9A: case 0xBA:
    case 0x9B: case 0xBB: case 0x5B: case 0x7B: case 0x1B: case 0x3B:
    case 0xEB: case 0xFB: case 0xCB: case 0xDB: case 0xEA: {
        const uint32_t desired = static_cast<uint32_t>(baseCycles(opcode));
        const uint32_t remaining = desired > micro_bus_cycles_ ? desired - micro_bus_cycles_ : 0u;
        if (remaining) queueInternalAction(remaining, CycleAction::ImpliedCommit);
        else handlePostCycleAction(CycleAction::ImpliedCommit);
        return true;
    }
    default: break;
    }
    switch (opcode) {
    case 0x09: case 0x29: case 0x49: case 0x69: case 0x89: case 0xA9: case 0xC9: case 0xE9:
        direct_wide_ = !micro_m8_before_;
        queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | pc_, CycleAction::ImmediateLo);
        return true;
    case 0xA0: case 0xA2: case 0xC0: case 0xE0:
        direct_wide_ = !micro_x8_before_;
        queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | pc_, CycleAction::ImmediateLo);
        return true;
    default: break;
    }
    switch (opcode) {
    case 0x05: case 0x25: case 0x45: case 0x65: case 0x24: case 0xA5: case 0xC5: case 0xE5:
        direct_wide_ = !micro_m8_before_; break;
    case 0x15: case 0x35: case 0x55: case 0x75: case 0x34: case 0xB5: case 0xD5: case 0xF5:
        direct_wide_ = !micro_m8_before_; direct_index_ = x_; direct_indexed_ = true; break;
    case 0xA4: case 0xA6: case 0xC4: case 0xE4:
        direct_wide_ = !micro_x8_before_; break;
    case 0xB4:
        direct_wide_ = !micro_x8_before_; direct_index_ = x_; direct_indexed_ = true; break;
    case 0xB6:
        direct_wide_ = !micro_x8_before_; direct_index_ = y_; direct_indexed_ = true; break;
    default: goto not_direct_read;
    }
    queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | pc_, CycleAction::DirectOperand);
    return true;
not_direct_read:
    switch (opcode) {
    case 0x0D: case 0x2D: case 0x4D: case 0x6D: case 0x2C: case 0xAD: case 0xCD: case 0xED:
        direct_wide_ = !micro_m8_before_; break;
    case 0x19: case 0x39: case 0x59: case 0x79: case 0xB9: case 0xD9: case 0xF9:
        direct_wide_ = !micro_m8_before_; direct_index_ = y_; direct_indexed_ = true; break;
    case 0x1D: case 0x3D: case 0x5D: case 0x7D: case 0x3C: case 0xBD: case 0xDD: case 0xFD:
        direct_wide_ = !micro_m8_before_; direct_index_ = x_; direct_indexed_ = true; break;
    case 0xAC: case 0xAE: case 0xCC: case 0xEC:
        direct_wide_ = !micro_x8_before_; break;
    case 0xBC:
        direct_wide_ = !micro_x8_before_; direct_index_ = x_; direct_indexed_ = true; break;
    case 0xBE:
        direct_wide_ = !micro_x8_before_; direct_index_ = y_; direct_indexed_ = true; break;
    default: goto not_absolute_read;
    }
    queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | pc_, CycleAction::AbsoluteLo);
    return true;
not_absolute_read:
    switch (opcode) {
    case 0x85: direct_value_ = a_; direct_wide_ = !micro_m8_before_; break;
    case 0x95: direct_value_ = a_; direct_wide_ = !micro_m8_before_; direct_index_ = x_; direct_indexed_ = true; break;
    case 0x84: direct_value_ = y_; direct_wide_ = !micro_x8_before_; break;
    case 0x94: direct_value_ = y_; direct_wide_ = !micro_x8_before_; direct_index_ = x_; direct_indexed_ = true; break;
    case 0x86: direct_value_ = x_; direct_wide_ = !micro_x8_before_; break;
    case 0x96: direct_value_ = x_; direct_wide_ = !micro_x8_before_; direct_index_ = y_; direct_indexed_ = true; break;
    case 0x64: direct_value_ = 0; direct_wide_ = !micro_m8_before_; break;
    case 0x74: direct_value_ = 0; direct_wide_ = !micro_m8_before_; direct_index_ = x_; direct_indexed_ = true; break;
    default: goto not_direct_store;
    }
    direct_store_ = true;
    queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | pc_, CycleAction::DirectOperand);
    return true;
not_direct_store:
    switch (opcode) {
    case 0x8D: direct_value_ = a_; direct_wide_ = !micro_m8_before_; break;
    case 0x99: direct_value_ = a_; direct_wide_ = !micro_m8_before_; direct_index_ = y_; direct_indexed_ = true; break;
    case 0x9D: direct_value_ = a_; direct_wide_ = !micro_m8_before_; direct_index_ = x_; direct_indexed_ = true; break;
    case 0x8C: direct_value_ = y_; direct_wide_ = !micro_x8_before_; break;
    case 0x8E: direct_value_ = x_; direct_wide_ = !micro_x8_before_; break;
    case 0x9C: direct_value_ = 0; direct_wide_ = !micro_m8_before_; break;
    case 0x9E: direct_value_ = 0; direct_wide_ = !micro_m8_before_; direct_index_ = x_; direct_indexed_ = true; break;
    default: goto not_absolute_store;
    }
    direct_store_ = true;
    queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | pc_, CycleAction::AbsoluteLo);
    return true;
not_absolute_store:
    switch (opcode) {
    case 0x04: case 0x14:
    case 0x06: case 0x26: case 0x46: case 0x66: case 0xC6: case 0xE6:
        direct_wide_ = !micro_m8_before_; direct_rmw_ = true;
        queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | pc_, CycleAction::DirectOperand);
        return true;
    case 0x16: case 0x36: case 0x56: case 0x76: case 0xD6: case 0xF6:
        direct_wide_ = !micro_m8_before_; direct_rmw_ = true; direct_indexed_ = true; direct_index_ = x_;
        queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | pc_, CycleAction::DirectOperand);
        return true;
    case 0x0C: case 0x1C:
    case 0x0E: case 0x2E: case 0x4E: case 0x6E: case 0xCE: case 0xEE:
        direct_wide_ = !micro_m8_before_; direct_rmw_ = true;
        queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | pc_, CycleAction::AbsoluteLo);
        return true;
    case 0x1E: case 0x3E: case 0x5E: case 0x7E: case 0xDE: case 0xFE:
        direct_wide_ = !micro_m8_before_; direct_rmw_ = true; direct_indexed_ = true; direct_index_ = x_;
        queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | pc_, CycleAction::AbsoluteLo);
        return true;
    default: break;
    }
    switch (opcode) {
    case 0x0F: case 0x2F: case 0x4F: case 0x6F: case 0xAF: case 0xCF: case 0xEF:
        direct_wide_ = !micro_m8_before_; direct_mode_ = DirectAddressMode::AbsoluteLong; break;
    case 0x1F: case 0x3F: case 0x5F: case 0x7F: case 0xBF: case 0xDF: case 0xFF:
        direct_wide_ = !micro_m8_before_; direct_mode_ = DirectAddressMode::AbsoluteLongX; direct_index_ = x_; break;
    case 0x8F:
        direct_wide_ = !micro_m8_before_; direct_store_ = true; direct_value_ = a_; direct_mode_ = DirectAddressMode::AbsoluteLong; break;
    case 0x9F:
        direct_wide_ = !micro_m8_before_; direct_store_ = true; direct_value_ = a_; direct_mode_ = DirectAddressMode::AbsoluteLongX; direct_index_ = x_; break;
    default: break;
    }
    if (direct_mode_ == DirectAddressMode::AbsoluteLong || direct_mode_ == DirectAddressMode::AbsoluteLongX) {
        queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | pc_, CycleAction::LongLo);
        return true;
    }
    switch (opcode) {
    case 0x01: case 0x21: case 0x41: case 0x61: case 0xA1: case 0xC1: case 0xE1:
        direct_wide_ = !micro_m8_before_; direct_mode_ = DirectAddressMode::IndirectX; break;
    case 0x11: case 0x31: case 0x51: case 0x71: case 0xB1: case 0xD1: case 0xF1:
        direct_wide_ = !micro_m8_before_; direct_mode_ = DirectAddressMode::IndirectY; break;
    case 0x12: case 0x32: case 0x52: case 0x72: case 0xB2: case 0xD2: case 0xF2:
        direct_wide_ = !micro_m8_before_; direct_mode_ = DirectAddressMode::Indirect; break;
    case 0x07: case 0x27: case 0x47: case 0x67: case 0xA7: case 0xC7: case 0xE7:
        direct_wide_ = !micro_m8_before_; direct_mode_ = DirectAddressMode::IndirectLong; break;
    case 0x17: case 0x37: case 0x57: case 0x77: case 0xB7: case 0xD7: case 0xF7:
        direct_wide_ = !micro_m8_before_; direct_mode_ = DirectAddressMode::IndirectLongY; break;
    case 0x81:
        direct_wide_ = !micro_m8_before_; direct_store_ = true; direct_value_ = a_; direct_mode_ = DirectAddressMode::IndirectX; break;
    case 0x91:
        direct_wide_ = !micro_m8_before_; direct_store_ = true; direct_value_ = a_; direct_mode_ = DirectAddressMode::IndirectY; break;
    case 0x92:
        direct_wide_ = !micro_m8_before_; direct_store_ = true; direct_value_ = a_; direct_mode_ = DirectAddressMode::Indirect; break;
    case 0x87:
        direct_wide_ = !micro_m8_before_; direct_store_ = true; direct_value_ = a_; direct_mode_ = DirectAddressMode::IndirectLong; break;
    case 0x97:
        direct_wide_ = !micro_m8_before_; direct_store_ = true; direct_value_ = a_; direct_mode_ = DirectAddressMode::IndirectLongY; break;
    default: break;
    }
    if (direct_mode_ == DirectAddressMode::IndirectX || direct_mode_ == DirectAddressMode::Indirect ||
        direct_mode_ == DirectAddressMode::IndirectY || direct_mode_ == DirectAddressMode::IndirectLong ||
        direct_mode_ == DirectAddressMode::IndirectLongY) {
        queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | pc_, CycleAction::IndirectOperand);
        return true;
    }
    switch (opcode) {
    case 0x03: case 0x23: case 0x43: case 0x63: case 0xA3: case 0xC3: case 0xE3:
        direct_wide_ = !micro_m8_before_; direct_mode_ = DirectAddressMode::StackRelative; break;
    case 0x13: case 0x33: case 0x53: case 0x73: case 0xB3: case 0xD3: case 0xF3:
        direct_wide_ = !micro_m8_before_; direct_mode_ = DirectAddressMode::StackRelativeIndirectY; break;
    case 0x83:
        direct_wide_ = !micro_m8_before_; direct_store_ = true; direct_value_ = a_; direct_mode_ = DirectAddressMode::StackRelative; break;
    case 0x93:
        direct_wide_ = !micro_m8_before_; direct_store_ = true; direct_value_ = a_; direct_mode_ = DirectAddressMode::StackRelativeIndirectY; break;
    default: break;
    }
    if (direct_mode_ == DirectAddressMode::StackRelative || direct_mode_ == DirectAddressMode::StackRelativeIndirectY) {
        queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | pc_, CycleAction::StackOperand);
        return true;
    }
    switch (opcode) {
    case 0x10: case 0x30: case 0x50: case 0x70: case 0x80:
    case 0x90: case 0xB0: case 0xD0: case 0xF0:
        queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | pc_, CycleAction::BranchDisp);
        return true;
    default: break;
    }
    switch (opcode) {
    case 0x08:
        direct_value_ = emulation_ ? static_cast<uint8_t>(p_ | 0x30) : p_;
        queueInternalAction(1, CycleAction::StackPushStart);
        return true;
    case 0x48:
        direct_value_ = a_; direct_wide_ = !micro_m8_before_;
        queueInternalAction(1, CycleAction::StackPushStart);
        return true;
    case 0xDA:
        direct_value_ = x_; direct_wide_ = !micro_x8_before_;
        queueInternalAction(1, CycleAction::StackPushStart);
        return true;
    case 0x5A:
        direct_value_ = y_; direct_wide_ = !micro_x8_before_;
        queueInternalAction(1, CycleAction::StackPushStart);
        return true;
    case 0x8B:
        direct_value_ = db_;
        queueInternalAction(1, CycleAction::StackPushStart);
        return true;
    case 0x0B:
        direct_value_ = dp_; direct_wide_ = true; direct_indexed_ = true;
        queueInternalAction(1, CycleAction::StackPushStart);
        return true;
    case 0x4B:
        direct_value_ = pb_;
        queueInternalAction(1, CycleAction::StackPushStart);
        return true;
    case 0x28:
    case 0x68:
    case 0xFA:
    case 0x7A:
        direct_wide_ = (opcode == 0x68) ? !micro_m8_before_ :
                       ((opcode == 0xFA || opcode == 0x7A) ? !micro_x8_before_ : false);
        queueInternalAction(2, CycleAction::StackPullStart);
        return true;
    case 0xAB:
        direct_indexed_ = true;
        queueInternalAction(2, CycleAction::StackPullStart);
        return true;
    case 0x2B:
        direct_wide_ = true; direct_indexed_ = true;
        queueInternalAction(2, CycleAction::StackPullStart);
        return true;
    default: break;
    }
    switch (opcode) {
    case 0x4C:
    case 0x5C:
    case 0x6C:
    case 0x7C:
    case 0xDC:
        queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | pc_, CycleAction::JumpLo);
        return true;
    case 0xFC:
        queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | pc_, CycleAction::JsrIndirectLo);
        return true;
    case 0x22:
        queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | pc_, CycleAction::JslLo);
        return true;
    case 0x6B:
        queueInternalAction(2, CycleAction::RtlStart);
        return true;
    case 0x40:
        if (!emulation_) ++timing_extra_cycles_;
        queueInternalAction(2, CycleAction::RtiStart);
        return true;
    case 0xF4:
    case 0x62:
        direct_wide_ = true;
        direct_indexed_ = true;
        queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | pc_, CycleAction::EffectiveLo);
        return true;
    case 0xD4:
        direct_wide_ = true;
        direct_indexed_ = true;
        queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | pc_, CycleAction::PeiOperand);
        return true;
    default: break;
    }
    switch (opcode) {
    case 0x00:
    case 0x02:
        if (!emulation_) ++timing_extra_cycles_;
        queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | pc_, CycleAction::SoftwareSignature);
        return true;
    case 0xC2:
    case 0xE2:
        queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | pc_, CycleAction::RepSepMask);
        return true;
    case 0x82:
        queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | pc_, CycleAction::BrlLo);
        return true;
    case 0x42:
        queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | pc_, CycleAction::WdmSignature);
        return true;
    case 0x44:
    case 0x54:
        queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | pc_, CycleAction::BlockDst);
        return true;
    default: break;
    }
    return false;
}
void CPU::handleReadAction(CycleAction action, uint8_t value) {
    switch (action) {
    case CycleAction::OpcodeFetch:
        ++pc_;
        onOpcodeFetched(value);
        return;
    case CycleAction::ImmediateLo:
        ++pc_;
        if (!direct_wide_) {
            microApplyReadOperation(micro_opcode_, value);
            directFinish();
        } else {
            direct_temp8_ = value;
            queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | pc_, CycleAction::ImmediateHi);
        }
        return;
    case CycleAction::ImmediateHi:
        ++pc_;
        microApplyReadOperation(micro_opcode_, static_cast<uint16_t>(direct_temp8_ | (static_cast<uint16_t>(value) << 8)));
        directFinish();
        return;
    case CycleAction::DirectOperand: {
        ++pc_;
        direct_address_ = directAddress(value, direct_index_);
        direct_temp8_ = static_cast<uint8_t>((direct_indexed_ ? 1u : 0u) +
            (((micro_dp_before_ & 0x00FF) != 0) ? 1u : 0u));
        if (direct_temp8_) {
            if ((micro_opcode_ == 0x04 || micro_opcode_ == 0x14) && !direct_indexed_) {
                queueInternalAction(direct_temp8_, CycleAction::StartRmwRead);
            } else {
                queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | static_cast<uint16_t>(pc_ - 1),
                    CycleAction::DirectPenalty);
            }
        } else {
            handlePostCycleAction(direct_rmw_ ? CycleAction::StartRmwRead :
                                  (direct_store_ ? CycleAction::StartStore : CycleAction::StartDataRead));
        }
        return;
    }
    case CycleAction::DirectPenalty:
        if (--direct_temp8_) {
            queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | static_cast<uint16_t>(pc_ - 1),
                CycleAction::DirectPenalty);
        } else {
            handlePostCycleAction(direct_rmw_ ? CycleAction::StartRmwRead :
                                  (direct_store_ ? CycleAction::StartStore : CycleAction::StartDataRead));
        }
        return;
    case CycleAction::AbsoluteLo:
        ++pc_;
        direct_temp8_ = value;
        queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | pc_, CycleAction::AbsoluteHi);
        return;
    case CycleAction::AbsoluteHi: {
        ++pc_;
        const uint16_t base = static_cast<uint16_t>(direct_temp8_ | (static_cast<uint16_t>(value) << 8));
        const uint16_t off = static_cast<uint16_t>(base + direct_index_);
        page_crossed_ = ((base ^ off) & 0xFF00) != 0;
        direct_address_ = (((static_cast<uint32_t>(db_) << 16) | base) + direct_index_) & 0xFFFFFF;
        const bool indexed_bus_cycle = direct_indexed_ && (direct_store_ || direct_rmw_ ||
            (hasPageCrossPenalty(micro_opcode_) && (page_crossed_ || !micro_x8_before_)));
        if (indexed_bus_cycle) {
            const uint16_t incorrect = static_cast<uint16_t>(
                ((base + (direct_index_ & 0xFF00)) & 0xFF00) |
                ((base + direct_index_) & 0x00FF));
            const uint32_t incorrect_address = (static_cast<uint32_t>(db_) << 16) | incorrect;
            const CycleAction next = direct_rmw_ ? CycleAction::DummyToRmw :
                (direct_store_ ? CycleAction::DummyToStore : CycleAction::DummyToRead);
            queueDeferredReadAction(incorrect_address, next);
        } else {
            handlePostCycleAction(direct_rmw_ ? CycleAction::StartRmwRead :
                                  (direct_store_ ? CycleAction::StartStore : CycleAction::StartDataRead));
        }
        return;
    }
    case CycleAction::LongLo:
        ++pc_;
        direct_temp8_ = value;
        queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | pc_, CycleAction::LongHi);
        return;
    case CycleAction::LongHi:
        ++pc_;
        direct_target_ = static_cast<uint16_t>(direct_temp8_ | (static_cast<uint16_t>(value) << 8));
        queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | pc_, CycleAction::LongBank);
        return;
    case CycleAction::LongBank:
        ++pc_;
        direct_address_ = static_cast<uint32_t>(direct_target_) | (static_cast<uint32_t>(value) << 16);
        if (direct_mode_ == DirectAddressMode::AbsoluteLongX)
            direct_address_ = (direct_address_ + direct_index_) & 0xFFFFFF;
        handlePostCycleAction(direct_store_ ? CycleAction::StartStore : CycleAction::StartDataRead);
        return;
    case CycleAction::IndirectOperand: {
        ++pc_;
        const bool indexed_x = direct_mode_ == DirectAddressMode::IndirectX;
        direct_address_ = directAddress(value, indexed_x ? x_ : 0);
        direct_temp8_ = static_cast<uint8_t>((indexed_x ? 1u : 0u) +
            (((micro_dp_before_ & 0x00FF) != 0) ? 1u : 0u));
        if (direct_temp8_) {
            queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | static_cast<uint16_t>(pc_ - 1),
                CycleAction::IndirectPenalty);
        } else {
            queueDeferredReadAction(direct_address_, CycleAction::PointerLo);
        }
        return;
    }
    case CycleAction::IndirectPenalty:
        if (--direct_temp8_) {
            queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | static_cast<uint16_t>(pc_ - 1),
                CycleAction::IndirectPenalty);
        } else {
            queueDeferredReadAction(direct_address_, CycleAction::PointerLo);
        }
        return;
    case CycleAction::PointerLo: {
        direct_temp8_ = value;
        const bool long_pointer = direct_mode_ == DirectAddressMode::IndirectLong ||
            direct_mode_ == DirectAddressMode::IndirectLongY;
        const uint16_t pointer_base = static_cast<uint16_t>(direct_address_);
        const uint16_t pointer_hi_address =
            (emulation_ && direct_mode_ == DirectAddressMode::IndirectX)
                ? static_cast<uint16_t>((pointer_base & 0xFF00) | static_cast<uint8_t>((pointer_base & 0x00FF) + 1))
                : directPointerAddress(pointer_base, 1, long_pointer);
        queueDeferredReadAction(pointer_hi_address, CycleAction::PointerHi);
        return;
    }
    case CycleAction::PointerHi: {
        direct_target_ = static_cast<uint16_t>(direct_temp8_ | (static_cast<uint16_t>(value) << 8));
        const bool long_pointer = direct_mode_ == DirectAddressMode::IndirectLong ||
            direct_mode_ == DirectAddressMode::IndirectLongY;
        if (long_pointer) {
            queueDeferredReadAction(directPointerAddress(static_cast<uint16_t>(direct_address_), 2, true),
                CycleAction::PointerBank);
            return;
        }
        const bool indexed_y = direct_mode_ == DirectAddressMode::IndirectY;
        const uint16_t base = direct_target_;
        const uint16_t off = static_cast<uint16_t>(base + (indexed_y ? y_ : 0));
        if (indexed_y) page_crossed_ = ((base ^ off) & 0xFF00) != 0;
        const uint32_t unindexed = (static_cast<uint32_t>(db_) << 16) | base;
        direct_address_ = indexed_y ? ((unindexed + y_) & 0xFFFFFF) : unindexed;
        const bool indexed_bus_cycle = indexed_y && (direct_store_ ||
            (hasPageCrossPenalty(micro_opcode_) && (page_crossed_ || !micro_x8_before_)));
        if (indexed_bus_cycle) {
            const uint16_t incorrect = static_cast<uint16_t>(
                ((base + (y_ & 0xFF00)) & 0xFF00) | ((base + y_) & 0x00FF));
            queueDeferredReadAction((static_cast<uint32_t>(db_) << 16) | incorrect, CycleAction::IndirectDummy);
        } else {
            handlePostCycleAction(direct_store_ ? CycleAction::StartStore : CycleAction::StartDataRead);
        }
        return;
    }
    case CycleAction::PointerBank:
        direct_address_ = static_cast<uint32_t>(direct_target_) | (static_cast<uint32_t>(value) << 16);
        if (direct_mode_ == DirectAddressMode::IndirectLongY)
            direct_address_ = (direct_address_ + y_) & 0xFFFFFF;
        handlePostCycleAction(direct_store_ ? CycleAction::StartStore : CycleAction::StartDataRead);
        return;
    case CycleAction::IndirectDummy:
        handlePostCycleAction(direct_store_ ? CycleAction::StartStore : CycleAction::StartDataRead);
        return;
    case CycleAction::StackOperand:
        ++pc_;
        direct_address_ = static_cast<uint16_t>(s_ + value);
        if (direct_mode_ == DirectAddressMode::StackRelativeIndirectY)
            queueInternalAction(1, CycleAction::StartStackPointerRead);
        else
            queueInternalAction(1, direct_store_ ? CycleAction::StartStore : CycleAction::StartDataRead);
        return;
    case CycleAction::StackPointerLo:
        direct_temp8_ = value;
        queueDeferredReadAction(static_cast<uint16_t>(direct_address_ + 1), CycleAction::StackPointerHi);
        return;
    case CycleAction::StackPointerHi: {
        const uint16_t base = static_cast<uint16_t>(direct_temp8_ | (static_cast<uint16_t>(value) << 8));
        direct_address_ = (((static_cast<uint32_t>(db_) << 16) | base) + y_) & 0xFFFFFF;
        queueInternalAction(1, direct_store_ ? CycleAction::StartStore : CycleAction::StartDataRead);
        return;
    }
    case CycleAction::DataLo:
        if (!direct_wide_) {
            microApplyReadOperation(micro_opcode_, value);
            directFinish();
        } else {
            direct_temp8_ = value;
            queueDeferredReadAction((direct_address_ + 1) & 0xFFFFFF, CycleAction::DataHi);
        }
        return;
    case CycleAction::DataHi:
        microApplyReadOperation(micro_opcode_, static_cast<uint16_t>(direct_temp8_ | (static_cast<uint16_t>(value) << 8)));
        directFinish();
        return;
    case CycleAction::RmwDataLo:
        if (!direct_wide_) {
            directRmwModify(value);
        } else {
            direct_temp8_ = value;
            queueDeferredReadAction((direct_address_ + 1) & 0xFFFFFF, CycleAction::RmwDataHi);
        }
        return;
    case CycleAction::RmwDataHi:
        directRmwModify(static_cast<uint16_t>(direct_temp8_ | (static_cast<uint16_t>(value) << 8)));
        return;
    case CycleAction::RmwAfterModify:
        handlePostCycleAction(CycleAction::RmwAfterModify);
        return;
    case CycleAction::DummyToRead:
        handlePostCycleAction(CycleAction::StartDataRead);
        return;
    case CycleAction::DummyToStore:
        handlePostCycleAction(CycleAction::StartStore);
        return;
    case CycleAction::DummyToRmw:
        handlePostCycleAction(CycleAction::StartRmwRead);
        return;
    case CycleAction::JsrLo:
        ++pc_;
        direct_temp8_ = value;
        queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | pc_, CycleAction::JsrHi);
        return;
    case CycleAction::JsrHi:
        ++pc_;
        direct_target_ = static_cast<uint16_t>(direct_temp8_ | (static_cast<uint16_t>(value) << 8));
        direct_value_ = static_cast<uint16_t>(pc_ - 1);
        queueInternalAction(1, CycleAction::JsrPushHigh);
        return;
    case CycleAction::RtsPullLow:
        direct_temp8_ = value;
        if (emulation_) s_ = static_cast<uint16_t>(0x0100 | ((s_ + 1) & 0x00FF));
        else ++s_;
        queueDeferredReadAction(s_, CycleAction::RtsPullHigh);
        return;
    case CycleAction::RtsPullHigh:
        direct_target_ = static_cast<uint16_t>(direct_temp8_ | (static_cast<uint16_t>(value) << 8));
        queueInternalAction(1, CycleAction::RtsCommit);
        return;
    case CycleAction::StackPullLow:
        if (!direct_wide_) {
            if (direct_indexed_ && emulation_)
                s_ = static_cast<uint16_t>(0x0100 | (s_ & 0x00FF));
            directCommitStackPull(value);
        } else {
            direct_temp8_ = value;
            if (direct_indexed_) ++s_;
            else if (emulation_) s_ = static_cast<uint16_t>(0x0100 | ((s_ + 1) & 0x00FF));
            else ++s_;
            queueDeferredReadAction(s_, CycleAction::StackPullHigh);
        }
        return;
    case CycleAction::StackPullHigh:
        if (direct_indexed_ && emulation_)
            s_ = static_cast<uint16_t>(0x0100 | (s_ & 0x00FF));
        directCommitStackPull(static_cast<uint16_t>(direct_temp8_ | (static_cast<uint16_t>(value) << 8)));
        return;
    case CycleAction::BranchDisp: {
        ++pc_;
        bool take = false;
        switch (micro_opcode_) {
        case 0x10: take = !flag(Flag_N); break; case 0x30: take = flag(Flag_N); break;
        case 0x50: take = !flag(Flag_V); break; case 0x70: take = flag(Flag_V); break;
        case 0x80: take = true; break;
        case 0x90: take = !flag(Flag_C); break; case 0xB0: take = flag(Flag_C); break;
        case 0xD0: take = !flag(Flag_Z); break; case 0xF0: take = flag(Flag_Z); break;
        default: break;
        }
        if (!take) { directFinish(); return; }
        const uint16_t old_pc = pc_;
        direct_target_ = static_cast<uint16_t>(pc_ + static_cast<int8_t>(value));
        const bool page_cross = emulation_ && ((old_pc ^ direct_target_) & 0xFF00) != 0;
        if (micro_opcode_ != 0x80) ++timing_extra_cycles_;
        if (page_cross) ++timing_extra_cycles_;
        direct_temp8_ = static_cast<uint8_t>(page_cross ? 2u : 1u);
        direct_address_ = (static_cast<uint32_t>(pb_) << 16) | old_pc;
        queueDeferredReadAction(direct_address_, CycleAction::BranchDummy);
        return;
    }
    case CycleAction::BranchDummy:
        if (--direct_temp8_) {
            const uint16_t wrong = static_cast<uint16_t>((pc_ & 0xFF00) | (direct_target_ & 0x00FF));
            queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | wrong, CycleAction::BranchDummy);
        } else {
            handlePostCycleAction(CycleAction::BranchCommit);
        }
        return;
    case CycleAction::JumpLo:
        ++pc_;
        direct_temp8_ = value;
        queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | pc_, CycleAction::JumpHi);
        return;
    case CycleAction::JumpHi:
        ++pc_;
        direct_target_ = static_cast<uint16_t>(direct_temp8_ | (static_cast<uint16_t>(value) << 8));
        switch (micro_opcode_) {
        case 0x4C:
            pc_ = direct_target_;
            directFinalize();
            return;
        case 0x5C:
            queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | pc_, CycleAction::JumpBank);
            return;
        case 0x6C:
        case 0xDC:
            direct_address_ = direct_target_;
            handlePostCycleAction(CycleAction::JumpPointerStart);
            return;
        case 0x7C:
            direct_address_ = (static_cast<uint32_t>(pb_) << 16) | static_cast<uint16_t>(direct_target_ + x_);
            queueInternalAction(1, CycleAction::JumpPointerStart);
            return;
        default:
            return;
        }
    case CycleAction::JumpBank:
        ++pc_;
        pc_ = direct_target_;
        pb_ = value;
        directFinalize();
        return;
    case CycleAction::JumpPointerLo:
        direct_temp8_ = value;
        queueDeferredReadAction((direct_address_ & 0xFF0000u) | static_cast<uint16_t>(direct_address_ + 1),
            CycleAction::JumpPointerHi);
        return;
    case CycleAction::JumpPointerHi:
        direct_target_ = static_cast<uint16_t>(direct_temp8_ | (static_cast<uint16_t>(value) << 8));
        if (micro_opcode_ == 0xDC) {
            queueDeferredReadAction(static_cast<uint16_t>(direct_address_ + 2), CycleAction::JumpPointerBank);
        } else {
            pc_ = direct_target_;
            directFinalize();
        }
        return;
    case CycleAction::JumpPointerBank:
        pc_ = direct_target_;
        pb_ = value;
        directFinalize();
        return;
    case CycleAction::JsrIndirectLo:
        ++pc_;
        direct_temp8_ = value;
        direct_value_ = pc_;
        queueDeferredWriteAction(s_, static_cast<uint8_t>(direct_value_ >> 8), CycleAction::JsrIndirectPushLow);
        return;
    case CycleAction::JsrIndirectHi:
        ++pc_;
        direct_address_ = (static_cast<uint32_t>(pb_) << 16) |
            static_cast<uint16_t>(static_cast<uint16_t>(direct_temp8_ | (static_cast<uint16_t>(value) << 8)) + x_);
        queueInternalAction(1, CycleAction::JumpPointerStart);
        return;
    case CycleAction::JslLo:
        ++pc_;
        direct_temp8_ = value;
        queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | pc_, CycleAction::JslHi);
        return;
    case CycleAction::JslHi:
        ++pc_;
        direct_target_ = static_cast<uint16_t>(direct_temp8_ | (static_cast<uint16_t>(value) << 8));
        queueDeferredWriteAction(s_, pb_, CycleAction::JslAfterBankPush);
        return;
    case CycleAction::JslBank:
        ++pc_;
        direct_address_ = value;
        direct_value_ = static_cast<uint16_t>(pc_ - 1);
        queueDeferredWriteAction(s_, static_cast<uint8_t>(direct_value_ >> 8), CycleAction::JslPushReturnLow);
        return;
    case CycleAction::RtlLow:
        direct_temp8_ = value;
        ++s_;
        queueDeferredReadAction(s_, CycleAction::RtlHigh);
        return;
    case CycleAction::RtlHigh:
        direct_target_ = static_cast<uint16_t>(direct_temp8_ | (static_cast<uint16_t>(value) << 8));
        ++s_;
        queueDeferredReadAction(s_, CycleAction::RtlBank);
        return;
    case CycleAction::RtlBank:
        if (emulation_) s_ = static_cast<uint16_t>(0x0100 | (s_ & 0x00FF));
        pc_ = static_cast<uint16_t>(direct_target_ + 1);
        pb_ = value;
        directFinalize();
        return;
    case CycleAction::RtiStatus:
        p_ = value;
        normalizeMode();
        if (emulation_) s_ = static_cast<uint16_t>(0x0100 | ((s_ + 1) & 0x00FF));
        else ++s_;
        queueDeferredReadAction(s_, CycleAction::RtiLow);
        return;
    case CycleAction::RtiLow:
        direct_temp8_ = value;
        if (emulation_) s_ = static_cast<uint16_t>(0x0100 | ((s_ + 1) & 0x00FF));
        else ++s_;
        queueDeferredReadAction(s_, CycleAction::RtiHigh);
        return;
    case CycleAction::RtiHigh:
        direct_target_ = static_cast<uint16_t>(direct_temp8_ | (static_cast<uint16_t>(value) << 8));
        if (emulation_) {
            pc_ = direct_target_;
            directFinalize();
        } else {
            ++s_;
            queueDeferredReadAction(s_, CycleAction::RtiBank);
        }
        return;
    case CycleAction::RtiBank:
        pc_ = direct_target_;
        pb_ = value;
        directFinalize();
        return;
    case CycleAction::EffectiveLo:
        ++pc_;
        direct_temp8_ = value;
        queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | pc_, CycleAction::EffectiveHi);
        return;
    case CycleAction::EffectiveHi: {
        ++pc_;
        const uint16_t operand = static_cast<uint16_t>(direct_temp8_ | (static_cast<uint16_t>(value) << 8));
        if (micro_opcode_ == 0x62) {
            direct_value_ = static_cast<uint16_t>(pc_ + static_cast<int16_t>(operand));
            queueInternalAction(1, CycleAction::StackPushStart);
        } else {
            direct_value_ = operand;
            handlePostCycleAction(CycleAction::StackPushStart);
        }
        return;
    }
    case CycleAction::PeiOperand:
        ++pc_;
        direct_address_ = directAddress(value);
        if ((micro_dp_before_ & 0x00FF) != 0) {
            queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | static_cast<uint16_t>(pc_ - 1),
                CycleAction::PeiPenalty);
        } else {
            queueDeferredReadAction(direct_address_, CycleAction::PeiPointerLo);
        }
        return;
    case CycleAction::PeiPenalty:
        queueDeferredReadAction(direct_address_, CycleAction::PeiPointerLo);
        return;
    case CycleAction::PeiPointerLo:
        direct_temp8_ = value;
        queueDeferredReadAction(directPointerAddress(static_cast<uint16_t>(direct_address_), 1, true),
            CycleAction::PeiPointerHi);
        return;
    case CycleAction::PeiPointerHi:
        direct_value_ = static_cast<uint16_t>(direct_temp8_ | (static_cast<uint16_t>(value) << 8));
        handlePostCycleAction(CycleAction::StackPushStart);
        return;
    case CycleAction::SoftwareSignature:
        ++pc_;
        direct_address_ = (micro_opcode_ == 0x00)
            ? (emulation_ ? 0xFFFEu : 0xFFE6u)
            : (emulation_ ? 0xFFF4u : 0xFFE4u);
        direct_value_ = pc_;
        if (!emulation_) {
            queueDeferredWriteAction(s_, pb_, CycleAction::SoftwarePushPCHigh);
        } else {
            queueDeferredWriteAction(s_, static_cast<uint8_t>(direct_value_ >> 8), CycleAction::SoftwarePushPCLow);
        }
        return;
    case CycleAction::SoftwareVectorLo:
        direct_temp8_ = value;
        queueDeferredReadAction(static_cast<uint16_t>(direct_address_ + 1), CycleAction::SoftwareVectorHi);
        return;
    case CycleAction::SoftwareVectorHi:
        pc_ = static_cast<uint16_t>(direct_temp8_ | (static_cast<uint16_t>(value) << 8));
        directFinish();
        return;
    case CycleAction::RepSepMask:
        ++pc_;
        direct_temp8_ = value;
        queueInternalAction(1, CycleAction::RepSepCommit);
        return;
    case CycleAction::BrlLo:
        ++pc_;
        direct_temp8_ = value;
        queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | pc_, CycleAction::BrlHi);
        return;
    case CycleAction::BrlHi: {
        ++pc_;
        const int16_t rel = static_cast<int16_t>(static_cast<uint16_t>(
            direct_temp8_ | (static_cast<uint16_t>(value) << 8)));
        direct_target_ = static_cast<uint16_t>(pc_ + rel);
        queueInternalAction(1, CycleAction::BrlCommit);
        return;
    }
    case CycleAction::WdmSignature:
        ++pc_;
        directFinish();
        return;
    case CycleAction::BlockDst:
        ++pc_;
        direct_temp8_ = value;
        queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | pc_, CycleAction::BlockSrc);
        return;
    case CycleAction::BlockSrc:
        ++pc_;
        direct_address_ = (static_cast<uint32_t>(value) << 16) | x_;
        queueDeferredReadAction(direct_address_, CycleAction::BlockData);
        return;
    case CycleAction::BlockData:
        direct_value_ = value;
        queueDeferredWriteAction((static_cast<uint32_t>(direct_temp8_) << 16) | y_, value,
            CycleAction::BlockAfterWrite);
        return;
    case CycleAction::HardwareAfterDummy:
        queueInternalAction(1, CycleAction::HardwareBeginStack);
        return;
    case CycleAction::HardwareVectorLo:
        direct_temp8_ = value;
        queueDeferredReadAction(static_cast<uint16_t>(direct_address_ + 1), CycleAction::HardwareVectorHi);
        return;
    case CycleAction::HardwareVectorHi:
        pc_ = static_cast<uint16_t>(direct_temp8_ | (static_cast<uint16_t>(value) << 8));
        cycles_ += emulation_ ? 7u : 8u;
        last_master_clocks_ = micro_master_clocks_;
        micro_active_ = false;
        latchInterruptRecognition();
        return;
    default:
        return;
    }
}
void CPU::handlePostCycleAction(CycleAction action) {
    switch (action) {
    case CycleAction::StartStackPointerRead:
        queueDeferredReadAction(direct_address_, CycleAction::StackPointerLo);
        return;
    case CycleAction::StartDataRead:
        queueDeferredReadAction(direct_address_, CycleAction::DataLo);
        return;
    case CycleAction::StartRmwRead:
        queueDeferredReadAction(direct_address_, CycleAction::RmwDataLo);
        return;
    case CycleAction::RmwAfterModify:
        if (direct_wide_) {
            queueDeferredWriteAction((direct_address_ + 1) & 0xFFFFFF,
                static_cast<uint8_t>(direct_value_ >> 8), CycleAction::RmwWriteLow);
        } else {
            queueDeferredWriteAction(direct_address_, static_cast<uint8_t>(direct_value_), CycleAction::FinishDirect);
        }
        return;
    case CycleAction::RmwWriteLow:
        queueDeferredWriteAction(direct_address_, static_cast<uint8_t>(direct_value_), CycleAction::FinishDirect);
        return;
    case CycleAction::StartStore:
        if (direct_wide_) {
            queueDeferredWriteAction(direct_address_, static_cast<uint8_t>(direct_value_), CycleAction::StoreHigh);
        } else {
            queueDeferredWriteAction(direct_address_, static_cast<uint8_t>(direct_value_), CycleAction::FinishDirect);
        }
        return;
    case CycleAction::StoreHigh:
        queueDeferredWriteAction((direct_address_ + 1) & 0xFFFFFF, static_cast<uint8_t>(direct_value_ >> 8), CycleAction::FinishDirect);
        return;
    case CycleAction::BranchCommit:
        pc_ = direct_target_;
        directFinish();
        return;
    case CycleAction::JsrPushHigh:
        queueDeferredWriteAction(s_, static_cast<uint8_t>(direct_value_ >> 8), CycleAction::JsrPushLow);
        return;
    case CycleAction::JsrPushLow:
        if (emulation_) s_ = static_cast<uint16_t>(0x0100 | ((s_ - 1) & 0x00FF));
        else --s_;
        queueDeferredWriteAction(s_, static_cast<uint8_t>(direct_value_), CycleAction::JsrFinish);
        return;
    case CycleAction::JsrFinish:
        if (emulation_) s_ = static_cast<uint16_t>(0x0100 | ((s_ - 1) & 0x00FF));
        else --s_;
        pc_ = direct_target_;
        directFinalize();
        return;
    case CycleAction::RtsPullLow:
        if (emulation_) s_ = static_cast<uint16_t>(0x0100 | ((s_ + 1) & 0x00FF));
        else ++s_;
        queueDeferredReadAction(s_, CycleAction::RtsPullLow);
        return;
    case CycleAction::RtsCommit:
        pc_ = static_cast<uint16_t>(direct_target_ + 1);
        directFinalize();
        return;
    case CycleAction::JumpPointerStart:
        queueDeferredReadAction(direct_address_, CycleAction::JumpPointerLo);
        return;
    case CycleAction::JsrIndirectPushLow:
        --s_;
        queueDeferredWriteAction(s_, static_cast<uint8_t>(direct_value_), CycleAction::JsrIndirectFetchHi);
        return;
    case CycleAction::JsrIndirectFetchHi:
        --s_;
        if (emulation_) s_ = static_cast<uint16_t>(0x0100 | (s_ & 0x00FF));
        queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | pc_, CycleAction::JsrIndirectHi);
        return;
    case CycleAction::JslAfterBankPush:
        --s_;
        queueInternalAction(1, CycleAction::JslFetchBank);
        return;
    case CycleAction::JslFetchBank:
        queueDeferredReadAction((static_cast<uint32_t>(pb_) << 16) | pc_, CycleAction::JslBank);
        return;
    case CycleAction::JslPushReturnLow:
        --s_;
        queueDeferredWriteAction(s_, static_cast<uint8_t>(direct_value_), CycleAction::JslFinish);
        return;
    case CycleAction::JslFinish:
        --s_;
        if (emulation_) s_ = static_cast<uint16_t>(0x0100 | (s_ & 0x00FF));
        pb_ = static_cast<uint8_t>(direct_address_);
        pc_ = direct_target_;
        directFinalize();
        return;
    case CycleAction::RtlStart:
        ++s_;
        queueDeferredReadAction(s_, CycleAction::RtlLow);
        return;
    case CycleAction::RtiStart:
        if (emulation_) s_ = static_cast<uint16_t>(0x0100 | ((s_ + 1) & 0x00FF));
        else ++s_;
        queueDeferredReadAction(s_, CycleAction::RtiStatus);
        return;
    case CycleAction::SoftwarePushPCHigh:
        --s_;
        queueDeferredWriteAction(s_, static_cast<uint8_t>(direct_value_ >> 8), CycleAction::SoftwarePushPCLow);
        return;
    case CycleAction::SoftwarePushPCLow:
        if (emulation_) s_ = static_cast<uint16_t>(0x0100 | ((s_ - 1) & 0x00FF));
        else --s_;
        queueDeferredWriteAction(s_, static_cast<uint8_t>(direct_value_), CycleAction::SoftwarePushStatus);
        return;
    case CycleAction::SoftwarePushStatus: {
        if (emulation_) s_ = static_cast<uint16_t>(0x0100 | ((s_ - 1) & 0x00FF));
        else --s_;
        uint8_t status = p_;
        if (emulation_) status |= 0x10;
        queueDeferredWriteAction(s_, status, CycleAction::SoftwareVectorLo);
        return;
    }
    case CycleAction::SoftwareVectorLo:
        if (emulation_) s_ = static_cast<uint16_t>(0x0100 | ((s_ - 1) & 0x00FF));
        else --s_;
        setFlag(Flag_I, true);
        setFlag(Flag_D, false);
        pb_ = 0;
        queueDeferredReadAction(direct_address_, CycleAction::SoftwareVectorLo);
        return;
    case CycleAction::RepSepCommit:
        if (micro_opcode_ == 0xC2) p_ &= static_cast<uint8_t>(~direct_temp8_);
        else p_ |= direct_temp8_;
        normalizeMode();
        directFinalize();
        return;
    case CycleAction::BrlCommit:
        pc_ = direct_target_;
        directFinalize();
        return;
    case CycleAction::BlockAfterWrite:
        queueInternalAction(2, CycleAction::BlockCommit);
        return;
    case CycleAction::BlockCommit:
        db_ = direct_temp8_;
        if (micro_opcode_ == 0x54) {
            if (index8()) { x_ = static_cast<uint8_t>(x_ + 1); y_ = static_cast<uint8_t>(y_ + 1); }
            else { ++x_; ++y_; }
        } else {
            if (index8()) { x_ = static_cast<uint8_t>(x_ - 1); y_ = static_cast<uint8_t>(y_ - 1); }
            else { --x_; --y_; }
        }
        --a_;
        if (a_ != 0xFFFF) pc_ = static_cast<uint16_t>(pc_ - 3);
        directFinalize();
        return;
    case CycleAction::StackPushStart:
        queueDeferredWriteAction(s_, direct_wide_ ? static_cast<uint8_t>(direct_value_ >> 8)
                                                  : static_cast<uint8_t>(direct_value_),
            direct_wide_ ? CycleAction::StackPushNext : CycleAction::StackPushFinish);
        return;
    case CycleAction::StackPushNext:
        if (direct_indexed_) --s_;
        else if (emulation_) s_ = static_cast<uint16_t>(0x0100 | ((s_ - 1) & 0x00FF));
        else --s_;
        queueDeferredWriteAction(s_, static_cast<uint8_t>(direct_value_), CycleAction::StackPushFinish);
        return;
    case CycleAction::StackPushFinish:
        if (direct_indexed_) {
            --s_;
            if (emulation_) s_ = static_cast<uint16_t>(0x0100 | (s_ & 0x00FF));
        } else if (emulation_) {
            s_ = static_cast<uint16_t>(0x0100 | ((s_ - 1) & 0x00FF));
        } else {
            --s_;
        }
        directFinalize();
        return;
    case CycleAction::StackPullStart:
        if (direct_indexed_) ++s_;
        else if (emulation_) s_ = static_cast<uint16_t>(0x0100 | ((s_ + 1) & 0x00FF));
        else ++s_;
        queueDeferredReadAction(s_, CycleAction::StackPullLow);
        return;
    case CycleAction::HardwareBeginStack:
        if (!emulation_) {
            queueDeferredWriteAction(s_, pb_, CycleAction::HardwareAfterBankPush);
        } else {
            queueDeferredWriteAction(s_, static_cast<uint8_t>(direct_value_ >> 8), CycleAction::HardwarePushPCLow);
        }
        return;
    case CycleAction::HardwareAfterBankPush:
        --s_;
        queueDeferredWriteAction(s_, static_cast<uint8_t>(direct_value_ >> 8), CycleAction::HardwarePushPCLow);
        return;
    case CycleAction::HardwarePushPCLow:
        if (emulation_) s_ = static_cast<uint16_t>(0x0100 | ((s_ - 1) & 0x00FF));
        else --s_;
        queueDeferredWriteAction(s_, static_cast<uint8_t>(direct_value_), CycleAction::HardwarePushStatus);
        return;
    case CycleAction::HardwarePushStatus: {
        if (emulation_) s_ = static_cast<uint16_t>(0x0100 | ((s_ - 1) & 0x00FF));
        else --s_;
        uint8_t status = p_;
        if (emulation_) status &= static_cast<uint8_t>(~0x10);
        queueDeferredWriteAction(s_, status, CycleAction::HardwareVectorLo);
        return;
    }
    case CycleAction::HardwareVectorLo:
        if (emulation_) s_ = static_cast<uint16_t>(0x0100 | ((s_ - 1) & 0x00FF));
        else --s_;
        setFlag(Flag_I, true);
        setFlag(Flag_D, false);
        pb_ = 0;
        queueDeferredReadAction(direct_address_, CycleAction::HardwareVectorLo);
        return;
    case CycleAction::ImpliedCommit:
        directApplyImplied();
        directFinalize();
        return;
    case CycleAction::FinishDirect:
        directFinish();
        return;
    default:
        return;
    }
}
void CPU::directCommitStackPull(uint16_t value) {
    switch (micro_opcode_) {
    case 0x28: {
        p_ = static_cast<uint8_t>(value);
        normalizeMode();
        directFinalize();
        irq_poll_i_ = direct_old_i_;
        return;
    }
    case 0x68:
        if (micro_m8_before_) {
            a_ = static_cast<uint16_t>((a_ & 0xFF00) | (value & 0x00FF));
            setNZ8(static_cast<uint8_t>(value));
        } else {
            a_ = value;
            setNZ16(value);
        }
        break;
    case 0xFA:
        x_ = micro_x8_before_ ? static_cast<uint8_t>(value) : value;
        if (micro_x8_before_) setNZ8(static_cast<uint8_t>(value)); else setNZ16(value);
        break;
    case 0x7A:
        y_ = micro_x8_before_ ? static_cast<uint8_t>(value) : value;
        if (micro_x8_before_) setNZ8(static_cast<uint8_t>(value)); else setNZ16(value);
        break;
    case 0xAB:
        db_ = static_cast<uint8_t>(value);
        setNZ8(db_);
        break;
    case 0x2B:
        dp_ = value;
        setNZ16(dp_);
        break;
    default:
        break;
    }
    directFinalize();
}
void CPU::directRmwModify(uint16_t old_value) {
    direct_target_ = old_value;
    const bool wide = direct_wide_;
    if (micro_opcode_ == 0x04 || micro_opcode_ == 0x0C ||
        micro_opcode_ == 0x14 || micro_opcode_ == 0x1C) {
        const uint16_t mask = wide ? 0xFFFFu : 0x00FFu;
        const uint16_t accum = static_cast<uint16_t>(a_ & mask);
        setFlag(Flag_Z, (old_value & accum) == 0);
        direct_value_ = (micro_opcode_ == 0x14 || micro_opcode_ == 0x1C)
            ? static_cast<uint16_t>(old_value & static_cast<uint16_t>(~accum))
            : static_cast<uint16_t>(old_value | accum);
        queueInternalAction(1, CycleAction::RmwAfterModify);
        return;
    }
    switch (micro_opcode_ & 0xE0) {
    case 0x00: direct_value_ = opASL(old_value, wide); break;
    case 0x20: direct_value_ = opROL(old_value, wide); break;
    case 0x40: direct_value_ = opLSR(old_value, wide); break;
    case 0x60: direct_value_ = opROR(old_value, wide); break;
    case 0xC0: direct_value_ = opDEC(old_value, wide); break;
    case 0xE0: direct_value_ = opINC(old_value, wide); break;
    default: direct_value_ = old_value; break;
    }
    if (emulation_) {
        queueDeferredWriteAction(direct_address_, static_cast<uint8_t>(old_value), CycleAction::RmwAfterModify);
    } else {
        const uint32_t dummy_address = wide ? ((direct_address_ + 1) & 0xFFFFFF) : direct_address_;
        queueDeferredReadAction(dummy_address, CycleAction::RmwAfterModify);
    }
}
void CPU::directApplyImplied() {
    switch (micro_opcode_) {
    case 0x0A: if (acc8()) a_=static_cast<uint16_t>((a_&0xFF00)|opASL(static_cast<uint8_t>(a_),false)); else a_=opASL(a_,true); break;
    case 0x2A: if (acc8()) a_=static_cast<uint16_t>((a_&0xFF00)|opROL(static_cast<uint8_t>(a_),false)); else a_=opROL(a_,true); break;
    case 0x4A: if (acc8()) a_=static_cast<uint16_t>((a_&0xFF00)|opLSR(static_cast<uint8_t>(a_),false)); else a_=opLSR(a_,true); break;
    case 0x6A: if (acc8()) a_=static_cast<uint16_t>((a_&0xFF00)|opROR(static_cast<uint8_t>(a_),false)); else a_=opROR(a_,true); break;
    case 0x1A: if (acc8()) a_=static_cast<uint16_t>((a_&0xFF00)|opINC(static_cast<uint8_t>(a_),false)); else a_=opINC(a_,true); break;
    case 0x3A: if (acc8()) a_=static_cast<uint16_t>((a_&0xFF00)|opDEC(static_cast<uint8_t>(a_),false)); else a_=opDEC(a_,true); break;
    case 0x18: setFlag(Flag_C,false); break;
    case 0x38: setFlag(Flag_C,true); break;
    case 0x58: setFlag(Flag_I,false); irq_poll_i_=direct_old_i_; break;
    case 0x78: setFlag(Flag_I,true);  irq_poll_i_=direct_old_i_; break;
    case 0xB8: setFlag(Flag_V,false); break;
    case 0xD8: setFlag(Flag_D,false); break;
    case 0xF8: setFlag(Flag_D,true); break;
    case 0x88: y_=static_cast<uint16_t>((y_-1)&(index8()?0xFF:0xFFFF)); setNZ(y_,!index8()); break;
    case 0xC8: y_=static_cast<uint16_t>((y_+1)&(index8()?0xFF:0xFFFF)); setNZ(y_,!index8()); break;
    case 0xCA: x_=static_cast<uint16_t>((x_-1)&(index8()?0xFF:0xFFFF)); setNZ(x_,!index8()); break;
    case 0xE8: x_=static_cast<uint16_t>((x_+1)&(index8()?0xFF:0xFFFF)); setNZ(x_,!index8()); break;
    case 0xAA: x_=index8()?static_cast<uint8_t>(a_):a_; setNZ(x_,!index8()); break;
    case 0xA8: y_=index8()?static_cast<uint8_t>(a_):a_; setNZ(y_,!index8()); break;
    case 0x8A: if(acc8()) a_=static_cast<uint16_t>((a_&0xFF00)|(x_&0xFF)); else a_=x_; setNZ(a_,!acc8()); break;
    case 0x98: if(acc8()) a_=static_cast<uint16_t>((a_&0xFF00)|(y_&0xFF)); else a_=y_; setNZ(a_,!acc8()); break;
    case 0x9A: s_=x_; if(emulation_) s_=static_cast<uint16_t>(0x0100|(s_&0xFF)); break;
    case 0xBA: x_=index8()?static_cast<uint8_t>(s_):s_; setNZ(x_,!index8()); break;
    case 0x9B: y_=x_; if(index8()) y_&=0xFF; setNZ(y_,!index8()); break;
    case 0xBB: x_=y_; if(index8()) x_&=0xFF; setNZ(x_,!index8()); break;
    case 0x5B: dp_=a_; setNZ16(dp_); break;
    case 0x7B: a_=dp_; setNZ16(a_); break;
    case 0x1B: s_=a_; if(emulation_) s_=static_cast<uint16_t>(0x0100|(s_&0xFF)); break;
    case 0x3B: a_=s_; setNZ16(a_); break;
    case 0xEB: { const uint8_t lo=static_cast<uint8_t>(a_), hi=static_cast<uint8_t>(a_>>8); a_=static_cast<uint16_t>((static_cast<uint16_t>(lo)<<8)|hi); setNZ8(static_cast<uint8_t>(a_)); } break;
    case 0xFB: { const bool old_c=flag(Flag_C); setFlag(Flag_C,emulation_); emulation_=old_c; normalizeMode(); } break;
    case 0xCB: waiting_=true; break;
    case 0xDB: stopped_=true; break;
    case 0xEA: break;
    default: break;
    }
}
void CPU::microApplyReadOperation(uint8_t opcode, uint16_t value) {
    switch (opcode) {
    case 0x01: case 0x03: case 0x05: case 0x07: case 0x09: case 0x0D: case 0x0F:
    case 0x11: case 0x12: case 0x13: case 0x15: case 0x17: case 0x19: case 0x1D: case 0x1F:
        opORA(value); break;
    case 0x21: case 0x23: case 0x25: case 0x27: case 0x29: case 0x2D: case 0x2F:
    case 0x31: case 0x32: case 0x33: case 0x35: case 0x37: case 0x39: case 0x3D: case 0x3F:
        opAND(value); break;
    case 0x41: case 0x43: case 0x45: case 0x47: case 0x49: case 0x4D: case 0x4F:
    case 0x51: case 0x52: case 0x53: case 0x55: case 0x57: case 0x59: case 0x5D: case 0x5F:
        opEOR(value); break;
    case 0x61: case 0x63: case 0x65: case 0x67: case 0x69: case 0x6D: case 0x6F:
    case 0x71: case 0x72: case 0x73: case 0x75: case 0x77: case 0x79: case 0x7D: case 0x7F:
        opADC(value); break;
    case 0x89: opBIT(value, true); break;
    case 0x24: case 0x2C: case 0x34: case 0x3C: opBIT(value, false); break;
    case 0xA1: case 0xA3: case 0xA5: case 0xA7: case 0xA9: case 0xAD: case 0xAF:
    case 0xB1: case 0xB2: case 0xB3: case 0xB5: case 0xB7: case 0xB9: case 0xBD: case 0xBF:
        if (acc8()) a_ = static_cast<uint16_t>((a_ & 0xFF00) | (value & 0xFF)); else a_ = value;
        setNZ(a_, !acc8());
        break;
    case 0xA0: case 0xA4: case 0xAC: case 0xB4: case 0xBC:
        y_ = index8() ? static_cast<uint8_t>(value) : value; setNZ(y_, !index8()); break;
    case 0xA2: case 0xA6: case 0xAE: case 0xB6: case 0xBE:
        x_ = index8() ? static_cast<uint8_t>(value) : value; setNZ(x_, !index8()); break;
    case 0xC0: case 0xC4: case 0xCC: opCMP(y_, value, !index8()); break;
    case 0xC1: case 0xC3: case 0xC5: case 0xC7: case 0xC9: case 0xCD: case 0xCF:
    case 0xD1: case 0xD2: case 0xD3: case 0xD5: case 0xD7: case 0xD9: case 0xDD: case 0xDF:
        opCMP(a_, value, !acc8()); break;
    case 0xE0: case 0xE4: case 0xEC: opCMP(x_, value, !index8()); break;
    case 0xE1: case 0xE3: case 0xE5: case 0xE7: case 0xE9: case 0xED: case 0xEF:
    case 0xF1: case 0xF2: case 0xF3: case 0xF5: case 0xF7: case 0xF9: case 0xFD: case 0xFF:
        opSBC(value); break;
    default: break;
    }
}
void CPU::onOpcodeFetched(uint8_t opcode) {
    last_opcode_ = opcode;
    last_step_executed_opcode_ = true;
    micro_opcode_ = opcode;
    page_crossed_ = false;
    if (startDirectInstruction(opcode)) {
        return;
    }
    ++legacy_fallback_count_;
    micro_active_ = false;
    last_master_clocks_ = 0;
    access_penalty_master_clocks_ = 0;
    timing_extra_cycles_ = 0;
    timing_base_cycles_ = baseCycles(opcode);
    page_crossed_ = false;
    recorded_bus_cycles_ = 1;
    recording_cycles_ = true;
    const bool m8_before = micro_m8_before_;
    const bool x8_before = micro_x8_before_;
    const bool i_before = flag(Flag_I);
    const uint16_t dp_before = micro_dp_before_;
    executeInstruction(opcode);
    timing_extra_cycles_ += modePenaltyCycles(opcode, m8_before, x8_before, dp_before);
    switch (opcode) {
    case 0x28: case 0x58: case 0x78: case 0xC2: case 0xE2: irq_poll_i_ = i_before; break;
    default: irq_poll_i_ = flag(Flag_I); break;
    }
    if ((opcode == 0x00 || opcode == 0x02) && !emulation_) ++timing_extra_cycles_;
    const uint32_t cpu_cycles = static_cast<uint32_t>(timing_base_cycles_) + timing_extra_cycles_;
    cycles_ += cpu_cycles;
    finishCycleQueue(cpu_cycles);
    recording_cycles_ = false;
}
uint8_t CPU::read8(uint32_t a) const {
    if (!hasMemoryBus()) return 0xFF;
    a &= 0xFFFFFF;
    if (!bus_ && !cycle_stepping_ && external_bus_.read_timed) {
        uint8_t clocks = internal_cycle_master_clocks_;
        const uint8_t value = external_bus_.read_timed(external_bus_.context, a, &clocks);
        if (clocks > internal_cycle_master_clocks_) {
            access_penalty_master_clocks_ += static_cast<uint32_t>(clocks - internal_cycle_master_clocks_);
        }
        return value;
    }
    const uint8_t clocks = memoryAccessMasterClocks(a);
    if (cycle_stepping_ && recording_cycles_) {
        recordBusCycle(a);
    } else if (clocks > internal_cycle_master_clocks_) {
        access_penalty_master_clocks_ += static_cast<uint32_t>(clocks - internal_cycle_master_clocks_);
    }
    return memoryRead(a);
}
uint16_t CPU::read16(uint32_t a) const {
    const uint8_t lo = read8(a);
    const uint8_t hi = read8((a + 1) & 0xFFFFFF);
    return static_cast<uint16_t>(lo | (static_cast<uint16_t>(hi) << 8));
}
uint16_t CPU::read16BankWrap(uint32_t a) const {
    const uint32_t bank = a & 0xFF0000;
    const uint16_t off = static_cast<uint16_t>(a);
    return static_cast<uint16_t>(read8(bank | off) | (static_cast<uint16_t>(read8(bank | static_cast<uint16_t>(off + 1))) << 8));
}
uint32_t CPU::read24(uint32_t a) const {
    return static_cast<uint32_t>(read8(a)) |
           (static_cast<uint32_t>(read8((a + 1) & 0xFFFFFF)) << 8) |
           (static_cast<uint32_t>(read8((a + 2) & 0xFFFFFF)) << 16);
}
void CPU::write8(uint32_t a, uint8_t v) {
    if (!hasMemoryBus()) return;
    a &= 0xFFFFFF;
    if (!bus_ && !cycle_stepping_ && external_bus_.write_timed) {
        uint8_t clocks = internal_cycle_master_clocks_;
        external_bus_.write_timed(external_bus_.context, a, v, &clocks);
        if (clocks > internal_cycle_master_clocks_) {
            access_penalty_master_clocks_ += static_cast<uint32_t>(clocks - internal_cycle_master_clocks_);
        }
        return;
    }
    const uint8_t clocks = memoryAccessMasterClocks(a);
    if (cycle_stepping_ && recording_cycles_) {
        recordBusCycle(a, true, v);
        return;
    }
    if (clocks > internal_cycle_master_clocks_) access_penalty_master_clocks_ += static_cast<uint32_t>(clocks - internal_cycle_master_clocks_);
    memoryWrite(a, v);
}
void CPU::write16(uint32_t a, uint16_t v) {
    write8(a, static_cast<uint8_t>(v));
    write8((a + 1) & 0xFFFFFF, static_cast<uint8_t>(v >> 8));
}
uint8_t CPU::fetch8() {
    const uint8_t v = read8((static_cast<uint32_t>(pb_) << 16) | pc_);
    ++pc_;
    return v;
}
uint16_t CPU::fetch16() { const uint8_t l = fetch8(); return static_cast<uint16_t>(l | (static_cast<uint16_t>(fetch8()) << 8)); }
uint32_t CPU::fetch24() { const uint16_t l = fetch16(); return static_cast<uint32_t>(l) | (static_cast<uint32_t>(fetch8()) << 16); }
void CPU::push8(uint8_t v) {
    write8(s_, v);
    if (emulation_) s_ = static_cast<uint16_t>(0x0100 | ((s_ - 1) & 0xFF)); else --s_;
}
void CPU::push16(uint16_t v) { push8(static_cast<uint8_t>(v >> 8)); push8(static_cast<uint8_t>(v)); }
void CPU::push16Linear(uint16_t v) {
    write8(s_, static_cast<uint8_t>(v >> 8)); --s_;
    write8(s_, static_cast<uint8_t>(v)); --s_;
    if (emulation_) s_ = static_cast<uint16_t>(0x0100 | (s_ & 0x00FF));
}
uint8_t CPU::pull8() {
    if (emulation_) s_ = static_cast<uint16_t>(0x0100 | ((s_ + 1) & 0xFF)); else ++s_;
    return read8(s_);
}
uint8_t CPU::pull8Linear() {
    ++s_;
    const uint8_t value = read8(s_);
    if (emulation_) s_ = static_cast<uint16_t>(0x0100 | (s_ & 0x00FF));
    return value;
}
uint16_t CPU::pull16() { const uint8_t l = pull8(); return static_cast<uint16_t>(l | (static_cast<uint16_t>(pull8()) << 8)); }
uint16_t CPU::pull16Linear() {
    ++s_; const uint8_t lo = read8(s_);
    ++s_; const uint8_t hi = read8(s_);
    if (emulation_) s_ = static_cast<uint16_t>(0x0100 | (s_ & 0x00FF));
    return static_cast<uint16_t>(lo | (static_cast<uint16_t>(hi) << 8));
}
void CPU::setNZ8(uint8_t v) { setFlag(Flag_Z, v == 0); setFlag(Flag_N, (v & 0x80) != 0); }
void CPU::setNZ16(uint16_t v) { setFlag(Flag_Z, v == 0); setFlag(Flag_N, (v & 0x8000) != 0); }
void CPU::setNZ(uint16_t v, bool wide) { if (wide) setNZ16(v); else setNZ8(static_cast<uint8_t>(v)); }
uint16_t CPU::readM(uint32_t a) const { return acc8() ? read8(a) : read16(a); }
void CPU::writeM(uint32_t a, uint16_t v) { if (acc8()) write8(a, static_cast<uint8_t>(v)); else write16(a, v); }
uint16_t CPU::readX(uint32_t a) const { return index8() ? read8(a) : read16(a); }
void CPU::writeX(uint32_t a, uint16_t v) { if (index8()) write8(a, static_cast<uint8_t>(v)); else write16(a, v); }
uint16_t CPU::fetchM() { return acc8() ? fetch8() : fetch16(); }
uint16_t CPU::fetchX() { return index8() ? fetch8() : fetch16(); }
uint16_t CPU::directAddress(uint8_t operand, uint16_t index) const {
    if (emulation_ && (dp_ & 0x00FF) == 0) {
        return static_cast<uint16_t>((dp_ & 0xFF00) |
            static_cast<uint8_t>(static_cast<uint16_t>(operand) + index));
    }
    return static_cast<uint16_t>(dp_ + operand + index);
}
uint16_t CPU::directPointerAddress(uint16_t base, uint8_t delta, bool linear_exception) const {
    if (!linear_exception && emulation_ && (dp_ & 0x00FF) == 0) {
        return static_cast<uint16_t>((dp_ & 0xFF00) |
            static_cast<uint8_t>((base & 0x00FF) + delta));
    }
    return static_cast<uint16_t>(base + delta);
}
uint32_t CPU::addrDP() { return directAddress(fetch8()); }
uint32_t CPU::addrDPX() { return directAddress(fetch8(), x_); }
uint32_t CPU::addrDPY() { return directAddress(fetch8(), y_); }
uint32_t CPU::addrAbs() { return (static_cast<uint32_t>(db_) << 16) | fetch16(); }
uint32_t CPU::addrAbsX() {
    const uint16_t base = fetch16();
    page_crossed_ = ((base & 0xFF00) != (static_cast<uint16_t>(base + x_) & 0xFF00));
    return (((static_cast<uint32_t>(db_) << 16) | base) + x_) & 0xFFFFFF;
}
uint32_t CPU::addrAbsY() {
    const uint16_t base = fetch16();
    page_crossed_ = ((base & 0xFF00) != (static_cast<uint16_t>(base + y_) & 0xFF00));
    return (((static_cast<uint32_t>(db_) << 16) | base) + y_) & 0xFFFFFF;
}
uint32_t CPU::addrLong() { return fetch24(); }
uint32_t CPU::addrLongX() { return (fetch24() + x_) & 0xFFFFFF; }
uint32_t CPU::addrDPIndirect() {
    const uint16_t p = directAddress(fetch8());
    const uint8_t lo = read8(p);
    const uint8_t hi = read8(directPointerAddress(p, 1));
    return (static_cast<uint32_t>(db_) << 16) | static_cast<uint16_t>(lo | (static_cast<uint16_t>(hi) << 8));
}
uint32_t CPU::addrDPIndirectX() {
    const uint16_t p = directAddress(fetch8(), x_);
    const uint8_t lo = read8(p);
    const uint16_t hi_addr = emulation_
        ? static_cast<uint16_t>((p & 0xFF00) | static_cast<uint8_t>((p & 0x00FF) + 1))
        : static_cast<uint16_t>(p + 1);
    const uint8_t hi = read8(hi_addr);
    return (static_cast<uint32_t>(db_) << 16) | static_cast<uint16_t>(lo | (static_cast<uint16_t>(hi) << 8));
}
uint32_t CPU::addrDPIndirectY() {
    const uint16_t p = directAddress(fetch8());
    const uint8_t lo = read8(p);
    const uint8_t hi = read8(directPointerAddress(p, 1));
    const uint16_t base = static_cast<uint16_t>(lo | (static_cast<uint16_t>(hi) << 8));
    const uint16_t off = static_cast<uint16_t>(base + y_);
    page_crossed_ = ((base & 0xFF00) != (off & 0xFF00));
    return (((static_cast<uint32_t>(db_) << 16) | base) + y_) & 0xFFFFFF;
}
uint32_t CPU::addrDPIndirectLong() {
    const uint16_t p = directAddress(fetch8());
    const uint32_t lo = read8(p);
    const uint32_t hi = read8(directPointerAddress(p, 1, true));
    const uint32_t bank = read8(directPointerAddress(p, 2, true));
    return lo | (hi << 8) | (bank << 16);
}
uint32_t CPU::addrDPIndirectLongY() { return (addrDPIndirectLong() + y_) & 0xFFFFFF; }
uint32_t CPU::addrStackRelative() { return static_cast<uint16_t>(s_ + fetch8()); }
uint32_t CPU::addrStackRelativeIndirectY() { const uint16_t p = static_cast<uint16_t>(s_ + fetch8()); return (((static_cast<uint32_t>(db_) << 16) | read16BankWrap(p)) + y_) & 0xFFFFFF; }
void CPU::branch(bool condition) {
    const int8_t d = static_cast<int8_t>(fetch8());
    if (condition) {
        const uint16_t old_pc = pc_;
        pc_ = static_cast<uint16_t>(pc_ + d);
        ++timing_extra_cycles_;
        if (emulation_ && ((old_pc ^ pc_) & 0xFF00) != 0) {
            ++timing_extra_cycles_;
        }
    }
}
void CPU::interrupt(uint16_t native_vector, uint16_t emu_vector, bool software, bool break_flag) {
    waiting_ = false;
    if (!software) {
        (void)read8((static_cast<uint32_t>(pb_) << 16) | pc_);
    }
    if (!emulation_) push8(pb_);
    push16(pc_);
    uint8_t status = p_;
    if (emulation_) {
        if (break_flag) status |= 0x10; else status &= static_cast<uint8_t>(~0x10);
    }
    push8(status);
    setFlag(Flag_I, true);
    setFlag(Flag_D, false);
    pb_ = 0;
    pc_ = read16(emulation_ ? emu_vector : native_vector);
    (void)software;
}
bool CPU::serviceInterrupts() {
    if (nmi_pending_) {
        nmi_pending_ = false;
        timing_base_cycles_ = static_cast<uint8_t>(emulation_ ? 7 : 8);
        interrupt(0xFFEA, 0xFFFA, false, false);
        irq_poll_i_ = true;
        return true;
    }
    if (irq_pending_) {
        waiting_ = false;
        timing_base_cycles_ = static_cast<uint8_t>(emulation_ ? 7 : 8);
        interrupt(0xFFEE, 0xFFFE, false, false);
        irq_pending_ = false;
        irq_poll_i_ = true;
        return true;
    }
    return false;
}
void CPU::opORA(uint16_t v) { if (acc8()) { a_ = static_cast<uint16_t>((a_ & 0xFF00) | (static_cast<uint8_t>(a_) | static_cast<uint8_t>(v))); setNZ8(static_cast<uint8_t>(a_)); } else { a_ |= v; setNZ16(a_); } }
void CPU::opAND(uint16_t v) { if (acc8()) { a_ = static_cast<uint16_t>((a_ & 0xFF00) | (static_cast<uint8_t>(a_) & static_cast<uint8_t>(v))); setNZ8(static_cast<uint8_t>(a_)); } else { a_ &= v; setNZ16(a_); } }
void CPU::opEOR(uint16_t v) { if (acc8()) { a_ = static_cast<uint16_t>((a_ & 0xFF00) | (static_cast<uint8_t>(a_) ^ static_cast<uint8_t>(v))); setNZ8(static_cast<uint8_t>(a_)); } else { a_ ^= v; setNZ16(a_); } }
void CPU::opADC(uint16_t v) {
    const bool wide = !acc8();
    const auto result = cpu_alu::adc(a_, v, flag(Flag_C), wide, flag(Flag_D));
    setFlag(Flag_C, result.carry);
    setFlag(Flag_V, result.overflow);
    setFlag(Flag_N, result.negative);
    setFlag(Flag_Z, result.zero);
    if (wide) {
        a_ = result.value;
    } else {
        a_ = static_cast<uint16_t>((a_ & 0xFF00u) | (result.value & 0x00FFu));
    }
}
void CPU::opSBC(uint16_t v) {
    const bool wide = !acc8();
    const auto result = cpu_alu::sbc(a_, v, flag(Flag_C), wide, flag(Flag_D));
    setFlag(Flag_C, result.carry);
    setFlag(Flag_V, result.overflow);
    setFlag(Flag_N, result.negative);
    setFlag(Flag_Z, result.zero);
    if (wide) {
        a_ = result.value;
    } else {
        a_ = static_cast<uint16_t>((a_ & 0xFF00u) | (result.value & 0x00FFu));
    }
}
void CPU::opCMP(uint16_t lhs, uint16_t rhs, bool wide) {
    const uint32_t mask = wide ? 0xFFFFu : 0xFFu;
    const uint32_t l = lhs & mask, r = rhs & mask, d = (l - r) & mask;
    setFlag(Flag_C, l >= r); setNZ(static_cast<uint16_t>(d), wide);
}
void CPU::opBIT(uint16_t v, bool immediate) {
    const bool wide = !acc8(); const uint16_t mask = wide ? 0xFFFF : 0x00FF;
    setFlag(Flag_Z, (a_ & v & mask) == 0);
    if (!immediate) { setFlag(Flag_N, (v & (wide ? 0x8000 : 0x80)) != 0); setFlag(Flag_V, (v & (wide ? 0x4000 : 0x40)) != 0); }
}
uint16_t CPU::opASL(uint16_t v, bool wide) { const uint16_t m=wide?0xFFFF:0xFF, s=wide?0x8000:0x80; setFlag(Flag_C,(v&s)!=0); v=static_cast<uint16_t>((v<<1)&m); setNZ(v,wide); return v; }
uint16_t CPU::opLSR(uint16_t v, bool wide) { setFlag(Flag_C,(v&1)!=0); v=static_cast<uint16_t>(v>>1); if(!wide)v&=0xFF; setNZ(v,wide); return v; }
uint16_t CPU::opROL(uint16_t v, bool wide) { const bool c=flag(Flag_C); const uint16_t s=wide?0x8000:0x80,m=wide?0xFFFF:0xFF; setFlag(Flag_C,(v&s)!=0); v=static_cast<uint16_t>(((v<<1)|(c?1:0))&m); setNZ(v,wide); return v; }
uint16_t CPU::opROR(uint16_t v, bool wide) { const bool c=flag(Flag_C); setFlag(Flag_C,(v&1)!=0); v=static_cast<uint16_t>((v>>1)|(c?(wide?0x8000:0x80):0)); if(!wide)v&=0xFF; setNZ(v,wide); return v; }
uint16_t CPU::opINC(uint16_t v, bool wide) { v=static_cast<uint16_t>((v+1)&(wide?0xFFFF:0xFF)); setNZ(v,wide); return v; }
uint16_t CPU::opDEC(uint16_t v, bool wide) { v=static_cast<uint16_t>((v-1)&(wide?0xFFFF:0xFF)); setNZ(v,wide); return v; }
uint8_t CPU::baseCycles(uint8_t opcode) {
    static constexpr uint8_t kCycles[256] = {
        7,6,7,4,5,3,5,6,3,2,2,4,6,4,6,5,
        2,5,5,7,5,4,6,6,2,4,2,2,6,4,7,5,
        6,6,8,4,3,3,5,6,4,2,2,5,4,4,6,5,
        2,5,5,7,4,4,6,6,2,4,2,2,4,4,7,5,
        6,6,2,4,7,3,5,6,3,2,2,3,3,4,6,5,
        2,5,5,7,7,4,6,6,2,4,3,2,4,4,7,5,
        6,6,6,4,3,3,5,6,4,2,2,6,5,4,6,5,
        2,5,5,7,4,4,6,6,2,4,4,2,6,4,7,5,
        3,6,4,4,3,3,3,6,2,2,2,3,4,4,4,5,
        2,6,5,7,4,4,4,6,2,5,2,2,4,5,5,5,
        2,6,2,4,3,3,3,6,2,2,2,4,4,4,4,5,
        2,5,5,7,4,4,4,6,2,4,2,2,4,4,4,5,
        2,6,3,4,3,3,5,6,2,2,2,3,4,4,6,5,
        2,5,5,7,6,4,6,6,2,4,3,3,6,4,7,5,
        2,6,3,4,3,3,5,6,2,2,2,3,4,4,6,5,
        2,5,5,7,5,4,6,6,2,4,4,2,8,4,7,5
    };
    return kCycles[opcode];
}
bool CPU::hasDirectPagePenalty(uint8_t op) {
    switch (op) {
    case 0x01: case 0x04: case 0x05: case 0x06: case 0x07:
    case 0x11: case 0x12: case 0x14: case 0x15: case 0x16: case 0x17:
    case 0x21: case 0x24: case 0x25: case 0x26: case 0x27:
    case 0x31: case 0x32: case 0x34: case 0x35: case 0x36: case 0x37:
    case 0x41: case 0x45: case 0x46: case 0x47:
    case 0x51: case 0x52: case 0x55: case 0x56: case 0x57:
    case 0x61: case 0x64: case 0x65: case 0x66: case 0x67:
    case 0x71: case 0x72: case 0x74: case 0x75: case 0x76: case 0x77:
    case 0x81: case 0x84: case 0x85: case 0x86: case 0x87:
    case 0x91: case 0x92: case 0x94: case 0x95: case 0x96: case 0x97:
    case 0xA1: case 0xA4: case 0xA5: case 0xA6: case 0xA7:
    case 0xB1: case 0xB2: case 0xB4: case 0xB5: case 0xB6: case 0xB7:
    case 0xC1: case 0xC4: case 0xC5: case 0xC6: case 0xC7:
    case 0xD1: case 0xD2: case 0xD4: case 0xD5: case 0xD6: case 0xD7:
    case 0xE1: case 0xE4: case 0xE5: case 0xE6: case 0xE7:
    case 0xF1: case 0xF2: case 0xF5: case 0xF6: case 0xF7:
        return true;
    default: return false;
    }
}
bool CPU::hasMWidthPenalty(uint8_t op) {
    const uint8_t low = static_cast<uint8_t>(op & 0x1F);
    switch (low) {
    case 0x01: case 0x03: case 0x05: case 0x07: case 0x09: case 0x0D: case 0x0F:
    case 0x11: case 0x12: case 0x13: case 0x15: case 0x17: case 0x19: case 0x1D: case 0x1F:
        return true;
    default: break;
    }
    switch (op) {
    case 0x04: case 0x0C: case 0x14: case 0x1C:
    case 0x24: case 0x2C: case 0x34: case 0x3C: case 0x89:
    case 0x48: case 0x68:
        return true;
    default: return false;
    }
}
bool CPU::hasMWidthDoublePenalty(uint8_t op) {
    switch (op) {
    case 0x06: case 0x0E: case 0x16: case 0x1E:
    case 0x26: case 0x2E: case 0x36: case 0x3E:
    case 0x46: case 0x4E: case 0x56: case 0x5E:
    case 0x66: case 0x6E: case 0x76: case 0x7E:
    case 0xC6: case 0xCE: case 0xD6: case 0xDE:
    case 0xE6: case 0xEE: case 0xF6: case 0xFE:
        return true;
    default: return false;
    }
}
bool CPU::hasXWidthPenalty(uint8_t op) {
    switch (op) {
    case 0x84: case 0x86: case 0x8C: case 0x8E: case 0x94: case 0x96:
    case 0xA0: case 0xA2: case 0xA4: case 0xA6: case 0xAC: case 0xAE:
    case 0xB4: case 0xB6: case 0xBC: case 0xBE:
    case 0xC0: case 0xC4: case 0xCC: case 0xE0: case 0xE4: case 0xEC:
    case 0x5A: case 0x7A: case 0xDA: case 0xFA:
        return true;
    default: return false;
    }
}
bool CPU::hasPageCrossPenalty(uint8_t op) {
    switch (op) {
    case 0x11: case 0x19: case 0x1D:
    case 0x31: case 0x39: case 0x3D:
    case 0x51: case 0x59: case 0x5D:
    case 0x71: case 0x79: case 0x7D:
    case 0xB1: case 0xB9: case 0xBD:
    case 0xD1: case 0xD9: case 0xDD:
    case 0xF1: case 0xF9: case 0xFD:
    case 0xBC: case 0xBE:
        return true;
    default: return false;
    }
}
uint8_t CPU::modePenaltyCycles(uint8_t opcode, bool m8_before, bool x8_before, uint16_t dp_before) const {
    uint8_t extra = 0;
    if (!m8_before) {
        if (hasMWidthDoublePenalty(opcode)) extra = static_cast<uint8_t>(extra + 2);
        else if (hasMWidthPenalty(opcode)) ++extra;
    }
    if (!x8_before && hasXWidthPenalty(opcode)) ++extra;
    if ((dp_before & 0x00FF) != 0 && hasDirectPagePenalty(opcode)) ++extra;
    if (hasPageCrossPenalty(opcode) && (page_crossed_ || !x8_before)) ++extra;
    return extra;
}
void CPU::step() {
    if (cycle_stepping_ && hasPendingCycles()) return;
    last_master_clocks_ = 0;
    last_step_executed_opcode_ = false;
    access_penalty_master_clocks_ = 0;
    timing_extra_cycles_ = 0;
    timing_base_cycles_ = 0;
    page_crossed_ = false;
    recorded_bus_cycles_ = 0;
    cycle_events_.clear();
    recording_cycles_ = cycle_stepping_;
    if (!hasMemoryBus() || stopped_) {
        recording_cycles_ = false;
        return;
    }
    if (waiting_) {
        if (nmi_pending_ || nmi_transition_ || irq_line_) {
            waiting_ = false;
            timing_base_cycles_ = 2;
            cycles_ += 2;
            last_master_clocks_ = static_cast<uint32_t>(2u * internal_cycle_master_clocks_);
            finishCycleQueue(2);
            recording_cycles_ = false;
            irq_poll_i_ = flag(Flag_I);
            return;
        }
        timing_base_cycles_ = 1;
        ++cycles_;
        last_master_clocks_ = internal_cycle_master_clocks_;
        finishCycleQueue(1);
        recording_cycles_ = false;
        return;
    }
    if (cycle_stepping_) {
        if (nmi_pending_) {
            recording_cycles_ = false;
            startHardwareInterrupt(true);
            return;
        }
        if (irq_pending_) {
            waiting_ = false;
            recording_cycles_ = false;
            startHardwareInterrupt(false);
            return;
        }
    } else {
        irq_pending_ = irq_line_ && !irq_poll_i_;
        if (serviceInterrupts()) {
            const uint32_t cpu_cycles = static_cast<uint32_t>(timing_base_cycles_) + timing_extra_cycles_;
            cycles_ += cpu_cycles;
            last_master_clocks_ = cpu_cycles * static_cast<uint32_t>(internal_cycle_master_clocks_) + access_penalty_master_clocks_;
            finishCycleQueue(cpu_cycles);
            recording_cycles_ = false;
            return;
        }
    }
    if (cycle_stepping_) {
        recording_cycles_ = false;
        queueOpcodeFetch();
        return;
    }
    const bool m8_before = acc8();
    const bool x8_before = index8();
    const bool i_before = flag(Flag_I);
    const uint16_t dp_before = dp_;
    const uint8_t opcode = fetch8();
    last_opcode_ = opcode;
    last_step_executed_opcode_ = true;
    timing_base_cycles_ = baseCycles(opcode);
    executeInstruction(opcode);
    timing_extra_cycles_ += modePenaltyCycles(opcode, m8_before, x8_before, dp_before);
    switch (opcode) {
    case 0x28:
    case 0x58:
    case 0x78:
    case 0xC2:
    case 0xE2:
        irq_poll_i_ = i_before;
        break;
    default:
        irq_poll_i_ = flag(Flag_I);
        break;
    }
    if ((opcode == 0x00 || opcode == 0x02) && !emulation_) {
        ++timing_extra_cycles_;
    }
    const uint32_t cpu_cycles = static_cast<uint32_t>(timing_base_cycles_) + timing_extra_cycles_;
    cycles_ += cpu_cycles;
    last_master_clocks_ = cpu_cycles * static_cast<uint32_t>(internal_cycle_master_clocks_) + access_penalty_master_clocks_;
    finishCycleQueue(cpu_cycles);
    recording_cycles_ = false;
}
#define RM(addrfn) readM(addrfn())
#define RX(addrfn) readX(addrfn())
#define STORE_M(addrfn,val) do { const uint32_t ea=(addrfn)(); writeM(ea,(val)); } while(0)
#define STORE_X(addrfn,val) do { const uint32_t ea=(addrfn)(); writeX(ea,(val)); } while(0)
#define LDA(v) do { const uint16_t q=(v); if(acc8()) a_=static_cast<uint16_t>((a_&0xFF00)|(q&0xFF)); else a_=q; setNZ(a_,!acc8()); } while(0)
#define LDX(v) do { x_=index8()?static_cast<uint8_t>(v):static_cast<uint16_t>(v); setNZ(x_,!index8()); } while(0)
#define LDY(v) do { y_=index8()?static_cast<uint8_t>(v):static_cast<uint16_t>(v); setNZ(y_,!index8()); } while(0)
#define MEM_RMW(addrfn,op) do { const uint32_t ea=(addrfn)(); uint16_t q=readM(ea); q=op(q,!acc8()); writeM(ea,q); } while(0)
void CPU::executeInstruction(uint8_t op) {
    switch (op) {
    case 0x01: opORA(RM(addrDPIndirectX)); break; case 0x03: opORA(RM(addrStackRelative)); break;
    case 0x05: opORA(RM(addrDP)); break; case 0x07: opORA(RM(addrDPIndirectLong)); break;
    case 0x09: opORA(fetchM()); break; case 0x0D: opORA(RM(addrAbs)); break; case 0x0F: opORA(RM(addrLong)); break;
    case 0x11: opORA(RM(addrDPIndirectY)); break; case 0x12: opORA(RM(addrDPIndirect)); break; case 0x13: opORA(RM(addrStackRelativeIndirectY)); break;
    case 0x15: opORA(RM(addrDPX)); break; case 0x17: opORA(RM(addrDPIndirectLongY)); break; case 0x19: opORA(RM(addrAbsY)); break; case 0x1D: opORA(RM(addrAbsX)); break; case 0x1F: opORA(RM(addrLongX)); break;
    case 0x21: opAND(RM(addrDPIndirectX)); break; case 0x23: opAND(RM(addrStackRelative)); break; case 0x25: opAND(RM(addrDP)); break; case 0x27: opAND(RM(addrDPIndirectLong)); break;
    case 0x29: opAND(fetchM()); break; case 0x2D: opAND(RM(addrAbs)); break; case 0x2F: opAND(RM(addrLong)); break; case 0x31: opAND(RM(addrDPIndirectY)); break;
    case 0x32: opAND(RM(addrDPIndirect)); break; case 0x33: opAND(RM(addrStackRelativeIndirectY)); break; case 0x35: opAND(RM(addrDPX)); break; case 0x37: opAND(RM(addrDPIndirectLongY)); break;
    case 0x39: opAND(RM(addrAbsY)); break; case 0x3D: opAND(RM(addrAbsX)); break; case 0x3F: opAND(RM(addrLongX)); break;
    case 0x41: opEOR(RM(addrDPIndirectX)); break; case 0x43: opEOR(RM(addrStackRelative)); break; case 0x45: opEOR(RM(addrDP)); break; case 0x47: opEOR(RM(addrDPIndirectLong)); break;
    case 0x49: opEOR(fetchM()); break; case 0x4D: opEOR(RM(addrAbs)); break; case 0x4F: opEOR(RM(addrLong)); break; case 0x51: opEOR(RM(addrDPIndirectY)); break;
    case 0x52: opEOR(RM(addrDPIndirect)); break; case 0x53: opEOR(RM(addrStackRelativeIndirectY)); break; case 0x55: opEOR(RM(addrDPX)); break; case 0x57: opEOR(RM(addrDPIndirectLongY)); break;
    case 0x59: opEOR(RM(addrAbsY)); break; case 0x5D: opEOR(RM(addrAbsX)); break; case 0x5F: opEOR(RM(addrLongX)); break;
    case 0x61: opADC(RM(addrDPIndirectX)); break; case 0x63: opADC(RM(addrStackRelative)); break; case 0x65: opADC(RM(addrDP)); break; case 0x67: opADC(RM(addrDPIndirectLong)); break;
    case 0x69: opADC(fetchM()); break; case 0x6D: opADC(RM(addrAbs)); break; case 0x6F: opADC(RM(addrLong)); break; case 0x71: opADC(RM(addrDPIndirectY)); break;
    case 0x72: opADC(RM(addrDPIndirect)); break; case 0x73: opADC(RM(addrStackRelativeIndirectY)); break; case 0x75: opADC(RM(addrDPX)); break; case 0x77: opADC(RM(addrDPIndirectLongY)); break;
    case 0x79: opADC(RM(addrAbsY)); break; case 0x7D: opADC(RM(addrAbsX)); break; case 0x7F: opADC(RM(addrLongX)); break;
    case 0x81: STORE_M(addrDPIndirectX,a_); break; case 0x83: STORE_M(addrStackRelative,a_); break; case 0x85: STORE_M(addrDP,a_); break; case 0x87: STORE_M(addrDPIndirectLong,a_); break;
    case 0x8D: STORE_M(addrAbs,a_); break; case 0x8F: STORE_M(addrLong,a_); break; case 0x91: STORE_M(addrDPIndirectY,a_); break; case 0x92: STORE_M(addrDPIndirect,a_); break;
    case 0x93: STORE_M(addrStackRelativeIndirectY,a_); break; case 0x95: STORE_M(addrDPX,a_); break; case 0x97: STORE_M(addrDPIndirectLongY,a_); break; case 0x99: STORE_M(addrAbsY,a_); break;
    case 0x9D: STORE_M(addrAbsX,a_); break; case 0x9F: STORE_M(addrLongX,a_); break;
    case 0xA1: LDA(RM(addrDPIndirectX)); break; case 0xA3: LDA(RM(addrStackRelative)); break; case 0xA5: LDA(RM(addrDP)); break; case 0xA7: LDA(RM(addrDPIndirectLong)); break;
    case 0xA9: LDA(fetchM()); break; case 0xAD: LDA(RM(addrAbs)); break; case 0xAF: LDA(RM(addrLong)); break; case 0xB1: LDA(RM(addrDPIndirectY)); break;
    case 0xB2: LDA(RM(addrDPIndirect)); break; case 0xB3: LDA(RM(addrStackRelativeIndirectY)); break; case 0xB5: LDA(RM(addrDPX)); break; case 0xB7: LDA(RM(addrDPIndirectLongY)); break;
    case 0xB9: LDA(RM(addrAbsY)); break; case 0xBD: LDA(RM(addrAbsX)); break; case 0xBF: LDA(RM(addrLongX)); break;
    case 0xC1: opCMP(a_,RM(addrDPIndirectX),!acc8()); break; case 0xC3: opCMP(a_,RM(addrStackRelative),!acc8()); break; case 0xC5: opCMP(a_,RM(addrDP),!acc8()); break; case 0xC7: opCMP(a_,RM(addrDPIndirectLong),!acc8()); break;
    case 0xC9: opCMP(a_,fetchM(),!acc8()); break; case 0xCD: opCMP(a_,RM(addrAbs),!acc8()); break; case 0xCF: opCMP(a_,RM(addrLong),!acc8()); break; case 0xD1: opCMP(a_,RM(addrDPIndirectY),!acc8()); break;
    case 0xD2: opCMP(a_,RM(addrDPIndirect),!acc8()); break; case 0xD3: opCMP(a_,RM(addrStackRelativeIndirectY),!acc8()); break; case 0xD5: opCMP(a_,RM(addrDPX),!acc8()); break; case 0xD7: opCMP(a_,RM(addrDPIndirectLongY),!acc8()); break;
    case 0xD9: opCMP(a_,RM(addrAbsY),!acc8()); break; case 0xDD: opCMP(a_,RM(addrAbsX),!acc8()); break; case 0xDF: opCMP(a_,RM(addrLongX),!acc8()); break;
    case 0xE1: opSBC(RM(addrDPIndirectX)); break; case 0xE3: opSBC(RM(addrStackRelative)); break; case 0xE5: opSBC(RM(addrDP)); break; case 0xE7: opSBC(RM(addrDPIndirectLong)); break;
    case 0xE9: opSBC(fetchM()); break; case 0xED: opSBC(RM(addrAbs)); break; case 0xEF: opSBC(RM(addrLong)); break; case 0xF1: opSBC(RM(addrDPIndirectY)); break;
    case 0xF2: opSBC(RM(addrDPIndirect)); break; case 0xF3: opSBC(RM(addrStackRelativeIndirectY)); break; case 0xF5: opSBC(RM(addrDPX)); break; case 0xF7: opSBC(RM(addrDPIndirectLongY)); break;
    case 0xF9: opSBC(RM(addrAbsY)); break; case 0xFD: opSBC(RM(addrAbsX)); break; case 0xFF: opSBC(RM(addrLongX)); break;
    case 0x06: MEM_RMW(addrDP,opASL); break; case 0x0E: MEM_RMW(addrAbs,opASL); break; case 0x16: MEM_RMW(addrDPX,opASL); break; case 0x1E: MEM_RMW(addrAbsX,opASL); break;
    case 0x0A: if(acc8()) a_=static_cast<uint16_t>((a_&0xFF00)|opASL(static_cast<uint8_t>(a_),false)); else a_=opASL(a_,true); break;
    case 0x26: MEM_RMW(addrDP,opROL); break; case 0x2E: MEM_RMW(addrAbs,opROL); break; case 0x36: MEM_RMW(addrDPX,opROL); break; case 0x3E: MEM_RMW(addrAbsX,opROL); break;
    case 0x2A: if(acc8()) a_=static_cast<uint16_t>((a_&0xFF00)|opROL(static_cast<uint8_t>(a_),false)); else a_=opROL(a_,true); break;
    case 0x46: MEM_RMW(addrDP,opLSR); break; case 0x4E: MEM_RMW(addrAbs,opLSR); break; case 0x56: MEM_RMW(addrDPX,opLSR); break; case 0x5E: MEM_RMW(addrAbsX,opLSR); break;
    case 0x4A: if(acc8()) a_=static_cast<uint16_t>((a_&0xFF00)|opLSR(static_cast<uint8_t>(a_),false)); else a_=opLSR(a_,true); break;
    case 0x66: MEM_RMW(addrDP,opROR); break; case 0x6E: MEM_RMW(addrAbs,opROR); break; case 0x76: MEM_RMW(addrDPX,opROR); break; case 0x7E: MEM_RMW(addrAbsX,opROR); break;
    case 0x6A: if(acc8()) a_=static_cast<uint16_t>((a_&0xFF00)|opROR(static_cast<uint8_t>(a_),false)); else a_=opROR(a_,true); break;
    case 0xC6: MEM_RMW(addrDP,opDEC); break; case 0xCE: MEM_RMW(addrAbs,opDEC); break; case 0xD6: MEM_RMW(addrDPX,opDEC); break; case 0xDE: MEM_RMW(addrAbsX,opDEC); break;
    case 0xE6: MEM_RMW(addrDP,opINC); break; case 0xEE: MEM_RMW(addrAbs,opINC); break; case 0xF6: MEM_RMW(addrDPX,opINC); break; case 0xFE: MEM_RMW(addrAbsX,opINC); break;
    case 0x1A: if(acc8()) a_=static_cast<uint16_t>((a_&0xFF00)|opINC(static_cast<uint8_t>(a_),false)); else a_=opINC(a_,true); break;
    case 0x3A: if(acc8()) a_=static_cast<uint16_t>((a_&0xFF00)|opDEC(static_cast<uint8_t>(a_),false)); else a_=opDEC(a_,true); break;
    case 0x24: opBIT(RM(addrDP),false); break; case 0x2C: opBIT(RM(addrAbs),false); break; case 0x34: opBIT(RM(addrDPX),false); break; case 0x3C: opBIT(RM(addrAbsX),false); break; case 0x89: opBIT(fetchM(),true); break;
    case 0x04: { const uint32_t e=addrDP(); uint16_t v=readM(e); setFlag(Flag_Z,(v&a_&(acc8()?0xFF:0xFFFF))==0); writeM(e,static_cast<uint16_t>(v|(a_&(acc8()?0xFF:0xFFFF)))); } break;
    case 0x0C: { const uint32_t e=addrAbs(); uint16_t v=readM(e); setFlag(Flag_Z,(v&a_&(acc8()?0xFF:0xFFFF))==0); writeM(e,static_cast<uint16_t>(v|(a_&(acc8()?0xFF:0xFFFF)))); } break;
    case 0x14: { const uint32_t e=addrDP(); uint16_t v=readM(e); setFlag(Flag_Z,(v&a_&(acc8()?0xFF:0xFFFF))==0); writeM(e,static_cast<uint16_t>(v&~a_)); } break;
    case 0x1C: { const uint32_t e=addrAbs(); uint16_t v=readM(e); setFlag(Flag_Z,(v&a_&(acc8()?0xFF:0xFFFF))==0); writeM(e,static_cast<uint16_t>(v&~a_)); } break;
    case 0x10: branch(!flag(Flag_N)); break; case 0x30: branch(flag(Flag_N)); break; case 0x50: branch(!flag(Flag_V)); break; case 0x70: branch(flag(Flag_V)); break;
    case 0x80: branch(true); break; case 0x90: branch(!flag(Flag_C)); break; case 0xB0: branch(flag(Flag_C)); break; case 0xD0: branch(!flag(Flag_Z)); break; case 0xF0: branch(flag(Flag_Z)); break;
    case 0x82: { const int16_t d=static_cast<int16_t>(fetch16()); pc_=static_cast<uint16_t>(pc_+d); } break;
    case 0x4C: pc_=fetch16(); break;
    case 0x5C: { const uint32_t a=fetch24(); pc_=static_cast<uint16_t>(a); pb_=static_cast<uint8_t>(a>>16); } break;
    case 0x6C: { const uint16_t p=fetch16(); pc_=read16BankWrap(p); } break;
    case 0x7C: { const uint16_t p=static_cast<uint16_t>(fetch16()+x_); pc_=read16BankWrap((static_cast<uint32_t>(pb_)<<16)|p); } break;
    case 0xDC: { const uint16_t p=fetch16(); const uint32_t a=static_cast<uint32_t>(read8(p)) | (static_cast<uint32_t>(read8(static_cast<uint16_t>(p+1)))<<8) | (static_cast<uint32_t>(read8(static_cast<uint16_t>(p+2)))<<16); pc_=static_cast<uint16_t>(a); pb_=static_cast<uint8_t>(a>>16); } break;
    case 0x20: { const uint16_t t=fetch16(); push16(static_cast<uint16_t>(pc_-1)); pc_=t; } break;
    case 0x22: {
        const uint32_t t=fetch24();
        const uint16_t ret=static_cast<uint16_t>(pc_-1);
        write8(s_, pb_); --s_;
        write8(s_, static_cast<uint8_t>(ret >> 8)); --s_;
        write8(s_, static_cast<uint8_t>(ret)); --s_;
        if (emulation_) s_=static_cast<uint16_t>(0x0100 | (s_ & 0x00FF));
        pb_=static_cast<uint8_t>(t>>16); pc_=static_cast<uint16_t>(t);
    } break;
    case 0xFC: {
        const uint16_t base=fetch16();
        const uint16_t t=read16BankWrap((static_cast<uint32_t>(pb_)<<16)|static_cast<uint16_t>(base+x_));
        const uint16_t ret=static_cast<uint16_t>(pc_-1);
        write8(s_, static_cast<uint8_t>(ret >> 8)); --s_;
        write8(s_, static_cast<uint8_t>(ret)); --s_;
        if (emulation_) s_=static_cast<uint16_t>(0x0100 | (s_ & 0x00FF));
        pc_=t;
    } break;
    case 0x60: pc_=static_cast<uint16_t>(pull16()+1); break;
    case 0x6B: {
        ++s_; const uint8_t lo=read8(s_);
        ++s_; const uint8_t hi=read8(s_);
        ++s_; const uint8_t bank=read8(s_);
        if(emulation_) s_=static_cast<uint16_t>(0x0100 | (s_ & 0x00FF));
        pc_=static_cast<uint16_t>(static_cast<uint16_t>(lo | (static_cast<uint16_t>(hi)<<8))+1);
        pb_=bank;
    } break;
    case 0x40: { p_=pull8(); pc_=pull16(); if(!emulation_) pb_=pull8(); normalizeMode(); } break;
    case 0xA0: LDY(fetchX()); break; case 0xA4: LDY(RX(addrDP)); break; case 0xAC: LDY(RX(addrAbs)); break; case 0xB4: LDY(RX(addrDPX)); break; case 0xBC: LDY(RX(addrAbsX)); break;
    case 0xA2: LDX(fetchX()); break; case 0xA6: LDX(RX(addrDP)); break; case 0xAE: LDX(RX(addrAbs)); break; case 0xB6: LDX(RX(addrDPY)); break; case 0xBE: LDX(RX(addrAbsY)); break;
    case 0x84: STORE_X(addrDP,y_); break; case 0x8C: STORE_X(addrAbs,y_); break; case 0x94: STORE_X(addrDPX,y_); break;
    case 0x86: STORE_X(addrDP,x_); break; case 0x8E: STORE_X(addrAbs,x_); break; case 0x96: STORE_X(addrDPY,x_); break;
    case 0x64: STORE_M(addrDP,0); break; case 0x74: STORE_M(addrDPX,0); break; case 0x9C: STORE_M(addrAbs,0); break; case 0x9E: STORE_M(addrAbsX,0); break;
    case 0xC0: opCMP(y_,fetchX(),!index8()); break; case 0xC4: opCMP(y_,RX(addrDP),!index8()); break; case 0xCC: opCMP(y_,RX(addrAbs),!index8()); break;
    case 0xE0: opCMP(x_,fetchX(),!index8()); break; case 0xE4: opCMP(x_,RX(addrDP),!index8()); break; case 0xEC: opCMP(x_,RX(addrAbs),!index8()); break;
    case 0x88: y_=static_cast<uint16_t>((y_-1)&(index8()?0xFF:0xFFFF)); setNZ(y_,!index8()); break;
    case 0xC8: y_=static_cast<uint16_t>((y_+1)&(index8()?0xFF:0xFFFF)); setNZ(y_,!index8()); break;
    case 0xCA: x_=static_cast<uint16_t>((x_-1)&(index8()?0xFF:0xFFFF)); setNZ(x_,!index8()); break;
    case 0xE8: x_=static_cast<uint16_t>((x_+1)&(index8()?0xFF:0xFFFF)); setNZ(x_,!index8()); break;
    case 0xAA: x_=index8()?static_cast<uint8_t>(a_):a_; setNZ(x_,!index8()); break;
    case 0xA8: y_=index8()?static_cast<uint8_t>(a_):a_; setNZ(y_,!index8()); break;
    case 0x8A: if(acc8()) a_=static_cast<uint16_t>((a_&0xFF00)|(x_&0xFF)); else a_=x_; setNZ(a_,!acc8()); break;
    case 0x98: if(acc8()) a_=static_cast<uint16_t>((a_&0xFF00)|(y_&0xFF)); else a_=y_; setNZ(a_,!acc8()); break;
    case 0x9A: s_=x_; if(emulation_) s_=static_cast<uint16_t>(0x0100|(s_&0xFF)); break;
    case 0xBA: x_=index8()?static_cast<uint8_t>(s_):s_; setNZ(x_,!index8()); break;
    case 0x9B: y_=x_; if(index8()) y_&=0xFF; setNZ(y_,!index8()); break;
    case 0xBB: x_=y_; if(index8()) x_&=0xFF; setNZ(x_,!index8()); break;
    case 0x5B: dp_=a_; setNZ16(dp_); break;
    case 0x7B: a_=dp_; setNZ16(a_); break;
    case 0x1B: s_=a_; if(emulation_) s_=static_cast<uint16_t>(0x0100|(s_&0xFF)); break;
    case 0x3B: a_=s_; setNZ16(a_); break;
    case 0x08: push8(emulation_ ? static_cast<uint8_t>(p_|0x30) : p_); break;
    case 0x28: p_=pull8(); normalizeMode(); break;
    case 0x48: if(acc8()) push8(static_cast<uint8_t>(a_)); else push16(a_); break;
    case 0x68: if(acc8()) { a_=static_cast<uint16_t>((a_&0xFF00)|pull8()); setNZ8(static_cast<uint8_t>(a_)); } else { a_=pull16(); setNZ16(a_); } break;
    case 0xDA: if(index8()) push8(static_cast<uint8_t>(x_)); else push16(x_); break;
    case 0xFA: x_=index8()?pull8():pull16(); setNZ(x_,!index8()); break;
    case 0x5A: if(index8()) push8(static_cast<uint8_t>(y_)); else push16(y_); break;
    case 0x7A: y_=index8()?pull8():pull16(); setNZ(y_,!index8()); break;
    case 0x8B: push8(db_); break; case 0xAB: db_=pull8Linear(); setNZ8(db_); break;
    case 0x0B: push16Linear(dp_); break; case 0x2B: dp_=pull16Linear(); setNZ16(dp_); break;
    case 0x4B: push8(pb_); break;
    case 0xF4: push16Linear(fetch16()); break;
    case 0xD4: {
        const uint16_t p = directAddress(fetch8());
        const uint8_t lo = read8(p);
        const uint8_t hi = read8(directPointerAddress(p, 1, true));
        push16Linear(static_cast<uint16_t>(lo | (static_cast<uint16_t>(hi) << 8)));
    } break;
    case 0x62: { const int16_t d=static_cast<int16_t>(fetch16()); push16Linear(static_cast<uint16_t>(pc_+d)); } break;
    case 0x18: setFlag(Flag_C,false); break; case 0x38: setFlag(Flag_C,true); break;
    case 0x58: setFlag(Flag_I,false); break; case 0x78: setFlag(Flag_I,true); break;
    case 0xB8: setFlag(Flag_V,false); break; case 0xD8: setFlag(Flag_D,false); break; case 0xF8: setFlag(Flag_D,true); break;
    case 0xC2: p_ &= static_cast<uint8_t>(~fetch8()); normalizeMode(); break;
    case 0xE2: p_ |= fetch8(); normalizeMode(); break;
    case 0xFB: { const bool old_c=flag(Flag_C); setFlag(Flag_C,emulation_); emulation_=old_c; normalizeMode(); } break;
    case 0xEB: { const uint8_t lo=static_cast<uint8_t>(a_), hi=static_cast<uint8_t>(a_>>8); a_=static_cast<uint16_t>((static_cast<uint16_t>(lo)<<8)|hi); setNZ8(static_cast<uint8_t>(a_)); } break;
    case 0x00: fetch8(); interrupt(0xFFE6,0xFFFE,true,true); break;
    case 0x02: fetch8(); interrupt(0xFFE4,0xFFF4,true,true); break;
    case 0xCB: waiting_=true; break;
    case 0xDB: stopped_=true; break;
    case 0xEA: break;
    case 0x42: fetch8(); break;
    case 0x44: case 0x54: {
        const uint8_t dest=fetch8(), src=fetch8();
        const uint8_t v=read8((static_cast<uint32_t>(src)<<16)|x_);
        write8((static_cast<uint32_t>(dest)<<16)|y_,v); db_=dest;
        if(op==0x54) {
            if(index8()) { x_=static_cast<uint8_t>(x_+1); y_=static_cast<uint8_t>(y_+1); }
            else { ++x_; ++y_; }
        } else {
            if(index8()) { x_=static_cast<uint8_t>(x_-1); y_=static_cast<uint8_t>(y_-1); }
            else { --x_; --y_; }
        }
        --a_;
        if(a_!=0xFFFF) pc_=static_cast<uint16_t>(pc_-3);
    } break;
    default:
        break;
    }
}
#undef RM
#undef RX
#undef STORE_M
#undef STORE_X
#undef LDA
#undef LDX
#undef LDY
#undef MEM_RMW

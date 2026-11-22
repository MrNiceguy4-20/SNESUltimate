#ifndef CPU_HPP
#define CPU_HPP
#include <array>
#include <cstdint>
#include <vector>
class Bus;
enum StatusFlags : uint8_t {
    Flag_C = (1 << 0),
    Flag_Z = (1 << 1),
    Flag_I = (1 << 2),
    Flag_D = (1 << 3),
    Flag_X = (1 << 4),
    Flag_M = (1 << 5),
    Flag_V = (1 << 6),
    Flag_N = (1 << 7)
};
class CPU {
public:
    struct State {
        uint16_t pc = 0;
        uint16_t s = 0x01FF;
        uint8_t p = Flag_I | Flag_X | Flag_M;
        uint16_t a = 0;
        uint16_t x = 0;
        uint16_t y = 0;
        uint8_t db = 0;
        uint16_t d = 0;
        uint8_t pb = 0;
        bool e = true;
    };
    struct RuntimeState {
        State architectural{};
        bool nmi_transition = false;
        bool nmi_pending = false;
        bool irq_line = false;
        bool irq_pending = false;
        bool irq_poll_i = true;
        bool cycle_nmi_sample = false;
        bool cycle_irq_sample = false;
        bool cycle_i_sample = true;
        bool waiting = false;
        bool stopped = false;
        uint64_t cycles = 0;
        uint32_t last_master_clocks = 0;
    };
    struct PendingCycleInfo {
        enum class Kind { Internal, Read, Write };
        Kind kind = Kind::Internal;
        uint8_t master_clocks = 0;
        uint32_t address = 0;
        uint8_t value = 0;
    };
    struct ExternalBus {
        void* context = nullptr;
        uint8_t (*read)(void*, uint32_t) = nullptr;
        void (*write)(void*, uint32_t, uint8_t) = nullptr;
        uint8_t (*access_master_clocks)(void*, uint32_t) = nullptr;
        uint8_t (*read_timed)(void*, uint32_t, uint8_t*) = nullptr;
        void (*write_timed)(void*, uint32_t, uint8_t, uint8_t*) = nullptr;
    };
    CPU();
    explicit CPU(Bus& bus_ref);
    void attachBus(Bus* bus) { bus_ = bus; external_bus_ = {}; }
    void attachExternalBus(const ExternalBus& bus) { bus_ = nullptr; external_bus_ = bus; }
    void setInternalCycleMasterClocks(uint8_t clocks) { internal_cycle_master_clocks_ = clocks ? clocks : 1; }
    void setCycleStepping(bool enabled) { cycle_stepping_ = enabled; }
    void setInternalCycleCoalescing(bool enabled) { coalesce_internal_cycles_ = enabled; }
    bool hasPendingCycles() const { return !cycle_events_.empty(); }
    bool checkpointBoundary() const { return cycle_events_.empty() && !micro_active_; }
    uint32_t nextCycleDelay() const { return cycle_events_.empty() ? 0u : cycle_events_.front().master_clocks; }
    void beginMachineCycle();
    void completeCycle();
    void reset();
    void step();
    State state() const;
    void setStateForTesting(const State& state);
    RuntimeState runtimeState() const;
    bool restoreRuntimeState(const RuntimeState& state);
    void requestNMI();
    void setIRQLine(bool asserted) { irq_line_ = asserted; }
    uint16_t pc() const { return pc_; }
    uint16_t a()  const { return a_; }
    uint16_t x()  const { return x_; }
    uint16_t y()  const { return y_; }
    uint16_t s()  const { return s_; }
    uint16_t dp() const { return dp_; }
    uint8_t  pb() const { return pb_; }
    uint8_t  db() const { return db_; }
    uint8_t  p()  const { return p_; }
    bool emulation() const { return emulation_; }
    bool waiting() const { return waiting_; }
    bool stopped() const { return stopped_; }
    bool nmiPending() const { return nmi_pending_ || nmi_transition_; }
    bool irqLine() const { return irq_line_; }
    uint64_t cycles() const { return cycles_; }
    uint32_t lastMasterClocks() const { return last_master_clocks_; }
    bool lastStepExecutedOpcode() const { return last_step_executed_opcode_; }
    uint8_t lastOpcode() const { return last_opcode_; }
    uint16_t programCounter() const { return pc_; }
    uint8_t programBank() const { return pb_; }
    uint64_t legacyFallbackCount() const { return legacy_fallback_count_; }
    bool peekNextCycle(PendingCycleInfo& info) const;
    friend class SNES;
private:
    enum class CycleAction : uint8_t {
        None,
        OpcodeFetch,
        ImmediateLo, ImmediateHi,
        DirectOperand, DirectPenalty, AbsoluteLo, AbsoluteHi,
        LongLo, LongHi, LongBank,
        IndirectOperand, IndirectPenalty, PointerLo, PointerHi, PointerBank, IndirectDummy,
        StackOperand, StartStackPointerRead, StackPointerLo, StackPointerHi,
        StartDataRead, DataLo, DataHi, DummyToRead,
        StartStore, StoreHigh, DummyToStore,
        StartRmwRead, RmwDataLo, RmwDataHi, DummyToRmw, RmwAfterModify, RmwWriteLow,
        BranchDisp, BranchDummy, BranchCommit,
        JsrLo, JsrHi, JsrPushHigh, JsrPushLow, JsrFinish,
        RtsPullLow, RtsPullHigh, RtsCommit,
        JumpLo, JumpHi, JumpBank, JumpPointerStart, JumpPointerLo, JumpPointerHi, JumpPointerBank,
        JsrIndirectLo, JsrIndirectPushLow, JsrIndirectFetchHi, JsrIndirectHi,
        JslLo, JslHi, JslAfterBankPush, JslFetchBank, JslBank, JslPushReturnLow, JslFinish,
        RtlStart, RtlLow, RtlHigh, RtlBank,
        RtiStart, RtiStatus, RtiLow, RtiHigh, RtiBank,
        EffectiveLo, EffectiveHi, PeiOperand, PeiPenalty, PeiPointerLo, PeiPointerHi,
        SoftwareSignature, SoftwarePushPCHigh, SoftwarePushPCLow, SoftwarePushStatus,
        SoftwareVectorLo, SoftwareVectorHi,
        RepSepMask, RepSepCommit,
        BrlLo, BrlHi, BrlCommit, WdmSignature,
        BlockDst, BlockSrc, BlockData, BlockAfterWrite, BlockCommit,
        StackPushStart, StackPushNext, StackPushFinish,
        StackPullStart, StackPullLow, StackPullHigh,
        HardwareAfterDummy, HardwareBeginStack, HardwareAfterBankPush, HardwarePushPCLow,
        HardwarePushStatus, HardwareVectorLo, HardwareVectorHi,
        ImpliedCommit, FinishDirect
    };
    enum class CycleKind : uint8_t { Internal, Read, Write };
    struct CycleEvent {
        uint32_t address = 0;
        uint8_t master_clocks = 6;
        uint8_t value = 0;
        CycleAction action = CycleAction::None;
        CycleKind kind = CycleKind::Internal;
    };
    class CycleQueue {
    public:
        static constexpr std::size_t Capacity = 64;
        static constexpr std::size_t Mask = Capacity - 1;
        bool empty() const { return size_ == 0; }
        CycleEvent& front() { return data_[head_]; }
        const CycleEvent& front() const { return data_[head_]; }
        void push_back(const CycleEvent& event) {
            data_[(head_ + size_) & Mask] = event;
            if (size_ < Capacity) ++size_;
        }
        void pop_front() {
            if (!size_) return;
            head_ = (head_ + 1) & Mask;
            --size_;
        }
        void clear() { head_ = 0; size_ = 0; }
        uint32_t totalMasterClocks() const {
            uint32_t total = 0;
            for (std::size_t i = 0; i < size_; ++i)
                total += data_[(head_ + i) & Mask].master_clocks;
            return total;
        }
    private:
        std::array<CycleEvent, Capacity> data_{};
        std::size_t head_ = 0;
        std::size_t size_ = 0;
    };
    Bus* bus_ = nullptr;
    ExternalBus external_bus_{};
    uint8_t internal_cycle_master_clocks_ = 6;
    bool cycle_stepping_ = false;
    bool coalesce_internal_cycles_ = false;
    mutable bool recording_cycles_ = false;
    mutable uint32_t recorded_bus_cycles_ = 0;
    mutable CycleQueue cycle_events_{};
    uint16_t pc_ = 0;
    uint16_t a_  = 0;
    uint16_t x_  = 0;
    uint16_t y_  = 0;
    uint16_t s_  = 0x01FF;
    uint16_t dp_ = 0;
    uint8_t  pb_ = 0;
    uint8_t  db_ = 0;
    uint8_t  p_  = Flag_I | Flag_X | Flag_M;
    bool emulation_ = true;
    bool nmi_transition_ = false;
    bool nmi_pending_ = false;
    bool irq_line_ = false;
    bool irq_pending_ = false;
    bool irq_poll_i_ = true;
    bool cycle_nmi_sample_ = false;
    bool cycle_irq_sample_ = false;
    bool cycle_i_sample_ = true;
    bool legacy_interrupt_poll_pending_ = false;
    bool machine_cycle_active_ = false;
    bool waiting_ = false;
    bool stopped_ = false;
    uint64_t cycles_ = 0;
    uint32_t last_master_clocks_ = 0;
    bool last_step_executed_opcode_ = false;
    uint8_t last_opcode_ = 0;
    mutable uint32_t access_penalty_master_clocks_ = 0;
    uint32_t timing_extra_cycles_ = 0;
    uint64_t legacy_fallback_count_ = 0;
    uint8_t timing_base_cycles_ = 0;
    bool page_crossed_ = false;
    bool micro_active_ = false;
    uint8_t micro_opcode_ = 0;
    uint32_t micro_bus_cycles_ = 0;
    uint32_t micro_master_clocks_ = 0;
    bool micro_m8_before_ = true;
    bool micro_x8_before_ = true;
    uint16_t micro_dp_before_ = 0;
    enum class DirectAddressMode : uint8_t {
        None, AbsoluteLong, AbsoluteLongX,
        IndirectX, Indirect, IndirectY, IndirectLong, IndirectLongY,
        StackRelative, StackRelativeIndirectY
    };
    DirectAddressMode direct_mode_ = DirectAddressMode::None;
    uint32_t direct_address_ = 0;
    uint16_t direct_value_ = 0;
    uint16_t direct_index_ = 0;
    uint16_t direct_target_ = 0;
    uint8_t direct_temp8_ = 0;
    bool direct_wide_ = false;
    bool direct_indexed_ = false;
    bool direct_store_ = false;
    bool direct_rmw_ = false;
    bool direct_old_i_ = false;
    void queueDeferredReadAction(uint32_t address, CycleAction action);
    void queueDeferredWriteAction(uint32_t address, uint8_t value, CycleAction action);
    void queueInternalAction(uint32_t count, CycleAction action);
    void queueOpcodeFetch();
    void onOpcodeFetched(uint8_t opcode);
    bool startDirectInstruction(uint8_t opcode);
    void handleReadAction(CycleAction action, uint8_t value);
    void handlePostCycleAction(CycleAction action);
    void directFinish();
    void directFinalize();
    void directApplyImplied();
    void directRmwModify(uint16_t old_value);
    void directCommitStackPull(uint16_t value);
    void microApplyReadOperation(uint8_t opcode, uint16_t value);
    void startHardwareInterrupt(bool nmi);
    bool acc8() const { return (p_ & Flag_M) != 0; }
    bool index8() const { return (p_ & Flag_X) != 0; }
    bool flag(StatusFlags f) const { return (p_ & f) != 0; }
    bool hasMemoryBus() const { return bus_ != nullptr || external_bus_.read != nullptr; }
    uint8_t memoryRead(uint32_t address) const;
    void memoryWrite(uint32_t address, uint8_t value);
    uint8_t memoryAccessMasterClocks(uint32_t address) const;
    void setFlag(StatusFlags f, bool set);
    void normalizeMode();
    void recordBusCycle(uint32_t address, bool write = false, uint8_t value = 0) const;
    void finishCycleQueue(uint32_t cpuCycles);
    uint8_t read8(uint32_t address) const;
    uint16_t read16(uint32_t address) const;
    uint16_t read16BankWrap(uint32_t address) const;
    uint32_t read24(uint32_t address) const;
    void write8(uint32_t address, uint8_t value);
    void write16(uint32_t address, uint16_t value);
    uint8_t fetch8();
    uint16_t fetch16();
    uint32_t fetch24();
    void push8(uint8_t value);
    void push16(uint16_t value);
    void push16Linear(uint16_t value);
    uint8_t pull8();
    uint8_t pull8Linear();
    uint16_t pull16();
    uint16_t pull16Linear();
    void setNZ8(uint8_t value);
    void setNZ16(uint16_t value);
    void setNZ(uint16_t value, bool wide);
    uint16_t readM(uint32_t address) const;
    void writeM(uint32_t address, uint16_t value);
    uint16_t readX(uint32_t address) const;
    void writeX(uint32_t address, uint16_t value);
    uint16_t fetchM();
    uint16_t fetchX();
    uint32_t addrDP();
    uint32_t addrDPX();
    uint32_t addrDPY();
    uint32_t addrAbs();
    uint32_t addrAbsX();
    uint32_t addrAbsY();
    uint32_t addrLong();
    uint32_t addrLongX();
    uint32_t addrDPIndirect();
    uint32_t addrDPIndirectX();
    uint32_t addrDPIndirectY();
    uint32_t addrDPIndirectLong();
    uint32_t addrDPIndirectLongY();
    uint32_t addrStackRelative();
    uint32_t addrStackRelativeIndirectY();
    uint16_t directAddress(uint8_t operand, uint16_t index = 0) const;
    uint16_t directPointerAddress(uint16_t base, uint8_t delta, bool linear_exception = false) const;
    void branch(bool condition);
    void interrupt(uint16_t native_vector, uint16_t emu_vector, bool software, bool break_flag);
    void latchInterruptRecognition();
    bool serviceInterrupts();
    void opORA(uint16_t value);
    void opAND(uint16_t value);
    void opEOR(uint16_t value);
    void opADC(uint16_t value);
    void opSBC(uint16_t value);
    void opCMP(uint16_t lhs, uint16_t rhs, bool wide);
    void opBIT(uint16_t value, bool immediate);
    uint16_t opASL(uint16_t value, bool wide);
    uint16_t opLSR(uint16_t value, bool wide);
    uint16_t opROL(uint16_t value, bool wide);
    uint16_t opROR(uint16_t value, bool wide);
    uint16_t opINC(uint16_t value, bool wide);
    uint16_t opDEC(uint16_t value, bool wide);
    void executeInstruction(uint8_t opcode);
    static uint8_t baseCycles(uint8_t opcode);
    uint8_t modePenaltyCycles(uint8_t opcode, bool m8_before, bool x8_before, uint16_t dp_before) const;
    static bool hasDirectPagePenalty(uint8_t opcode);
    static bool hasMWidthPenalty(uint8_t opcode);
    static bool hasMWidthDoublePenalty(uint8_t opcode);
    static bool hasXWidthPenalty(uint8_t opcode);
    static bool hasPageCrossPenalty(uint8_t opcode);
};
#endif

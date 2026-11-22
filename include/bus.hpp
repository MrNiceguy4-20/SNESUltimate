#ifndef BUS_HPP
#define BUS_HPP
#include <cstdint>
#include <vector>
#include <array>
class CPU;
class PPU;
class APU;
class DMA;
class Cartridge;
class Controller;
class Bus {
public:
    enum class SCpuRevision : uint8_t {
        SCPU = 0,
        SCPUA = 1,
        SCPUB = 2,
    };
    Bus();
    void attachCPU(CPU* cpu) { cpu_ = cpu; }
    void attachPPU(PPU* ppu) { ppu_ = ppu; }
    void attachAPU(APU* apu) { apu_ = apu; }
    void attachDMA(DMA* dma) { dma_ = dma; }
    void attachCartridge(Cartridge* cartridge) { cartridge_ = cartridge; }
    void attachController1(Controller* controller) { controller1_ = controller; }
    void attachController2(Controller* controller) { controller2_ = controller; }
    uint8_t read(uint32_t address);
    uint8_t readDMA(int channel, uint32_t address);
    void writeDMA(uint32_t address, uint8_t value);
    void beginCartridgeDMA(int channel, uint32_t aAddress, uint32_t length, uint8_t control);
    void endCartridgeDMA(int channel);
    void write(uint32_t address, uint8_t value);
    uint16_t read16(uint32_t address);
    void write16(uint32_t address, uint16_t value);
    void power();
    void reset();
    void updateVideoTiming(int scanline, int dot, bool hblank, bool vblank);
    void processNmiCircuitTick(uint16_t hClock, uint16_t scanline);
    void triggerVBlankNmiEdge();
    void triggerTimerIrq();
    void processTimerIrqCircuitTick(uint16_t hClock, uint16_t scanline);
    void setCartridgeIrq(bool asserted);
    void setMasterClock(uint64_t masterClock);
    void completeCpuMachineCycle();
    uint64_t masterClock() const { return master_clock_; }
    void setSCpuRevision(SCpuRevision revision) { scpu_revision_ = revision; }
    SCpuRevision scpuRevision() const { return scpu_revision_; }
    uint8_t scpuVersion() const { return scpu_revision_ == SCpuRevision::SCPU ? 1 : 2; }
    static constexpr uint64_t MATH_MUL_CPU_CYCLES = 8;
    static constexpr uint64_t MATH_DIV_CPU_CYCLES = 16;
    bool fastRomEnabled() const { return (memsel_ & 0x01) != 0; }
    uint8_t memsel() const { return memsel_; }
    uint8_t openBus();
    void configureOpenBusDecay(bool enabled, uint64_t thresholdMasterClocks = 0, uint8_t targetValue = 0x00);
    uint8_t ioPortOutput() const { return wrio_; }
    const std::vector<uint8_t>& wram() const { return wram_; }
    void enableFlatTestMemory(bool enabled);
    void clearFlatTestMemory(uint8_t value = 0);
    uint8_t flatTestPeek(uint32_t address) const;
    void flatTestPoke(uint32_t address, uint8_t value);
    uint8_t accessMasterClocks(uint32_t address) const;
    uint8_t nmitimen() const { return nmitimen_; }
    uint8_t irqMode() const { return static_cast<uint8_t>((nmitimen_ >> 4) & 0x03); }
    uint16_t htime() const { return htime_; }
    uint16_t vtime() const { return vtime_; }
    uint64_t irqConfigGeneration() const { return irq_config_generation_; }
    void serviceTimerIrqLine();
    friend class SNES;
private:
    bool isSystemBank(uint8_t bank) const;
    uint8_t readSystemArea(uint8_t bank, uint16_t addr, bool& handled);
    void writeSystemArea(uint8_t bank, uint16_t addr, uint8_t value, uint8_t previousBus, bool& handled);
    void updateTimerIrqLevel(uint16_t hClock);
    void updateNmiLine();
    uint8_t readCpuIo(uint16_t addr);
    void writeCpuIo(uint16_t addr, uint8_t value);
    bool isInvalidDMAABusAddress(uint32_t address) const;
    void beginAutoJoypad();
    void updateControllerStrobe();
    void advanceAutoJoypad(uint32_t masterClocks);
    void stepMathAlu();
    void driveOpenBus(uint8_t value, uint8_t drivenMask = 0xFF);
    void applyOpenBusDecay();
    uint8_t readWramPort(uint16_t addr);
    void writeWramPort(uint16_t addr, uint8_t value);
    uint8_t readLoROM(uint32_t address);
    void writeLoROM(uint32_t address, uint8_t value);
    uint8_t readHiROM(uint32_t address);
    uint8_t readExLoROM(uint32_t address);
    void writeHiROM(uint32_t address, uint8_t value);
    void writeExLoROM(uint32_t address, uint8_t value);
    uint8_t readExHiROM(uint32_t address);
    void writeExHiROM(uint32_t address, uint8_t value);
    std::vector<uint8_t> wram_;
    bool flat_test_memory_enabled_ = false;
    std::vector<uint8_t> flat_test_memory_;
    uint32_t wram_address_ = 0;
    uint8_t nmitimen_ = 0;
    uint8_t wrio_ = 0xFF;
    uint8_t mdmaen_ = 0;
    uint8_t hdmaen_ = 0;
    uint8_t memsel_ = 0;
    SCpuRevision scpu_revision_ = SCpuRevision::SCPUB;
    uint8_t wrmpya_ = 0xFF;
    uint16_t wrdiv_ = 0xFFFF;
    uint16_t quotient_ = 0;
    uint16_t product_remainder_ = 0;
    uint8_t math_mpy_counter_ = 0;
    uint8_t math_div_counter_ = 0;
    uint32_t math_shift_ = 0;
    uint64_t master_clock_ = 0;
    uint8_t open_bus_ = 0;
    bool open_bus_decay_enabled_ = false;
    uint64_t open_bus_decay_threshold_ = 0;
    uint8_t open_bus_decay_target_ = 0;
    std::array<uint64_t, 8> open_bus_bit_refresh_clock_{};
    uint8_t cpu_io_read_bus_ = 0;
    bool joy_strobe_ = false;
    bool auto_joy_strobe_ = false;
    std::array<uint16_t, 4> auto_joy_{0,0,0,0};
    bool auto_joy_active_ = false;
    uint32_t auto_joy_elapsed_clocks_ = 0;
    uint16_t auto_joy_next_sample_clock_ = 256;
    bool auto_joy_latch_released_ = false;
    bool auto_joy_pending_ = false;
    uint16_t htime_ = 0;
    uint16_t vtime_ = 0;
    bool rdnmi_flag_ = false;
    bool nmi_line_level_ = false;
    uint16_t nmi_last_hclock_ = 0;
    bool timeup_flag_ = false;
    uint64_t timer_irq_active_until_ = 0;
    uint64_t timer_irq_line_due_ = UINT64_MAX;
    bool timer_irq_line_asserted_ = false;
    bool timer_irq_edge_committed_ = false;
    uint16_t timer_hcounter_ = 0;
    uint16_t timer_vcounter_ = 0;
    bool timer_irq_level_ = false;
    uint8_t timer_irq_need_ = 0;
    bool timer_irq_enabled_ = false;
    uint16_t timer_last_hclock_ = 0;
    bool hblank_ = false;
    bool vblank_ = false;
    bool previous_vblank_ = false;
    bool irq_match_latched_ = false;
    bool cartridge_irq_ = false;
    uint64_t irq_config_generation_ = 0;
    uint16_t last_irq_config_write_ = 0;
    CPU* cpu_ = nullptr;
    PPU* ppu_ = nullptr;
    APU* apu_ = nullptr;
    DMA* dma_ = nullptr;
    Cartridge* cartridge_ = nullptr;
    Controller* controller1_ = nullptr;
    Controller* controller2_ = nullptr;
};
#endif

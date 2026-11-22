#pragma once
#include "cpu.hpp"
#include "ppu.hpp"
#include "apu.hpp"
#include "dma.hpp"
#include "bus.hpp"
#include "cartridge.hpp"
#include "scheduler.hpp"
#include "controller.hpp"
#include <string>
#include <cstdint>
#include <vector>
class SNES {
public:
    SNES();
    ~SNES();
    bool loadROM(const std::string& path);
    void powerCycle();
    void reset();
    std::vector<uint8_t> saveState();
    bool loadState(const std::vector<uint8_t>& state);
    bool saveStateToFile(const std::string& path);
    bool loadStateFromFile(const std::string& path);
    void runFrame();
    static constexpr double NTSC_MASTER_CLOCK_HZ = 945000000.0 / 44.0;
    static constexpr double SPC700_CLOCK_HZ = 1024000.0;
    static constexpr double NTSC_FRAME_RATE_HZ = NTSC_MASTER_CLOCK_HZ / 357366.0;
    static constexpr double PAL_MASTER_CLOCK_HZ = 21281370.0;
    static constexpr double PAL_FRAME_RATE_HZ = PAL_MASTER_CLOCK_HZ / (1364.0 * 312.0);
    static constexpr int DRAM_REFRESH_MASTER_CLOCKS = 40;
    double nominalFrameRate() const { return ppu_.palRegion() ? PAL_FRAME_RATE_HZ : NTSC_FRAME_RATE_HZ; }
    double masterClockHz() const { return ppu_.palRegion() ? PAL_MASTER_CLOCK_HZ : NTSC_MASTER_CLOCK_HZ; }
    void runCycles(uint64_t cycles);
    void setButton(int player, int button, bool pressed);
    void setControllerType(int port, Controller::DeviceType type);
    void setMultitapButton(int port, int subPort, int button, bool pressed);
    void setMultitapConnected(int port, int subPort, bool connected);
    void addMouseMotion(int port, int dx, int dy);
    void setMouseButton(int port, Controller::MouseButton button, bool pressed);
    void setSuperScopeAim(int x, int y, bool onScreen = true);
    void setSuperScopeButton(Controller::ScopeButton button, bool pressed);
    CPU&         cpu()         { return cpu_; }
    PPU&         ppu()         { return ppu_; }
    APU&         apu()         { return apu_; }
    DMA&         dma()         { return dma_; }
    Bus&         bus()         { return bus_; }
    Cartridge&   cartridge()   { return cart_; }
    Scheduler&   scheduler()   { return scheduler_; }
    Controller&  controller1() { return controller1_; }
    Controller&  controller2() { return controller2_; }
    bool isLoaded() const { return cart_.isLoaded(); }
    uint64_t frameCount() const { return frame_count_; }
    uint64_t dramRefreshCount() const { return dram_refresh_count_; }
private:
    void connectSystems();
    void scheduleTimerIrqForCurrentLine(uint64_t now);
    void refreshTimerIrqSchedule(uint64_t now);
    CPU         cpu_;
    PPU         ppu_;
    APU         apu_;
    DMA         dma_;
    Bus         bus_;
    Cartridge   cart_;
    Scheduler   scheduler_;
    Controller  controller1_;
    Controller  controller2_;
    uint64_t frame_count_ = 0;
    uint64_t next_cpu_time_ = 0;
    uint64_t next_ppu_dot_time_ = 4;
    uint64_t next_apu_time_ = 21;
    uint64_t next_apu_time_fp_ = 0;
    uint64_t master_clocks_per_apu_cycle_fp_ = 0;
    uint64_t next_dma_time_ = UINT64_MAX;
    uint64_t next_hdma_time_ = UINT64_MAX;
    uint64_t paused_dma_remaining_ = 0;
    uint64_t paused_cpu_remaining_ = 0;
    bool pending_hdma_line_ = false;
    uint8_t pending_hdma_mask_ = 0;
    uint64_t pending_hdma_trigger_clock_ = 0;
    uint64_t mdma_cpu_remaining_ = 0;
    bool mdma_cpu_paused_ = false;
    bool hdma_nested_in_mdma_ = false;
    uint64_t dram_refresh_count_ = 0;
    uint64_t next_dram_refresh_time_ = 538;
    uint64_t current_line_start_time_ = 0;
    uint64_t next_timer_irq_time_ = UINT64_MAX;
    uint64_t seen_irq_config_generation_ = 0;
};

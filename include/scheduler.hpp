#pragma once
#include <cstdint>
#include <functional>
#include <queue>
#include <vector>
class CPU;
class PPU;
class APU;
class DMA;
struct Event {
    uint64_t time;
    int      id;
    std::function<void()> callback;
    bool operator>(const Event& other) const {
        return time > other.time;
    }
};
class Scheduler {
public:
    Scheduler();
    ~Scheduler();
    void attachCPU(CPU* cpu) { cpu_ = cpu; }
    void attachPPU(PPU* ppu) { ppu_ = ppu; }
    void attachAPU(APU* apu) { apu_ = apu; }
    void attachDMA(DMA* dma) { dma_ = dma; }
    void reset();
    uint64_t now() const { return current_time_; }
    bool hasPendingEvents() const { return !events_.empty(); }
    void advanceToNoEvents(uint64_t target_time) { current_time_ = target_time; }
    void schedule(uint64_t absolute_time, int id, std::function<void()> callback);
    void scheduleRelative(uint64_t cycles_from_now, int id, std::function<void()> callback);
    void run(uint64_t target_time);
    void advanceTo(uint64_t target_time) {
        if (events_.empty()) current_time_ = target_time;
        else run(target_time);
    }
    void runFor(uint64_t cycles);
    void advance(uint64_t master_cycles) { runFor(master_cycles); }
    enum EventId {
        EVENT_NONE = 0,
        EVENT_HBLANK,
        EVENT_VBLANK,
        EVENT_HDMA,
        EVENT_IRQ,
        EVENT_NMI,
        EVENT_APU_TIMER,
        EVENT_DMA_FINISH,
        EVENT_FRAME_END
    };
    friend class SNES;
private:
    uint64_t current_time_ = 0;
    std::priority_queue<Event, std::vector<Event>, std::greater<Event>> events_;
    CPU* cpu_ = nullptr;
    PPU* ppu_ = nullptr;
    APU* apu_ = nullptr;
    DMA* dma_ = nullptr;
};

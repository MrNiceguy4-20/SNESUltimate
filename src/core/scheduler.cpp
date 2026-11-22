#include "scheduler.hpp"
#include "cpu.hpp"
#include "ppu.hpp"
#include "apu.hpp"
#include "dma.hpp"
#include <iostream>
Scheduler::Scheduler() = default;
Scheduler::~Scheduler() = default;
void Scheduler::reset() {
    current_time_ = 0;
    while (!events_.empty()) {
        events_.pop();
    }
}
void Scheduler::schedule(uint64_t absolute_time, int id, std::function<void()> callback) {
    if (absolute_time < current_time_) {
        absolute_time = current_time_;
    }
    events_.push({absolute_time, id, std::move(callback)});
}
void Scheduler::scheduleRelative(uint64_t cycles_from_now, int id, std::function<void()> callback) {
    schedule(current_time_ + cycles_from_now, id, std::move(callback));
}
void Scheduler::run(uint64_t target_time) {
    while (!events_.empty() && events_.top().time <= target_time) {
        Event ev = events_.top();
        events_.pop();
        current_time_ = ev.time;
        if (ev.callback) {
            ev.callback();
        }
    }
    current_time_ = target_time;
}
void Scheduler::runFor(uint64_t cycles) {
    run(current_time_ + cycles);
}

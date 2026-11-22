#include "apu.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>
namespace {
constexpr uint64_t FP = uint64_t{1} << 16;
void prepare(APU& apu, uint16_t pc, uint8_t a, uint8_t y = 0) {
    APU::State state{};
    state.pc = pc;
    state.a = a;
    state.y = y;
    state.sp = 0xEF;
    apu.setStateForTesting(state);
    apu.write(0x00F1, 0x00);
}
}
int main() {
    {
        APU apu;
        prepare(apu, 0x2000, 0x5A);
        apu.write(0x00F4, 0x11);
        apu.write(0x2000, 0xC4);
        apu.write(0x2001, 0xF4);
        apu.beginScheduledInstruction(100 * FP, 20 * FP);
        assert(apu.tick() == 4);
        assert(apu.readPort(0) == 0x11);
        assert(apu.nextPortEventMasterClock() == 160);
        apu.synchronizePorts(159);
        assert(apu.readPort(0) == 0x11);
        apu.synchronizePorts(160);
        assert(apu.readPort(0) == 0x5A);
    }
    {
        APU apu;
        prepare(apu, 0x2100, 0x34, 0x12);
        apu.write(0x00F4, 0xA0);
        apu.write(0x00F5, 0xB0);
        apu.write(0x2100, 0xDA);
        apu.write(0x2101, 0xF4);
        std::vector<uint64_t> commitCycles;
        apu.setPortWriteObserver([&](uint8_t, uint8_t, bool, uint64_t cycle) { commitCycles.push_back(cycle); });
        apu.beginScheduledInstruction(200 * FP, 10 * FP);
        assert(apu.tick() == 5);
        assert(apu.nextPortEventMasterClock() == 230);
        apu.synchronizePorts(229);
        assert(apu.readPort(0) == 0xA0 && apu.readPort(1) == 0xB0);
        apu.synchronizePorts(230);
        assert(apu.readPort(0) == 0x34 && apu.readPort(1) == 0xB0);
        apu.synchronizePorts(240);
        assert(apu.readPort(1) == 0x12);
        assert(commitCycles.size() == 2);
        assert(commitCycles[0] == 3 && commitCycles[1] == 4);
    }
    {
        APU apu;
        prepare(apu, 0x2200, 0x66);
        apu.write(0x00F4, 0x22);
        apu.write(0x00F0, 0x1A);
        apu.write(0x2200, 0xC4);
        apu.write(0x2201, 0xF4);
        apu.beginScheduledInstruction(300 * FP, 10 * FP);
        assert(apu.tick() == 6);
        assert(apu.nextPortEventMasterClock() == 350);
        apu.synchronizePorts(349);
        assert(apu.readPort(0) == 0x22);
        apu.synchronizePorts(350);
        assert(apu.readPort(0) == 0x66);
    }
    std::cout << "apu_port_write_timing_accuracy: PASS\n";
    return 0;
}

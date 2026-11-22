#include "apu.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
namespace {
constexpr uint64_t FP = uint64_t{1} << 16;
void prepare(APU& apu, uint16_t pc, uint8_t a = 0, uint8_t y = 0) {
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
        prepare(apu, 0x2000, 0x00, 0x44);
        apu.writePort(0, 0x11);
        apu.write(0x2000, 0xEB);
        apu.write(0x2001, 0xF4);
        apu.beginScheduledInstruction(100 * FP, 20 * FP);
        assert(apu.tick() == 3);
        assert(apu.y() == 0x44);
        assert(apu.nextPortEventMasterClock() == 140);
        apu.synchronizePorts(139);
        assert(apu.y() == 0x44);
        apu.writePort(0, 0x77);
        apu.synchronizePorts(140);
        assert(apu.y() == 0x77);
    }
    {
        APU apu;
        prepare(apu, 0x2100, 0x55);
        apu.writePort(1, 0x22);
        apu.write(0x2100, 0xE4);
        apu.write(0x2101, 0xF5);
        apu.beginScheduledInstruction(200 * FP, 10 * FP);
        assert(apu.tick() == 3);
        apu.synchronizePorts(220);
        assert(apu.a() == 0x22);
        apu.writePort(1, 0x99);
        assert(apu.a() == 0x22);
    }
    {
        APU apu;
        prepare(apu, 0x2200, 0x00, 0x33);
        apu.writePort(0, 0x10);
        apu.write(0x2200, 0x7E);
        apu.write(0x2201, 0xF4);
        apu.beginScheduledInstruction(300 * FP, 10 * FP);
        assert(apu.tick() == 3);
        apu.writePort(0, 0x33);
        apu.synchronizePorts(320);
        assert((apu.psw() & 0x02) != 0);
        assert((apu.psw() & 0x01) != 0);
    }
    {
        APU apu;
        prepare(apu, 0x2300, 0xAA, 0xBB);
        apu.writePort(0, 0x12);
        apu.writePort(1, 0x34);
        apu.write(0x2300, 0xBA);
        apu.write(0x2301, 0xF4);
        apu.beginScheduledInstruction(400 * FP, 10 * FP);
        assert(apu.tick() == 5);
        assert(apu.nextPortEventMasterClock() == 420);
        apu.synchronizePorts(420);
        apu.writePort(0, 0xFE);
        apu.writePort(1, 0x56);
        assert(apu.nextPortEventMasterClock() == 440);
        apu.synchronizePorts(440);
        assert(apu.a() == 0x12);
        assert(apu.y() == 0x56);
    }
    {
        APU apu;
        prepare(apu, 0x2400, 0x00, 0x10);
        apu.writePort(0, 0x21);
        apu.write(0x00F0, 0x1A);
        apu.write(0x2400, 0xEB);
        apu.write(0x2401, 0xF4);
        apu.beginScheduledInstruction(500 * FP, 10 * FP);
        assert(apu.tick() == 5);
        assert(apu.nextPortEventMasterClock() == 530);
        apu.writePort(0, 0x88);
        apu.synchronizePorts(529);
        assert(apu.y() == 0x10);
        apu.synchronizePorts(530);
        assert(apu.y() == 0x88);
    }
    std::cout << "apu_port_read_timing_accuracy: PASS\n";
    return 0;
}

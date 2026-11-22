#include "bus.hpp"
#include "cartridge.hpp"
#include "cpu.hpp"
#include "ppu.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>
namespace {
std::vector<uint8_t> makeRom() {
    std::vector<uint8_t> rom(0x10000, 0xEA);
    constexpr std::size_t h = 0x7FC0;
    const char title[] = "CPU NMI RECOGNITION ";
    for (std::size_t i = 0; i < 21; ++i) rom[h + i] = static_cast<uint8_t>(title[i]);
    rom[h + 0x15] = 0x20;
    rom[h + 0x17] = 0x06;
    rom[h + 0x19] = 0x01;
    rom[0x7FFA] = 0x00; rom[0x7FFB] = 0x90;
    rom[0x7FFC] = 0x00; rom[0x7FFD] = 0x80;
    return rom;
}
struct Rig {
    Cartridge cart;
    Bus bus;
    CPU cpu;
    PPU ppu;
    Rig() {
        assert(cart.loadData(makeRom(), "cpu_nmi_recognition.sfc", "nmi-recognition"));
        bus.attachCPU(&cpu);
        bus.attachPPU(&ppu);
        bus.attachCartridge(&cart);
        cpu.attachBus(&bus);
        ppu.attachBus(&bus);
        bus.reset();
        ppu.reset();
        cpu.reset();
        cpu.setCycleStepping(true);
    }
};
void completeStartedCycle(CPU& cpu) {
    assert(cpu.hasPendingCycles());
    cpu.completeCycle();
}
void startNext(CPU& cpu) {
    assert(cpu.hasPendingCycles());
    cpu.beginMachineCycle();
}
}
int main() {
    {
        Rig r;
        r.bus.write(0x4200, 0x00);
        r.bus.write(0x7E0000, 0x70);
        assert(r.bus.read(0x7E0000) == 0x70);
        const uint8_t rdnmi_open_bus = r.bus.read(0x4210);
        assert((rdnmi_open_bus & 0x70) == 0x70);
        assert((rdnmi_open_bus & 0x0F) == 0x02);
        r.bus.updateVideoTiming(r.ppu.vblankStartLine(), 0, false, true);
        r.bus.setMasterClock(1);
        r.bus.processNmiCircuitTick(1, static_cast<uint16_t>(r.ppu.vblankStartLine()));
        assert((r.bus.read(0x4210) & 0x80) == 0);
        r.bus.setMasterClock(2);
        r.bus.processNmiCircuitTick(2, static_cast<uint16_t>(r.ppu.vblankStartLine()));
        assert((r.bus.read(0x4210) & 0x80) != 0);
        r.cpu.setStateForTesting(r.cpu.state());
        r.bus.processNmiCircuitTick(2, static_cast<uint16_t>(r.ppu.vblankStartLine()));
        r.bus.write(0x4200, 0x80);
        assert(r.cpu.nmiPending());
        (void)r.bus.read(0x4210);
        assert(r.cpu.nmiPending());
    }
    {
        Rig r;
        r.cpu.step();
        startNext(r.cpu);
        completeStartedCycle(r.cpu);
        startNext(r.cpu);
        r.cpu.requestNMI();
        completeStartedCycle(r.cpu);
        assert(r.cpu.pc() == 0x8001);
        r.cpu.step();
        startNext(r.cpu);
        completeStartedCycle(r.cpu);
        assert(r.cpu.pc() == 0x8002);
        startNext(r.cpu);
        completeStartedCycle(r.cpu);
        r.cpu.step();
        startNext(r.cpu);
        completeStartedCycle(r.cpu);
        assert(r.cpu.pc() == 0x8002);
    }
    {
        Rig r;
        CPU::State st = r.cpu.state();
        st.p = static_cast<uint8_t>(st.p & ~Flag_I);
        r.cpu.setStateForTesting(st);
        r.cpu.step();
        startNext(r.cpu);
        completeStartedCycle(r.cpu);
        r.cpu.requestNMI();
        r.cpu.setIRQLine(true);
        startNext(r.cpu);
        completeStartedCycle(r.cpu);
        r.cpu.step();
        startNext(r.cpu);
        completeStartedCycle(r.cpu);
        assert(r.cpu.pc() == 0x8001);
    }
    std::cout << "CPU NMI recognition regression passed\n";
    return 0;
}

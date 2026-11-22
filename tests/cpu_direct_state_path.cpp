#include "cpu.hpp"
#include "bus.hpp"
#include "cartridge.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
namespace {
std::vector<uint8_t> makeRom() {
    std::vector<uint8_t> rom(0x10000, 0xEA);
    std::size_t p = 0;
    rom[p++] = 0xA9; rom[p++] = 0x12;
    rom[p++] = 0x85; rom[p++] = 0x10;
    rom[p++] = 0xA5; rom[p++] = 0x10;
    rom[p++] = 0x8D; rom[p++] = 0x34; rom[p++] = 0x12;
    rom[p++] = 0xAD; rom[p++] = 0x34; rom[p++] = 0x12;
    rom[p++] = 0xC9; rom[p++] = 0x12;
    rom[p++] = 0xF0; rom[p++] = 0x02;
    rom[p++] = 0xA9; rom[p++] = 0xFF;
    rom[p++] = 0xEA;
    rom[0x20] = 0x20; rom[0x21] = 0x30; rom[0x22] = 0x80;
    rom[0x30] = 0x60;
    constexpr std::size_t header = 0x7FC0;
    const char title[] = "DIRECT STATE PATH    ";
    for (std::size_t i = 0; i < 21; ++i) rom[header + i] = static_cast<uint8_t>(title[i]);
    rom[header + 0x15] = 0x20;
    rom[header + 0x17] = 0x06;
    rom[header + 0x19] = 0x01;
    rom[0x7FFC] = 0x00;
    rom[0x7FFD] = 0x80;
    return rom;
}
void runInstruction(CPU& cpu) {
    cpu.step();
    while (cpu.hasPendingCycles()) cpu.completeCycle();
}
}
int main() {
    Cartridge cart;
    Bus bus;
    CPU cpu;
    assert(cart.loadData(makeRom(), "direct_state_path.sfc", "direct-state-path"));
    bus.attachCPU(&cpu);
    bus.attachCartridge(&cart);
    cpu.attachBus(&bus);
    bus.reset();
    cpu.reset();
    cpu.setCycleStepping(true);
    cpu.setInternalCycleCoalescing(true);
    for (int i = 0; i < 7; ++i) runInstruction(cpu);
    assert(cpu.a() == 0x0012);
    assert((cpu.p() & Flag_Z) != 0);
    assert(bus.read(0x000010) == 0x12);
    assert(bus.read(0x001234) == 0x12);
    assert(cpu.pc() == 0x8012);
    assert(cpu.legacyFallbackCount() == 0);
    runInstruction(cpu);
    assert(cpu.legacyFallbackCount() == 0);
    CPU::State st = cpu.state();
    st.pc = 0x8020;
    st.s = 0x01FF;
    cpu.setStateForTesting(st);
    runInstruction(cpu);
    assert(cpu.pc() == 0x8030);
    assert(cpu.s() == 0x01FD);
    runInstruction(cpu);
    assert(cpu.pc() == 0x8023);
    assert(cpu.s() == 0x01FF);
    assert(cpu.legacyFallbackCount() == 0);
    std::puts("cpu direct-state microcode regression ok");
}

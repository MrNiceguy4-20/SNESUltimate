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
    rom[0x0000] = 0x71;
    rom[0x0001] = 0x34;
    constexpr std::size_t header = 0x7FC0;
    const char title[] = "DP IND Y BANK CARRY  ";
    for (std::size_t i = 0; i < 21; ++i) rom[header + i] = static_cast<uint8_t>(title[i]);
    rom[header + 0x15] = 0x20;
    rom[header + 0x17] = 0x06;
    rom[header + 0x19] = 0x01;
    rom[0x7FFC] = 0x00;
    rom[0x7FFD] = 0x80;
    return rom;
}
}
int main() {
    Cartridge cart;
    Bus bus;
    CPU cpu;
    assert(cart.loadData(makeRom(), "dp_ind_y_bank_carry.sfc", "dp-ind-y-bank-carry"));
    bus.attachCPU(&cpu);
    bus.attachCartridge(&cart);
    cpu.attachBus(&bus);
    bus.reset();
    cpu.reset();
    CPU::State state{};
    state.pc = 0x8000;
    state.pb = 0x00;
    state.a = 0x1234;
    state.x = 0x3456;
    state.y = 0x1100;
    state.s = 0x01EF;
    state.d = 0xFFFF;
    state.db = 0x7E;
    state.p = Flag_C;
    state.e = false;
    cpu.setStateForTesting(state);
    bus.write(0x000033, 0xDC);
    bus.write(0x000034, 0xFE);
    bus.write(0x7F0FDC, 0xCB);
    bus.write(0x7F0FDD, 0xED);
    bus.write(0x7E0FDC, 0x00);
    bus.write(0x7E0FDD, 0x00);
    cpu.step();
    while (cpu.hasPendingCycles()) cpu.completeCycle();
    assert(cpu.a() == 0x0000);
    assert((cpu.p() & (Flag_C | Flag_Z)) == (Flag_C | Flag_Z));
    std::puts("cpu (dp),Y bank-carry regression ok");
}

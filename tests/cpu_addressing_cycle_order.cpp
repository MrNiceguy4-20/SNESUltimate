#include "cpu.hpp"
#include "bus.hpp"
#include "cartridge.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <vector>
namespace {
using Kind = CPU::PendingCycleInfo::Kind;
std::vector<uint8_t> makeRom(std::initializer_list<uint8_t> code) {
    std::vector<uint8_t> rom(0x10000, 0xEA);
    std::size_t i = 0;
    for (uint8_t byte : code) rom[i++] = byte;
    constexpr std::size_t header = 0x7FC0;
    const char title[] = "CPU ADDRESS CYCLE   ";
    for (std::size_t j = 0; j < 21; ++j) rom[header + j] = static_cast<uint8_t>(title[j]);
    rom[header + 0x15] = 0x20;
    rom[header + 0x17] = 0x06;
    rom[header + 0x19] = 0x01;
    rom[0x7FFC] = 0x00;
    rom[0x7FFD] = 0x80;
    return rom;
}
struct Rig {
    Cartridge cart;
    Bus bus;
    CPU cpu;
    explicit Rig(std::vector<uint8_t> rom) {
        assert(cart.loadData(std::move(rom), "cpu_addressing_cycle.sfc", "address-cycle"));
        bus.attachCPU(&cpu);
        bus.attachCartridge(&cart);
        cpu.attachBus(&bus);
        bus.reset();
        cpu.reset();
        cpu.setCycleStepping(true);
    }
};
void drain(CPU& cpu) {
    while (cpu.hasPendingCycles()) cpu.completeCycle();
}
void runInstruction(CPU& cpu) {
    cpu.step();
    drain(cpu);
}
void expect(CPU& cpu, Kind kind, uint32_t address = 0, uint8_t clocks = 0) {
    CPU::PendingCycleInfo info;
    assert(cpu.peekNextCycle(info));
    assert(info.kind == kind);
    if (kind != Kind::Internal) assert(info.address == address);
    if (clocks != 0) assert(info.master_clocks == clocks);
    cpu.completeCycle();
}
}
int main() {
    {
        Rig r(makeRom({0x48, 0x68}));
        r.cpu.step();
        expect(r.cpu, Kind::Read, 0x008000, 8);
        expect(r.cpu, Kind::Internal, 0, 6);
        expect(r.cpu, Kind::Write, 0x0001FF, 8);
        assert(!r.cpu.hasPendingCycles());
        assert(r.cpu.s() == 0x01FE);
        r.cpu.step();
        expect(r.cpu, Kind::Read, 0x008001, 8);
        expect(r.cpu, Kind::Internal, 0, 6);
        expect(r.cpu, Kind::Internal, 0, 6);
        expect(r.cpu, Kind::Read, 0x0001FF, 8);
        assert(!r.cpu.hasPendingCycles());
        assert(r.cpu.s() == 0x01FF);
    }
    {
        Rig r(makeRom({0xA2, 0x01,
                       0xB5, 0x10}));
        r.bus.write(0x000011, 0x5A);
        runInstruction(r.cpu);
        r.cpu.step();
        expect(r.cpu, Kind::Read, 0x008002, 8);
        expect(r.cpu, Kind::Read, 0x008003, 8);
        expect(r.cpu, Kind::Read, 0x008003, 8);
        expect(r.cpu, Kind::Read, 0x000011, 8);
        assert(static_cast<uint8_t>(r.cpu.a()) == 0x5A);
    }
    {
        Rig r(makeRom({0xA2, 0x01,
                       0xBD, 0xFF, 0x00}));
        r.bus.write(0x000100, 0x77);
        runInstruction(r.cpu);
        r.cpu.step();
        expect(r.cpu, Kind::Read, 0x008002, 8);
        expect(r.cpu, Kind::Read, 0x008003, 8);
        expect(r.cpu, Kind::Read, 0x008004, 8);
        expect(r.cpu, Kind::Read, 0x000000, 8);
        expect(r.cpu, Kind::Read, 0x000100, 8);
        assert(static_cast<uint8_t>(r.cpu.a()) == 0x77);
    }
    {
        Rig r(makeRom({0xA2, 0x01,
                       0x9D, 0x10, 0x00}));
        runInstruction(r.cpu);
        r.cpu.step();
        expect(r.cpu, Kind::Read, 0x008002, 8);
        expect(r.cpu, Kind::Read, 0x008003, 8);
        expect(r.cpu, Kind::Read, 0x008004, 8);
        expect(r.cpu, Kind::Read, 0x000011, 8);
        expect(r.cpu, Kind::Write, 0x000011, 8);
    }
    {
        Rig r(makeRom({0xA3, 0x01}));
        r.bus.write(0x000200, 0x33);
        r.cpu.step();
        expect(r.cpu, Kind::Read, 0x008000, 8);
        expect(r.cpu, Kind::Read, 0x008001, 8);
        expect(r.cpu, Kind::Internal, 0, 6);
        expect(r.cpu, Kind::Read, 0x000200, 8);
        assert(static_cast<uint8_t>(r.cpu.a()) == 0x33);
    }
    {
        Rig r(makeRom({0xB3, 0x0F}));
        CPU::State st = r.cpu.state();
        st.s = 0x01F0;
        st.y = 0x0001;
        r.cpu.setStateForTesting(st);
        r.bus.write(0x0001FF, 0x34);
        r.bus.write(0x000200, 0x12);
        r.bus.write(0x001235, 0x6C);
        r.cpu.step();
        expect(r.cpu, Kind::Read,     0x008000, 8);
        expect(r.cpu, Kind::Read,     0x008001, 8);
        expect(r.cpu, Kind::Internal, 0,        6);
        expect(r.cpu, Kind::Read,     0x0001FF, 8);
        expect(r.cpu, Kind::Read,     0x000200, 8);
        expect(r.cpu, Kind::Internal, 0,        6);
        expect(r.cpu, Kind::Read,     0x001235, 8);
        assert(static_cast<uint8_t>(r.cpu.a()) == 0x6C);
    }
    {
        Rig r(makeRom({0x93, 0x0F}));
        CPU::State st = r.cpu.state();
        st.s = 0x01F0;
        st.y = 0x0001;
        st.a = 0x005A;
        r.cpu.setStateForTesting(st);
        r.bus.write(0x0001FF, 0x34);
        r.bus.write(0x000200, 0x12);
        r.cpu.step();
        expect(r.cpu, Kind::Read,     0x008000, 8);
        expect(r.cpu, Kind::Read,     0x008001, 8);
        expect(r.cpu, Kind::Internal, 0,        6);
        expect(r.cpu, Kind::Read,     0x0001FF, 8);
        expect(r.cpu, Kind::Read,     0x000200, 8);
        expect(r.cpu, Kind::Internal, 0,        6);
        expect(r.cpu, Kind::Write,    0x001235, 8);
        assert(r.bus.read(0x001235) == 0x5A);
    }
    {
        Rig r(makeRom({0x20, 0x04, 0x80, 0xEA, 0x60}));
        runInstruction(r.cpu);
        assert(r.cpu.pc() == 0x8004);
        assert(r.cpu.s() == 0x01FD);
        r.cpu.step();
        expect(r.cpu, Kind::Read, 0x008004, 8);
        expect(r.cpu, Kind::Internal, 0, 6);
        expect(r.cpu, Kind::Internal, 0, 6);
        expect(r.cpu, Kind::Read, 0x0001FE, 8);
        expect(r.cpu, Kind::Read, 0x0001FF, 8);
        expect(r.cpu, Kind::Internal, 0, 6);
        assert(!r.cpu.hasPendingCycles());
        assert(r.cpu.pc() == 0x8003);
    }
    std::puts("cpu addressing cycle-order regression ok");
}

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
    const char title[] = "CPU CYCLE ORDER TEST ";
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
        assert(cart.loadData(std::move(rom), "cpu_cycle_order.sfc", "cycle-order"));
        bus.attachCPU(&cpu);
        bus.attachCartridge(&cart);
        cpu.attachBus(&bus);
        bus.reset();
        cpu.reset();
        cpu.setCycleStepping(true);
    }
};
CPU::PendingCycleInfo next(CPU& cpu) {
    CPU::PendingCycleInfo info;
    assert(cpu.peekNextCycle(info));
    return info;
}
void expect(CPU& cpu, Kind kind, uint32_t address, uint8_t clocks) {
    const auto info = next(cpu);
    assert(info.kind == kind);
    assert(info.master_clocks == clocks);
    if (kind != Kind::Internal) assert(info.address == address);
    cpu.completeCycle();
}
}
int main() {
    {
        Rig r(makeRom({0x20, 0x10, 0x80}));
        r.cpu.step();
        expect(r.cpu, Kind::Read,     0x008000, 8);
        expect(r.cpu, Kind::Read,     0x008001, 8);
        expect(r.cpu, Kind::Read,     0x008002, 8);
        expect(r.cpu, Kind::Internal, 0,        6);
        expect(r.cpu, Kind::Write,    0x0001FF, 8);
        expect(r.cpu, Kind::Write,    0x0001FE, 8);
        assert(!r.cpu.hasPendingCycles());
        assert(r.cpu.pc() == 0x8010);
        assert(r.cpu.s() == 0x01FD);
    }
    {
        Rig r(makeRom({0x22, 0x34, 0x12, 0x01}));
        r.cpu.step();
        expect(r.cpu, Kind::Read,     0x008000, 8);
        expect(r.cpu, Kind::Read,     0x008001, 8);
        expect(r.cpu, Kind::Read,     0x008002, 8);
        expect(r.cpu, Kind::Write,    0x0001FF, 8);
        expect(r.cpu, Kind::Internal, 0,        6);
        expect(r.cpu, Kind::Read,     0x008003, 8);
        expect(r.cpu, Kind::Write,    0x0001FE, 8);
        expect(r.cpu, Kind::Write,    0x0001FD, 8);
        assert(!r.cpu.hasPendingCycles());
        assert(r.cpu.pb() == 0x01 && r.cpu.pc() == 0x1234);
        assert(r.cpu.s() == 0x01FC);
    }
    {
        auto rom = makeRom({0xFC, 0x00, 0x90});
        rom[0x1002] = 0x78;
        rom[0x1003] = 0x56;
        Rig r(std::move(rom));
        CPU::State st = r.cpu.state();
        st.x = 0x0002;
        r.cpu.setStateForTesting(st);
        r.cpu.step();
        expect(r.cpu, Kind::Read,     0x008000, 8);
        expect(r.cpu, Kind::Read,     0x008001, 8);
        expect(r.cpu, Kind::Write,    0x0001FF, 8);
        expect(r.cpu, Kind::Write,    0x0001FE, 8);
        expect(r.cpu, Kind::Read,     0x008002, 8);
        expect(r.cpu, Kind::Internal, 0,        6);
        expect(r.cpu, Kind::Read,     0x009002, 8);
        expect(r.cpu, Kind::Read,     0x009003, 8);
        assert(!r.cpu.hasPendingCycles());
        assert(r.cpu.pc() == 0x5678);
    }
    {
        Rig r(makeRom({0x6B}));
        CPU::State st = r.cpu.state();
        st.s = 0x01FF;
        r.cpu.setStateForTesting(st);
        r.bus.write(0x000200, 0x34);
        r.bus.write(0x000201, 0x12);
        r.bus.write(0x000202, 0x03);
        r.cpu.step();
        expect(r.cpu, Kind::Read,     0x008000, 8);
        expect(r.cpu, Kind::Internal, 0,        6);
        expect(r.cpu, Kind::Internal, 0,        6);
        expect(r.cpu, Kind::Read,     0x000200, 8);
        expect(r.cpu, Kind::Read,     0x000201, 8);
        expect(r.cpu, Kind::Read,     0x000202, 8);
        assert(!r.cpu.hasPendingCycles());
        assert(r.cpu.pc() == 0x1235 && r.cpu.pb() == 0x03);
        assert(r.cpu.s() == 0x0102);
    }
    {
        Rig r(makeRom({0xFB,
                       0xC2, 0x20,
                       0xEE, 0x00, 0x00
        }));
        r.bus.write(0x000000, 0x34);
        r.bus.write(0x000001, 0x12);
        r.cpu.step(); while (r.cpu.hasPendingCycles()) r.cpu.completeCycle();
        r.cpu.step(); while (r.cpu.hasPendingCycles()) r.cpu.completeCycle();
        assert(!r.cpu.emulation());
        assert((r.cpu.p() & Flag_M) == 0);
        r.cpu.step();
        expect(r.cpu, Kind::Read,     0x008003, 8);
        expect(r.cpu, Kind::Read,     0x008004, 8);
        expect(r.cpu, Kind::Read,     0x008005, 8);
        expect(r.cpu, Kind::Read,     0x000000, 8);
        expect(r.cpu, Kind::Read,     0x000001, 8);
        expect(r.cpu, Kind::Read,     0x000001, 8);
        auto high_write = next(r.cpu);
        assert(high_write.kind == Kind::Write && high_write.address == 0x000001 && high_write.value == 0x12);
        r.cpu.completeCycle();
        auto low_write = next(r.cpu);
        assert(low_write.kind == Kind::Write && low_write.address == 0x000000 && low_write.value == 0x35);
        r.cpu.completeCycle();
        assert(!r.cpu.hasPendingCycles());
        assert(r.bus.read(0x000000) == 0x35);
        assert(r.bus.read(0x000001) == 0x12);
    }
    {
        Rig r(makeRom({0xC2, 0x20, 0xEA}));
        r.cpu.step();
        expect(r.cpu, Kind::Read,     0x008000, 8);
        expect(r.cpu, Kind::Read,     0x008001, 8);
        expect(r.cpu, Kind::Internal, 0,        6);
        assert(!r.cpu.hasPendingCycles());
    }
    {
        Rig r(makeRom({0x18, 0xEA}));
        r.cpu.step();
        expect(r.cpu, Kind::Read,     0x008000, 8);
        expect(r.cpu, Kind::Internal, 0,        6);
        assert(!r.cpu.hasPendingCycles());
    }
    {
        Rig r(makeRom({0x62, 0x02, 0x00, 0xEA}));
        r.cpu.step();
        expect(r.cpu, Kind::Read,     0x008000, 8);
        expect(r.cpu, Kind::Read,     0x008001, 8);
        expect(r.cpu, Kind::Read,     0x008002, 8);
        expect(r.cpu, Kind::Internal, 0,        6);
        expect(r.cpu, Kind::Write,    0x0001FF, 8);
        expect(r.cpu, Kind::Write,    0x0001FE, 8);
        assert(!r.cpu.hasPendingCycles());
    }
    std::puts("cpu cycle-order regression ok");
}

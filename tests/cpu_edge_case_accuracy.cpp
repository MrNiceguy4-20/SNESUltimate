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
    const char title[] = "CPU EDGE ACCURACY   ";
    for (std::size_t j = 0; j < 21; ++j) rom[header + j] = static_cast<uint8_t>(title[j]);
    rom[header + 0x15] = 0x20;
    rom[header + 0x17] = 0x06;
    rom[header + 0x19] = 0x01;
    rom[0x7FE4] = 0x40; rom[0x7FE5] = 0x90;
    rom[0x7FE6] = 0x20; rom[0x7FE7] = 0x90;
    rom[0x7FEA] = 0x00; rom[0x7FEB] = 0x90;
    rom[0x7FEE] = 0x60; rom[0x7FEF] = 0x90;
    rom[0x7FF4] = 0x40; rom[0x7FF5] = 0x90;
    rom[0x7FFA] = 0x00; rom[0x7FFB] = 0x90;
    rom[0x7FFC] = 0x00; rom[0x7FFD] = 0x80;
    rom[0x7FFE] = 0x20; rom[0x7FFF] = 0x90;
    rom[0x1000] = 0x40;
    rom[0x1020] = 0x40;
    rom[0x1040] = 0x40;
    rom[0x1060] = 0x40;
    return rom;
}
struct Rig {
    Cartridge cart;
    Bus bus;
    CPU cpu;
    explicit Rig(std::vector<uint8_t> rom) {
        assert(cart.loadData(std::move(rom), "cpu_edge_accuracy.sfc", "edge-accuracy"));
        bus.attachCPU(&cpu);
        bus.attachCartridge(&cart);
        cpu.attachBus(&bus);
        bus.reset();
        cpu.reset();
        cpu.setCycleStepping(true);
    }
};
void drain(CPU& cpu) { while (cpu.hasPendingCycles()) cpu.completeCycle(); }
void runInstruction(CPU& cpu) { cpu.step(); drain(cpu); }
void expect(CPU& cpu, Kind kind, uint32_t address = 0) {
    CPU::PendingCycleInfo info;
    assert(cpu.peekNextCycle(info));
    assert(info.kind == kind);
    if (kind != Kind::Internal) assert(info.address == address);
    cpu.completeCycle();
}
}
int main() {
    {
        Rig r(makeRom({
            0xF4, 0x00, 0x12,
            0x2B,
            0xA2, 0x20,
            0xB5, 0xF0
        }));
        r.bus.write(0x001210, 0x5A);
        r.bus.write(0x001310, 0xA5);
        runInstruction(r.cpu); runInstruction(r.cpu); runInstruction(r.cpu); runInstruction(r.cpu);
        assert(r.cpu.dp() == 0x1200);
        assert(static_cast<uint8_t>(r.cpu.a()) == 0x5A);
    }
    {
        Rig r(makeRom({
            0x18, 0xFB,
            0xF4, 0x00, 0x12,
            0x2B,
            0xA2, 0x20,
            0xB5, 0xF0
        }));
        r.bus.write(0x001210, 0x5A);
        r.bus.write(0x001310, 0xA5);
        for (int i = 0; i < 6; ++i) runInstruction(r.cpu);
        assert(!r.cpu.emulation());
        assert(static_cast<uint8_t>(r.cpu.a()) == 0xA5);
    }
    {
        Rig r(makeRom({
            0xF4, 0x00, 0x12,
            0x2B,
            0xB2, 0xFF
        }));
        r.bus.write(0x0012FF, 0x34);
        r.bus.write(0x001200, 0x12);
        r.bus.write(0x001300, 0x56);
        r.bus.write(0x001234, 0x7C);
        runInstruction(r.cpu); runInstruction(r.cpu); runInstruction(r.cpu);
        assert(static_cast<uint8_t>(r.cpu.a()) == 0x7C);
    }
    {
        Rig r(makeRom({
            0xF4, 0x01, 0x12,
            0x2B,
            0xA2, 0x01,
            0xA1, 0xFD
        }));
        r.bus.write(0x0012FF, 0x34);
        r.bus.write(0x001300, 0x10);
        r.bus.write(0x001200, 0x12);
        r.bus.write(0x001034, 0xA6);
        r.bus.write(0x001234, 0x5B);
        runInstruction(r.cpu); runInstruction(r.cpu); runInstruction(r.cpu); runInstruction(r.cpu);
        assert(r.cpu.dp() == 0x1201);
        assert(static_cast<uint8_t>(r.cpu.a()) == 0x5B);
    }
    {
        Rig r(makeRom({
            0xF4, 0x00, 0x12,
            0x2B,
            0xA7, 0xFF
        }));
        r.bus.write(0x0012FF, 0x00);
        r.bus.write(0x001300, 0x20);
        r.bus.write(0x001301, 0x7E);
        r.bus.write(0x7E2000, 0x66);
        runInstruction(r.cpu); runInstruction(r.cpu); runInstruction(r.cpu);
        assert(static_cast<uint8_t>(r.cpu.a()) == 0x66);
    }
    {
        Rig r(makeRom({0xEA}));
        r.cpu.requestNMI();
        r.cpu.step();
        expect(r.cpu, Kind::Read, 0x008000);
        expect(r.cpu, Kind::Internal);
        expect(r.cpu, Kind::Write, 0x0001FF);
        expect(r.cpu, Kind::Write, 0x0001FE);
        expect(r.cpu, Kind::Write, 0x0001FD);
        expect(r.cpu, Kind::Read, 0x00FFFA);
        expect(r.cpu, Kind::Read, 0x00FFFB);
        assert(!r.cpu.hasPendingCycles());
        assert(r.cpu.pc() == 0x9000);
    }
    {
        Rig r(makeRom({0x00, 0x5A}));
        CPU::State st = r.cpu.state();
        st.p = static_cast<uint8_t>(Flag_M | Flag_X | Flag_D | Flag_C);
        st.pc = 0x8000;
        r.cpu.setStateForTesting(st);
        runInstruction(r.cpu);
        assert(r.cpu.pc() == 0x9020 && r.cpu.pb() == 0);
        assert(r.cpu.s() == 0x01FC);
        assert(r.bus.read(0x0001FF) == 0x80);
        assert(r.bus.read(0x0001FE) == 0x02);
        assert((r.bus.read(0x0001FD) & 0x10) != 0);
        assert((r.bus.read(0x0001FD) & Flag_D) != 0);
        assert((r.cpu.p() & Flag_D) == 0 && (r.cpu.p() & Flag_I) != 0);
        runInstruction(r.cpu);
        assert(r.cpu.pc() == 0x8002);
        assert((r.cpu.p() & Flag_D) != 0 && (r.cpu.p() & Flag_I) == 0);
    }
    {
        Rig r(makeRom({0x02, 0xA5}));
        CPU::State st = r.cpu.state();
        st.p = static_cast<uint8_t>(Flag_M | Flag_X | Flag_D);
        st.pc = 0x8000;
        r.cpu.setStateForTesting(st);
        runInstruction(r.cpu);
        assert(r.cpu.pc() == 0x9040);
        assert((r.bus.read(0x0001FD) & 0x10) != 0);
        runInstruction(r.cpu);
        assert(r.cpu.pc() == 0x8002);
    }
    {
        Rig r(makeRom({0x00, 0x11}));
        CPU::State st{};
        st.pc = 0x8000; st.s = 0x01FF; st.p = static_cast<uint8_t>(Flag_D | Flag_C);
        st.pb = 0; st.db = 0x7E; st.e = false;
        r.cpu.setStateForTesting(st);
        runInstruction(r.cpu);
        assert(r.cpu.pc() == 0x9020 && r.cpu.pb() == 0 && r.cpu.s() == 0x01FB);
        assert(r.bus.read(0x0001FF) == 0x00);
        assert(r.bus.read(0x0001FE) == 0x80);
        assert(r.bus.read(0x0001FD) == 0x02);
        assert(r.bus.read(0x0001FC) == static_cast<uint8_t>(Flag_D | Flag_C));
        runInstruction(r.cpu);
        assert(r.cpu.pc() == 0x8002 && r.cpu.pb() == 0);
        assert(r.cpu.p() == static_cast<uint8_t>(Flag_D | Flag_C));
    }
    {
        Rig r(makeRom({0xEA}));
        CPU::State st = r.cpu.state();
        st.p = static_cast<uint8_t>(Flag_M | Flag_X | Flag_D);
        r.cpu.setStateForTesting(st);
        r.cpu.setIRQLine(true);
        r.cpu.requestNMI();
        runInstruction(r.cpu);
        assert(r.cpu.pc() == 0x9000);
        assert((r.bus.read(0x0001FD) & 0x10) == 0);
        assert((r.bus.read(0x0001FD) & Flag_D) != 0);
        assert((r.cpu.p() & Flag_D) == 0);
    }
    {
        Rig r(makeRom({0x78, 0xCB, 0xEA}));
        runInstruction(r.cpu);
        runInstruction(r.cpu);
        assert(r.cpu.waiting() && r.cpu.pc() == 0x8002);
        const uint64_t before = r.cpu.cycles();
        r.cpu.setIRQLine(true);
        r.cpu.step();
        expect(r.cpu, Kind::Internal);
        expect(r.cpu, Kind::Internal);
        assert(!r.cpu.hasPendingCycles());
        assert(!r.cpu.waiting());
        assert(r.cpu.cycles() - before == 2);
        r.cpu.step();
        expect(r.cpu, Kind::Read, 0x008002);
        drain(r.cpu);
        assert(r.cpu.pc() == 0x8003);
    }
    {
        Rig r(makeRom({0xCB, 0xEA}));
        runInstruction(r.cpu);
        assert(r.cpu.waiting());
        r.cpu.requestNMI();
        r.cpu.step();
        expect(r.cpu, Kind::Internal);
        expect(r.cpu, Kind::Internal);
        assert(!r.cpu.waiting() && r.cpu.nmiPending());
        r.cpu.step();
        expect(r.cpu, Kind::Read, 0x008001);
        expect(r.cpu, Kind::Internal);
        drain(r.cpu);
        assert(r.cpu.pc() == 0x9000);
    }
    {
        Rig r(makeRom({0x58, 0xEA, 0xEA}));
        r.cpu.step(); expect(r.cpu, Kind::Read, 0x008000);
        r.cpu.setIRQLine(true);
        expect(r.cpu, Kind::Internal);
        assert((r.cpu.p() & Flag_I) == 0);
        r.cpu.step(); expect(r.cpu, Kind::Read, 0x008001); drain(r.cpu);
        r.cpu.step(); expect(r.cpu, Kind::Read, 0x008002); drain(r.cpu);
        assert(r.cpu.pc() == 0x9020);
    }
    {
        Rig r(makeRom({0x78, 0xEA}));
        CPU::State st = r.cpu.state();
        st.p = static_cast<uint8_t>((st.p | Flag_M | Flag_X) & ~Flag_I);
        r.cpu.setStateForTesting(st);
        r.cpu.step(); expect(r.cpu, Kind::Read, 0x008000);
        r.cpu.setIRQLine(true);
        expect(r.cpu, Kind::Internal);
        assert((r.cpu.p() & Flag_I) != 0);
        r.cpu.step(); expect(r.cpu, Kind::Read, 0x008001); drain(r.cpu);
        assert(r.cpu.pc() == 0x9020);
    }
    {
        Rig r(makeRom({0x28, 0xEA}));
        CPU::State st = r.cpu.state();
        st.s = 0x01FE; st.p = static_cast<uint8_t>(Flag_M | Flag_X | Flag_I);
        r.cpu.setStateForTesting(st);
        r.bus.write(0x0001FF, static_cast<uint8_t>(Flag_M | Flag_X));
        r.cpu.step();
        expect(r.cpu, Kind::Read, 0x008000);
        expect(r.cpu, Kind::Internal);
        expect(r.cpu, Kind::Internal);
        expect(r.cpu, Kind::Read, 0x0001FF);
        r.cpu.setIRQLine(true);
        assert((r.cpu.p() & Flag_I) == 0);
        r.cpu.step(); expect(r.cpu, Kind::Read, 0x008001); drain(r.cpu);
        r.cpu.step(); expect(r.cpu, Kind::Read, 0x008002); drain(r.cpu);
        assert(r.cpu.pc() == 0x9020);
    }
    {
        Rig r(makeRom({0xC2, 0x04, 0xEA, 0xEA}));
        r.cpu.step();
        expect(r.cpu, Kind::Read, 0x008000);
        expect(r.cpu, Kind::Read, 0x008001);
        r.cpu.setIRQLine(true);
        expect(r.cpu, Kind::Internal);
        assert((r.cpu.p() & Flag_I) == 0);
        r.cpu.step(); expect(r.cpu, Kind::Read, 0x008002); drain(r.cpu);
        r.cpu.step(); expect(r.cpu, Kind::Read, 0x008003); drain(r.cpu);
        assert(r.cpu.pc() == 0x9020);
    }
    {
        Rig r(makeRom({0xE2, 0x04, 0xEA}));
        CPU::State st = r.cpu.state();
        st.p = static_cast<uint8_t>((st.p | Flag_M | Flag_X) & ~Flag_I);
        r.cpu.setStateForTesting(st);
        r.cpu.step();
        expect(r.cpu, Kind::Read, 0x008000);
        expect(r.cpu, Kind::Read, 0x008001);
        r.cpu.setIRQLine(true);
        expect(r.cpu, Kind::Internal);
        assert((r.cpu.p() & Flag_I) != 0);
        r.cpu.step(); expect(r.cpu, Kind::Read, 0x008002); drain(r.cpu);
        assert(r.cpu.pc() == 0x9020);
    }
    {
        Rig r(makeRom({0xDB, 0xEA}));
        runInstruction(r.cpu);
        assert(r.cpu.stopped() && r.cpu.pc() == 0x8001);
        r.cpu.setIRQLine(true);
        r.cpu.requestNMI();
        r.cpu.step();
        assert(!r.cpu.hasPendingCycles());
        assert(r.cpu.stopped() && r.cpu.pc() == 0x8001);
    }
    {
        Rig r(makeRom({
            0x18, 0xFB,
            0xA9, 0x00,
            0xA2, 0x10,
            0xA0, 0x20,
            0x54, 0x7E, 0x7F
        }));
        r.bus.write(0x7F0010, 0x9B);
        for (int i = 0; i < 5; ++i) runInstruction(r.cpu);
        r.cpu.step();
        expect(r.cpu, Kind::Read, 0x008008);
        expect(r.cpu, Kind::Read, 0x008009);
        expect(r.cpu, Kind::Read, 0x00800A);
        expect(r.cpu, Kind::Read, 0x7F0010);
        expect(r.cpu, Kind::Write, 0x7E0020);
        assert(r.cpu.a() == 0x0000);
        assert(r.cpu.x() == 0x0010);
        assert(r.cpu.y() == 0x0020);
        assert(r.cpu.db() == 0x00);
        assert(r.cpu.pc() == 0x800B);
        expect(r.cpu, Kind::Internal);
        assert(r.cpu.a() == 0x0000);
        assert(r.cpu.x() == 0x0010);
        assert(r.cpu.y() == 0x0020);
        assert(r.cpu.db() == 0x00);
        assert(r.cpu.pc() == 0x800B);
        expect(r.cpu, Kind::Internal);
        assert(r.cpu.a() == 0xFFFF);
        assert(r.cpu.x() == 0x0011);
        assert(r.cpu.y() == 0x0021);
        assert(r.cpu.db() == 0x7E);
        assert(r.cpu.pc() == 0x800B);
        assert(r.bus.read(0x7E0020) == 0x9B);
    }
    {
        Rig r(makeRom({
            0x18, 0xFB,
            0x58,
            0xA9, 0x01,
            0xA2, 0x10,
            0xA0, 0x20,
            0x54, 0x7E, 0x7F,
            0xEA
        }));
        r.bus.write(0x7F0010, 0xA1);
        r.bus.write(0x7F0011, 0xB2);
        for (int i = 0; i < 6; ++i) runInstruction(r.cpu);
        assert(r.cpu.pc() == 0x8009);
        r.cpu.step();
        expect(r.cpu, Kind::Read, 0x008009);
        expect(r.cpu, Kind::Read, 0x00800A);
        expect(r.cpu, Kind::Read, 0x00800B);
        expect(r.cpu, Kind::Read, 0x7F0010);
        expect(r.cpu, Kind::Write, 0x7E0020);
        expect(r.cpu, Kind::Internal);
        r.cpu.requestNMI();
        assert(r.cpu.nmiPending());
        expect(r.cpu, Kind::Internal);
        assert(r.cpu.a() == 0x0000);
        assert(r.cpu.x() == 0x0011 && r.cpu.y() == 0x0021);
        assert(r.cpu.pc() == 0x8009);
        r.cpu.step();
        drain(r.cpu);
        assert(r.cpu.pc() == 0x9000);
        assert(r.bus.read(0x0001FE) == 0x80);
        assert(r.bus.read(0x0001FD) == 0x09);
        runInstruction(r.cpu);
        assert(r.cpu.pc() == 0x8009);
        runInstruction(r.cpu);
        assert(r.cpu.a() == 0xFFFF);
        assert(r.cpu.x() == 0x0012 && r.cpu.y() == 0x0022);
        assert(r.cpu.pc() == 0x800C);
        assert(r.bus.read(0x7E0020) == 0xA1);
        assert(r.bus.read(0x7E0021) == 0xB2);
    }
    {
        Rig r(makeRom({
            0x18, 0xFB,
            0xA9, 0x00,
            0xA2, 0x50,
            0xA0, 0x60,
            0x54, 0x7E, 0x7F,
            0xEA
        }));
        r.bus.write(0x7F0050, 0xE5);
        for (int i = 0; i < 5; ++i) runInstruction(r.cpu);
        assert(r.cpu.pc() == 0x8008);
        r.cpu.step();
        expect(r.cpu, Kind::Read, 0x008008);
        expect(r.cpu, Kind::Read, 0x008009);
        expect(r.cpu, Kind::Read, 0x00800A);
        expect(r.cpu, Kind::Read, 0x7F0050);
        expect(r.cpu, Kind::Write, 0x7E0060);
        r.cpu.requestNMI();
        expect(r.cpu, Kind::Internal);
        expect(r.cpu, Kind::Internal);
        assert(r.cpu.a() == 0xFFFF);
        assert(r.cpu.pc() == 0x800B);
        r.cpu.step();
        drain(r.cpu);
        assert(r.cpu.pc() == 0x9000);
        assert(r.bus.read(0x0001FE) == 0x80);
        assert(r.bus.read(0x0001FD) == 0x0B);
        runInstruction(r.cpu);
        assert(r.cpu.pc() == 0x800B);
        assert(r.bus.read(0x7E0060) == 0xE5);
    }
    {
        Rig r(makeRom({
            0x18, 0xFB,
            0x58,
            0xA9, 0x01,
            0xA2, 0x30,
            0xA0, 0x40,
            0x54, 0x7E, 0x7F,
            0xEA
        }));
        r.bus.write(0x7F0030, 0xC3);
        r.bus.write(0x7F0031, 0xD4);
        for (int i = 0; i < 6; ++i) runInstruction(r.cpu);
        r.cpu.step();
        expect(r.cpu, Kind::Read, 0x008009);
        expect(r.cpu, Kind::Read, 0x00800A);
        expect(r.cpu, Kind::Read, 0x00800B);
        expect(r.cpu, Kind::Read, 0x7F0030);
        expect(r.cpu, Kind::Write, 0x7E0040);
        r.cpu.setIRQLine(true);
        expect(r.cpu, Kind::Internal);
        expect(r.cpu, Kind::Internal);
        assert(r.cpu.pc() == 0x8009 && r.cpu.a() == 0x0000);
        r.cpu.step();
        drain(r.cpu);
        assert(r.cpu.pc() == 0x9060);
        r.cpu.setIRQLine(false);
        runInstruction(r.cpu);
        assert(r.cpu.pc() == 0x8009);
        runInstruction(r.cpu);
        assert(r.cpu.a() == 0xFFFF && r.cpu.pc() == 0x800C);
        assert(r.bus.read(0x7E0040) == 0xC3);
        assert(r.bus.read(0x7E0041) == 0xD4);
    }
    std::puts("cpu edge-case accuracy regression ok");
}

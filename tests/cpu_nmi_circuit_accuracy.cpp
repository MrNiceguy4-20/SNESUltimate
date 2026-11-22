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
    constexpr std::size_t header = 0x7FC0;
    const char title[] = "NMI CIRCUIT ACCURACY ";
    for (std::size_t i = 0; i < 21; ++i) rom[header + i] = static_cast<uint8_t>(title[i]);
    rom[header + 0x15] = 0x20;
    rom[header + 0x17] = 0x06;
    rom[header + 0x19] = 0x01;
    rom[0x0000] = 0x78;
    rom[0x0001] = 0xCB;
    rom[0x0002] = 0xEA;
    rom[0x1000] = 0x40;
    rom[0x7FFA] = 0x00; rom[0x7FFB] = 0x90;
    rom[0x7FFC] = 0x00; rom[0x7FFD] = 0x80;
    return rom;
}
struct Rig {
    Cartridge cart;
    Bus bus;
    CPU cpu;
    Rig() {
        assert(cart.loadData(makeRom(), "nmi-circuit.sfc", "nmi-circuit"));
        bus.attachCartridge(&cart);
        bus.attachCPU(&cpu);
        cpu.attachBus(&bus);
        bus.reset();
        cpu.reset();
        cpu.setCycleStepping(true);
    }
};
void drain(CPU& cpu) { while (cpu.hasPendingCycles()) cpu.completeCycle(); }
void runInstruction(CPU& cpu) { cpu.step(); drain(cpu); }
void enterVblank(Rig& r) { r.bus.updateVideoTiming(225, 0, false, true); }
}
int main() {
    {
        Rig r;
        enterVblank(r);
        r.bus.triggerVBlankNmiEdge();
        assert(!r.bus.nmiLineAsserted());
        assert(!r.cpu.nmiPending());
        r.bus.write(0x004200, 0x80);
        assert(r.bus.nmiLineAsserted());
        assert(r.cpu.nmiPending());
        r.bus.write(0x004200, 0x00);
        assert(!r.bus.nmiLineAsserted());
        assert(r.cpu.nmiPending());
    }
    {
        Rig r;
        r.bus.write(0x004200, 0x80);
        enterVblank(r);
        r.bus.triggerVBlankNmiEdge();
        assert(r.bus.nmiLineAsserted() && r.cpu.nmiPending());
        const uint8_t rdnmi = r.bus.read(0x004210);
        assert((rdnmi & 0x80) != 0);
        assert(!r.bus.nmiLineAsserted());
        assert(r.cpu.nmiPending());
    }
    {
        Rig r;
        r.bus.write(0x004200, 0x80);
        enterVblank(r);
        r.bus.triggerVBlankNmiEdge();
        assert(r.cpu.nmiPending());
        r.cpu.step();
        drain(r.cpu);
        assert(!r.cpu.nmiPending());
        r.bus.write(0x004200, 0x00);
        assert(!r.bus.nmiLineAsserted());
        r.bus.write(0x004200, 0x80);
        assert(r.bus.nmiLineAsserted());
        assert(r.cpu.nmiPending());
    }
    {
        Rig r;
        runInstruction(r.cpu);
        runInstruction(r.cpu);
        assert(r.cpu.waiting());
        r.bus.write(0x004200, 0x80);
        enterVblank(r);
        r.bus.triggerVBlankNmiEdge();
        assert(r.cpu.nmiPending());
        r.cpu.step();
        assert(!r.cpu.waiting());
        assert(r.cpu.nmiPending());
        assert(r.cpu.hasPendingCycles());
        drain(r.cpu);
        r.cpu.step();
        drain(r.cpu);
        assert(r.cpu.pc() == 0x9000);
    }
    std::puts("cpu_nmi_circuit_accuracy: ok");
    return 0;
}

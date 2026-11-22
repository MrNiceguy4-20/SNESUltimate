#include "bus.hpp"
#include "cpu.hpp"
#include "snes.hpp"
#include <cstdint>
#include <iostream>
namespace {
int failures = 0;
void expect(bool c, const char* m) {
    if (!c) { std::cerr << "FAIL: " << m << '\n'; ++failures; }
}
}
int main() {
    Bus bus;
    bus.reset();
    uint64_t generation = bus.irqConfigGeneration();
    const uint16_t regs[] = {0x4207, 0x4208, 0x4209, 0x420A};
    const uint8_t values[] = {0x34, 0x01, 0x56, 0x01};
    const char* names[] = {"HTIMEL", "HTIMEH", "VTIMEL", "VTIMEH"};
    for (int i = 0; i < 4; ++i) {
        bus.write(regs[i], values[i]);
        const uint64_t next = bus.irqConfigGeneration();
        expect(next == generation + 1, names[i]);
        generation = next;
    }
    bus.write(0x4200, 0x30);
    expect(bus.irqConfigGeneration() == generation + 1,
           "NMITIMEN IRQ mode change also invalidates the scheduled comparator");
    CPU cpu(bus);
    bus.attachCPU(&cpu);
    cpu.reset();
    bus.updateVideoTiming(225, 0, false, true);
    bus.triggerVBlankNmiEdge();
    expect((bus.read(0x4210) & 0x80) != 0, "RDNMI reports the VBlank NMI source");
    expect((bus.read(0x4210) & 0x80) == 0, "RDNMI read acknowledges only the source latch");
    cpu.setStateForTesting(cpu.state());
    bus.write(0x4200, 0x00);
    bus.triggerVBlankNmiEdge();
    expect(!cpu.nmiPending(), "disabled NMI does not latch a CPU NMI request");
    bus.write(0x4200, 0x80);
    expect(cpu.nmiPending(), "enabling NMI while RDNMI is set requests NMI immediately");
    (void)bus.read(0x4210);
    cpu.setStateForTesting(cpu.state());
    bus.write(0x4200, 0x00);
    bus.write(0x4200, 0x80);
    expect(!cpu.nmiPending(), "re-enabling NMI after acknowledging RDNMI does not retrigger old VBlank");
    bus.write(0x4200, 0x10);
    bus.setMasterClock(100);
    bus.triggerTimerIrq();
    expect(!cpu.irqLine(), "TIMEUP rises one circuit tick before the CPU IRQ line");
    bus.setMasterClock(104);
    bus.serviceTimerIrqLine();
    expect(cpu.irqLine(), "timer IRQ reaches the CPU line four master clocks after TIMEUP");
    expect((bus.read(0x4211) & 0x80) != 0, "TIMEUP reports internal timer IRQ");
    expect(!cpu.irqLine(), "TIMEUP read acknowledges internal timer IRQ");
    expect((bus.read(0x4211) & 0x80) == 0, "acknowledged timer IRQ no longer appears in TIMEUP");
    bus.setCartridgeIrq(true);
    expect(cpu.irqLine(), "external cartridge IRQ asserts CPU IRQ line");
    expect((bus.read(0x4211) & 0x80) != 0, "TIMEUP reports external cartridge IRQ");
    expect(cpu.irqLine(), "TIMEUP read does not acknowledge external cartridge IRQ");
    expect((bus.read(0x4211) & 0x80) != 0, "external cartridge IRQ remains visible across TIMEUP reads");
    bus.setCartridgeIrq(false);
    expect(!cpu.irqLine(), "clearing external cartridge IRQ releases CPU IRQ line");
    bus.setMasterClock(200);
    bus.triggerTimerIrq();
    bus.setMasterClock(204);
    bus.setCartridgeIrq(true);
    expect((bus.read(0x4211) & 0x80) != 0, "TIMEUP reports combined timer and cartridge IRQ");
    expect(cpu.irqLine(), "acknowledging timer IRQ leaves cartridge IRQ asserted");
    bus.setCartridgeIrq(false);
    expect(!cpu.irqLine(), "combined IRQ clears after timer acknowledgement and external release");
    bus.setCartridgeIrq(false);
    bus.write(0x4200, 0x10);
    bus.setMasterClock(1000);
    bus.triggerTimerIrq();
    expect((bus.read(0x4211) & 0x80) != 0, "TIMEUP is visible at the comparator edge");
    expect(!cpu.irqLine(), "CPU IRQ line has not risen on the TIMEUP clock");
    bus.setMasterClock(1003);
    expect((bus.read(0x4211) & 0x80) != 0, "TIMEUP remains visible inside four-clock comparator window");
    expect(!cpu.irqLine(), "CPU IRQ line remains low inside the TIMEUP hold window");
    bus.setMasterClock(1004);
    bus.serviceTimerIrqLine();
    expect(cpu.irqLine(), "CPU IRQ line rises when the four-clock propagation delay expires");
    expect((bus.read(0x4211) & 0x80) != 0, "TIMEUP remains latched when comparator window ends");
    expect(!cpu.irqLine(), "TIMEUP read after comparator window acknowledges timer IRQ");
    expect((bus.read(0x4211) & 0x80) == 0, "acknowledged TIMEUP stays clear after comparator window");
    bus.write(0x4200, 0x10);
    bus.setMasterClock(2000);
    bus.triggerTimerIrq();
    bus.setMasterClock(2002);
    bus.write(0x4200, 0x00);
    expect((bus.read(0x4211) & 0x80) != 0,
           "$4200 disable inside propagation window cannot acknowledge TIMEUP");
    expect(!cpu.irqLine(), "committed timer IRQ has not reached CPU before its propagation edge");
    bus.setMasterClock(2004);
    bus.serviceTimerIrqLine();
    expect(cpu.irqLine(), "$4200 disable cannot cancel an already-committed timer IRQ edge");
    expect((bus.read(0x4211) & 0x80) != 0, "committed edge keeps TIMEUP visible until acknowledged");
    expect(!cpu.irqLine(), "TIMEUP read acknowledges committed timer IRQ after it reaches CPU");
    bus.write(0x4200, 0x10);
    bus.setMasterClock(2100);
    bus.write(0x4200, 0x00);
    bus.setMasterClock(2104);
    bus.serviceTimerIrqLine();
    expect(!cpu.irqLine(), "$4200 disable before a timer edge leaves no pending IRQ");
    {
        SNES snes;
        snes.reset();
        snes.bus().write(0x4209, 0x01);
        snes.bus().write(0x420A, 0x00);
        while (snes.ppu().scanline() < 1) snes.runCycles(4);
        snes.runCycles(200);
        snes.bus().write(0x4200, 0x20);
        snes.runCycles(5);
        expect((snes.bus().read(0x4211) & 0x80) == 0,
               "mid-line V-IRQ enable does not set TIMEUP before two circuit ticks");
        snes.runCycles(1);
        expect((snes.bus().read(0x4211) & 0x80) != 0,
               "mid-line V-IRQ enable fires when V already matches VTIME");
    }
    {
        SNES snes;
        snes.reset();
        snes.bus().write(0x4209, 0xC8);
        snes.bus().write(0x420A, 0x00);
        snes.bus().write(0x4200, 0x20);
        while (snes.ppu().scanline() < 5) snes.runCycles(4);
        snes.runCycles(201);
        expect((snes.bus().read(0x4211) & 0x80) == 0, "nonmatching VTIME remains quiet");
        snes.bus().write(0x4209, 0x05);
        snes.runCycles(1);
        expect((snes.bus().read(0x4211) & 0x80) != 0,
               "VTIME rewrite to current line re-evaluates comparator at write time");
    }
    if (failures) {
        std::cerr << failures << " IRQ timer reschedule regression(s) failed\n";
        return 1;
    }
    std::cout << "IRQ timer register reschedule regression passed\n";
    return 0;
}

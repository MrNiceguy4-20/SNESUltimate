#include "snes.hpp"
#include "bus.hpp"
#include "dma.hpp"
#include "cpu.hpp"
#include <cstdint>
#include <iostream>
namespace {
int failures = 0;
void expect(bool c, const char* m) {
    if (!c) { std::cerr << "FAIL: " << m << '\n'; ++failures; }
}
}
int main() {
    SNES snes;
    snes.reset();
    auto& bus = snes.bus();
    auto& dma = snes.dma();
    bus.write(0x7E0000, 0x01);
    bus.write(0x7E0001, 0x00);
    bus.write(0x7E0002, 0x00);
    bus.write(0x7E0003, 0x00);
    bus.write(0x7E0004, 0x00);
    bus.write(0x7E0005, 0x00);
    bus.write(0x4300, 0x04);
    bus.write(0x4301, 0x20);
    bus.write(0x4302, 0x00);
    bus.write(0x4303, 0x00);
    bus.write(0x4304, 0x7E);
    bus.write(0x420C, 0x01);
    bus.write(0x4207, 0x16);
    bus.write(0x4208, 0x01);
    bus.write(0x4200, 0x10);
    snes.runCycles(1125);
    expect((bus.read(0x4211) & 0x80) == 0,
           "TIMEUP remains clear one master clock before the H=278 timer edge");
    snes.runCycles(1);
    expect((bus.read(0x4211) & 0x80) != 0,
           "TIMEUP rises at H=278+14 while HDMA owns the bus");
    expect(!snes.cpu().irqLine(),
           "CPU-facing IRQ remains low during the four-clock propagation interval");
    expect(dma.hdmaActive(),
           "HDMA remains active while the timer IRQ circuit propagates independently");
    snes.runCycles(4);
    expect(snes.cpu().irqLine(),
           "timer IRQ reaches CPU even though CPU cannot execute until HDMA releases the bus");
    (void)bus.read(0x4211);
    expect(!snes.cpu().irqLine(), "TIMEUP acknowledgement releases timer IRQ after hold interval");
    SNES late;
    late.reset();
    auto& lbus = late.bus();
    lbus.write(0x4207, 0x16);
    lbus.write(0x4208, 0x01);
    lbus.write(0x4200, 0x10);
    late.runCycles(1123);
    lbus.write(0x4200, 0x00);
    late.runCycles(3);
    expect((lbus.read(0x4211) & 0x80) != 0,
           "late $4200 disable cannot cancel a comparator edge already in propagation");
    late.runCycles(4);
    expect(late.cpu().irqLine(),
           "committed comparator edge reaches /IRQ after H/V IRQ is disabled");
    (void)lbus.read(0x4211);
    if (failures) {
        std::cerr << failures << " IRQ/HDMA phase regression(s) failed\n";
        return 1;
    }
    std::cout << "IRQ/HDMA phase regression passed\n";
    return 0;
}

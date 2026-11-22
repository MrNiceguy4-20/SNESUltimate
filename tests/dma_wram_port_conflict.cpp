#include "bus.hpp"
#include "dma.hpp"
#include <cassert>
#include <iostream>
static void program(DMA& dma, bool bToA, uint16_t a, uint8_t bank, uint16_t size) {
    dma.writeRegister(0x4300, bToA ? 0x80 : 0x00);
    dma.writeRegister(0x4301, 0x80);
    dma.writeRegister(0x4302, static_cast<uint8_t>(a));
    dma.writeRegister(0x4303, static_cast<uint8_t>(a >> 8));
    dma.writeRegister(0x4304, bank);
    dma.writeRegister(0x4305, static_cast<uint8_t>(size));
    dma.writeRegister(0x4306, static_cast<uint8_t>(size >> 8));
}
static void run(DMA& dma) {
    dma.startDMA(1, 0);
    while (dma.dmaActive()) dma.stepDMA();
}
int main() {
    Bus bus;
    DMA dma;
    bus.attachDMA(&dma);
    dma.attachBus(&bus);
    bus.reset();
    dma.reset();
    bus.write(0x2181, 0x00);
    bus.write(0x2182, 0x08);
    bus.write(0x2183, 0x00);
    bus.write(0x7E0800, 0x55);
    bus.write(0x7E1000, 0xAA);
    program(dma, false, 0x1000, 0x7E, 1);
    run(dma);
    bus.write(0x2180, 0x3F);
    assert(bus.read(0x7E0800) == 0x3F);
    assert(bus.read(0x7E0801) == 0x00);
    bus.write(0x2181, 0x00);
    bus.write(0x2182, 0x08);
    bus.write(0x2183, 0x00);
    bus.write(0x7E1000, 0xAA);
    program(dma, true, 0x1000, 0x7E, 1);
    run(dma);
    assert(bus.read(0x7E1000) != 0xAA);
    bus.write(0x2180, 0x7C);
    assert(bus.read(0x7E0800) == 0x7C);
    std::cout << "DMA WRAM/$2180 conflict accuracy ok\n";
    return 0;
}

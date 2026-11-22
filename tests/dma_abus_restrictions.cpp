#include "bus.hpp"
#include "controller.hpp"
#include "dma.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
static void run(DMA& dma) {
    dma.startDMA(1, 0);
    while (dma.dmaActive()) dma.stepDMA();
}
static void program(DMA& dma, bool bToA, uint32_t aAddress, uint8_t bbad) {
    dma.writeRegister(0x4300, bToA ? 0x80 : 0x00);
    dma.writeRegister(0x4301, bbad);
    dma.writeRegister(0x4302, static_cast<uint8_t>(aAddress));
    dma.writeRegister(0x4303, static_cast<uint8_t>(aAddress >> 8));
    dma.writeRegister(0x4304, static_cast<uint8_t>(aAddress >> 16));
    dma.writeRegister(0x4305, 1);
    dma.writeRegister(0x4306, 0);
}
int main() {
    Bus bus;
    DMA dma;
    Controller pad;
    bus.attachDMA(&dma);
    dma.attachBus(&bus);
    bus.attachController1(&pad);
    bus.reset();
    dma.reset();
    pad.setButton(Controller::B, true);
    pad.setButton(Controller::Y, false);
    bus.write(0x4016, 1);
    bus.write(0x4016, 0);
    program(dma, false, 0x004016, 0x80);
    run(dma);
    assert((bus.read(0x4016) & 1) == 1);
    bus.write(0x4200, 0x00);
    bus.write(0x2181, 0x00);
    bus.write(0x2182, 0x00);
    bus.write(0x2183, 0x00);
    bus.write(0x7E0000, 0xFF);
    program(dma, true, 0x004200, 0x80);
    run(dma);
    assert(bus.nmitimen() == 0x00);
    bus.write(0x2181, 0x00);
    bus.write(0x2182, 0x00);
    bus.write(0x2183, 0x00);
    bus.write(0x7E0000, 0x5A);
    program(dma, true, 0x004301, 0x80);
    const uint8_t before = dma.readRegister(0x4301);
    run(dma);
    assert(dma.readRegister(0x4301) == before);
    dma.reset();
    bus.write(0x4200, 0x10);
    bus.setMasterClock(100);
    bus.triggerTimerIrq();
    bus.setMasterClock(200);
    bus.write(0x7E0000, 0x01);
    dma.writeRegister(0x4300, 0x00);
    dma.writeRegister(0x4301, 0x00);
    dma.writeRegister(0x4302, 0x11);
    dma.writeRegister(0x4303, 0x42);
    dma.writeRegister(0x4304, 0x00);
    dma.setHDMAEnable(0x01);
    dma.startHDMAInit();
    while (dma.hdmaActive()) dma.stepHDMA();
    assert((bus.read(0x4211) & 0x80) != 0);
    std::cout << "DMA/HDMA A-bus MMIO restriction accuracy ok\n";
    return 0;
}

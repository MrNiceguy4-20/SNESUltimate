#include "bus.hpp"
#include "dma.hpp"
#include "ppu.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
namespace {
uint64_t runOneByte(uint8_t resumeSpeed, uint64_t startClock = 0) {
    PPU ppu;
    Bus bus;
    DMA dma;
    bus.attachPPU(&ppu);
    bus.attachDMA(&dma);
    dma.attachBus(&bus);
    bus.write(0x7E0000, 0x0A);
    dma.writeRegister(0x4300, 0x00);
    dma.writeRegister(0x4301, 0x00);
    dma.writeRegister(0x4302, 0x00);
    dma.writeRegister(0x4303, 0x00);
    dma.writeRegister(0x4304, 0x7E);
    dma.writeRegister(0x4305, 0x01);
    dma.writeRegister(0x4306, 0x00);
    dma.startDMA(0x01, startClock, resumeSpeed);
    while (dma.dmaActive()) dma.stepDMA();
    assert(ppu.brightness() == 0x0A);
    return dma.lastDMAMasterClocks();
}
}
int main() {
    assert(runOneByte(6) == 36);
    assert(runOneByte(8) == 40);
    assert(runOneByte(12) == 36);
    assert(runOneByte(6, 3) == 30);
    std::cout << "CPU-speed-aware DMA end synchronization regression passed\n";
    return 0;
}

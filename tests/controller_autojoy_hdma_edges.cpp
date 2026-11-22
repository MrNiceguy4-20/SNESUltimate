#include "bus.hpp"
#include "controller.hpp"
#include "dma.hpp"
#include "ppu.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
int main() {
    {
        Bus bus;
        PPU ppu;
        Controller c1;
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        bus.attachController1(&c1);
        c1.setButton(Controller::B, true);
        c1.setButton(Controller::Start, true);
        c1.setButton(Controller::Right, true);
        c1.setButton(Controller::A, true);
        c1.setButton(Controller::R, true);
        bus.write(0x4200, 0x01);
        while (!(ppu.scanline() == ppu.vblankStartLine() && ppu.dot() == 0)) ppu.tick();
        bus.updateVideoTiming(ppu.scanline(), ppu.dot(), ppu.hblank(), true);
        while (ppu.dot() < 33) {
            ppu.tick();
            bus.updateVideoTiming(ppu.scanline(), ppu.dot(), ppu.hblank(), true);
        }
        while ((bus.read(0x4212) & 0x01) != 0) {
            ppu.tick();
            bus.updateVideoTiming(ppu.scanline(), ppu.dot(), ppu.hblank(), ppu.vblank());
        }
        assert(bus.read(0x4218) == 0x90);
        assert(bus.read(0x4219) == 0x91);
    }
    {
        DMA dma;
        dma.writeRegister(0x430A, 0x80);
        dma.setHDMAEnable(0x01);
        assert(dma.readRegister(0x430A) == 0x80);
    }
    std::cout << "phase114 controller autojoy + HDMA $80 edges ok\n";
    return 0;
}

#include "bus.hpp"
#include "ppu.hpp"
#include <cassert>
#include <iostream>
int main() {
    Bus bus; PPU ppu;
    ppu.attachBus(&bus); bus.attachPPU(&ppu);
    ppu.power(); bus.reset();
    ppu.writeRegister(0x2100, 0x0f);
    ppu.writeRegister(0x2102, 0x20);
    ppu.writeRegister(0x2103, 0x00);
    ppu.writeRegister(0x2100, 0x80);
    (void)ppu.readRegister(0x2138);
    ppu.writeRegister(0x2100, 0x0f);
    (void)ppu.readRegister(0x2138);
    assert(ppu.internalOamByteAddress() == 0x41);
    while (ppu.scanline() != ppu.vblankStartLine() || ppu.dot() != 0) ppu.tick();
    assert(ppu.internalOamByteAddress() == 0x41);
    for (int i = 0; i < PPU::OAM_RESET_DOT; ++i) ppu.tick();
    assert(ppu.dot() == PPU::OAM_RESET_DOT);
    assert(ppu.internalOamByteAddress() == 0x41);
    ppu.tick();
    assert(ppu.internalOamByteAddress() == 0x40);
    ppu.writeRegister(0x2100, 0x80);
    (void)ppu.readRegister(0x2138);
    const auto held = ppu.internalOamByteAddress();
    while (!(ppu.scanline() == 0 && ppu.dot() == 0)) ppu.tick();
    while (ppu.scanline() != ppu.vblankStartLine() || ppu.dot() != PPU::OAM_RESET_DOT) ppu.tick();
    ppu.tick();
    assert(ppu.internalOamByteAddress() == held);
    std::cout << "phase14 VBlank OAM reset timing ok\n";
}

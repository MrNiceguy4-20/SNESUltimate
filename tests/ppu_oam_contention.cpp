#include "bus.hpp"
#include "ppu.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
int main() {
    Bus bus; PPU ppu;
    ppu.attachBus(&bus); bus.attachPPU(&ppu);
    ppu.power(); bus.reset();
    ppu.writeRegister(0x2100,0x80);
    ppu.writeRegister(0x2102,0x00);
    ppu.writeRegister(0x2103,0x00);
    ppu.writeRegister(0x2104,0x11);
    ppu.writeRegister(0x2104,0x22);
    assert(ppu.oamByte(0)==0x11 && ppu.oamByte(1)==0x22);
    ppu.writeRegister(0x2102,0x20);
    ppu.writeRegister(0x2103,0x00);
    assert(ppu.internalOamByteAddress()==0x40);
    ppu.writeRegister(0x2100,0x0f);
    ppu.tick();
    assert(ppu.activeOamByteAddress()==0x000);
    const uint8_t activeRead=ppu.readRegister(0x2138);
    assert(activeRead==0x11);
    assert(ppu.internalOamByteAddress()==0x41);
    const uint8_t oldLow=ppu.oamByte(0);
    ppu.writeRegister(0x2104,0x5a);
    assert(ppu.oamByte(0)==oldLow);
    assert(ppu.oamByte(0x200)==0x5a);
    assert(ppu.internalOamByteAddress()==0x42);
    ppu.writeRegister(0x2100,0x80);
    ppu.writeRegister(0x2102,0x14);
    ppu.writeRegister(0x2103,0x80);
    while (ppu.dot()!=0) ppu.tick();
    ppu.writeRegister(0x2100,0x0f);
    ppu.tick();
    assert(ppu.activeOamByteAddress()==(10u<<2));
    ppu.writeRegister(0x2102,0x40);
    ppu.writeRegister(0x2103,0x80);
    assert(ppu.activeOamByteAddress()==(10u<<2));
    std::cout << "phase6 active OAM contention accuracy ok\n";
    return 0;
}

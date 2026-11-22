#include "bus.hpp"
#include "ppu.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
int main() {
    Bus bus;
    PPU ppu;
    ppu.attachBus(&bus);
    bus.attachPPU(&ppu);
    ppu.writeRegister(0x2100, 0x80);
    ppu.writeRegister(0x2115, 0x80);
    ppu.writeRegister(0x2116, 0x00);
    ppu.writeRegister(0x2117, 0x00);
    ppu.writeRegister(0x2118, 0x34);
    ppu.writeRegister(0x2119, 0x12);
    ppu.writeRegister(0x2118, 0x78);
    ppu.writeRegister(0x2119, 0x56);
    ppu.writeRegister(0x2116, 0x00);
    ppu.writeRegister(0x2117, 0x00);
    assert(ppu.readRegister(0x2139) == 0x34);
    assert(ppu.readRegister(0x213A) == 0x12);
    assert(ppu.vramWordAddress() == 1);
    ppu.writeRegister(0x2100, 0x0F);
    const uint8_t before = ppu.vramByte(2);
    ppu.writeRegister(0x2116, 0x01);
    ppu.writeRegister(0x2117, 0x00);
    ppu.writeRegister(0x2118, 0xAA);
    assert(ppu.vramWordAddress() == 1);
    ppu.writeRegister(0x2119, 0xBB);
    assert(ppu.vramWordAddress() == 2);
    assert(ppu.vramByte(2) == before);
    ppu.writeRegister(0x2116, 0x03);
    ppu.writeRegister(0x2117, 0x00);
    assert(ppu.readRegister(0x213A) == 0x00);
    assert(ppu.vramWordAddress() == 4);
    ppu.writeRegister(0x2100, 0x80);
    ppu.writeRegister(0x2121, 0x00);
    ppu.writeRegister(0x2122, 0x1F);
    ppu.writeRegister(0x2122, 0x01);
    ppu.writeRegister(0x2121, 0x00);
    (void)bus.write(0x7E0000, 0x80);
    assert(ppu.readRegister(0x213B) == 0x1F);
    assert(ppu.readRegister(0x213B) == 0x01);
    std::cout << "phase37 PPU port accuracy ok\n";
    return 0;
}

#include "ppu.hpp"
#include <cstdint>
#include <iostream>
namespace {
int failures = 0;
void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}
uint32_t readProduct(PPU& ppu) {
    const uint32_t lo = ppu.readRegister(0x2134);
    const uint32_t mid = ppu.readRegister(0x2135);
    const uint32_t hi = ppu.readRegister(0x2136);
    return lo | (mid << 8) | (hi << 16);
}
void writeM7A(PPU& ppu, uint16_t value) {
    ppu.writeRegister(0x211B, static_cast<uint8_t>(value & 0xFF));
    ppu.writeRegister(0x211B, static_cast<uint8_t>(value >> 8));
}
void testSignedMode7Multiply() {
    PPU ppu;
    ppu.reset();
    writeM7A(ppu, 0x1234);
    ppu.writeRegister(0x211C, 0x03);
    expect(readProduct(ppu) == 0x00369Cu,
           "M7A 0x1234 * signed M7B 3 produces immediate 24-bit result");
    writeM7A(ppu, 0xFF00);
    ppu.writeRegister(0x211C, 0x02);
    expect(readProduct(ppu) == 0xFFFE00u,
           "negative signed M7A is sign-extended into 24-bit result");
    writeM7A(ppu, 0x0100);
    ppu.writeRegister(0x211C, 0xFF);
    expect(readProduct(ppu) == 0xFFFF00u,
           "M7B uses the signed last-written byte as the second factor");
}
void testSharedMode7Latch() {
    PPU ppu;
    ppu.reset();
    ppu.writeRegister(0x211C, 0x7F);
    ppu.writeRegister(0x211B, 0x01);
    ppu.writeRegister(0x211C, 0x02);
    expect(readProduct(ppu) == 0x0002FEu,
           "Mode-7 shared latch affects M7A exactly like the real register pair");
}
}
int main() {
    testSignedMode7Multiply();
    testSharedMode7Latch();
    if (failures) {
        std::cerr << failures << " Mode-7 multiplier regression(s) failed\n";
        return 1;
    }
    std::cout << "Mode-7 multiplier regression passed\n";
    return 0;
}

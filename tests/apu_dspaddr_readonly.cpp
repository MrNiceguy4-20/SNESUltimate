#include "apu.hpp"
#include <cstdint>
#include <cstdlib>
#include <iostream>
namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
}
int main() {
    APU apu;
    apu.reset();
    apu.write(0x00F2, 0x0C);
    apu.write(0x00F3, 0x35);
    require(apu.read(0x00F2) == 0x0C, "DSPADDR normal readback");
    require(apu.read(0x00F3) == 0x35, "DSPDATA normal readback");
    apu.write(0x00F2, 0x8C);
    require(apu.read(0x00F2) == 0x0C, "DSPADDR read-protect bit must read as zero");
    require(apu.read(0x00F3) == 0x35, "DSPDATA read remains selected with bit 7 set");
    apu.write(0x00F3, 0xA7);
    require(apu.read(0x00F3) == 0x35, "DSPDATA write must be inhibited by DSPADDR bit 7");
    apu.write(0x00F2, 0x0C);
    apu.write(0x00F3, 0xA7);
    require(apu.read(0x00F3) == 0xA7, "DSPDATA write re-enabled after clearing bit 7");
    apu.write(0x00F2, 0xFF);
    require(apu.read(0x00F2) == 0x7F, "DSPADDR low seven bits remain readable");
    std::cout << "PASS: S-SMP DSPADDR read-only flag accuracy\n";
    return 0;
}

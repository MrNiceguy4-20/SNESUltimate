#include "cpu.hpp"
#include "bus.hpp"
#include "cartridge.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
namespace {
std::vector<uint8_t> makeRom(uint8_t opcode) {
    std::vector<uint8_t> rom(0x10000, 0xEA);
    rom[0x0000] = opcode;
    rom[0x0001] = 0x00;
    rom[0x0002] = 0x00;
    rom[0x0003] = 0x00;
    constexpr std::size_t header = 0x7FC0;
    const char title[] = "CPU MICROCODE COVERAGE";
    for (std::size_t i = 0; i < 21; ++i) rom[header + i] = static_cast<uint8_t>(title[i]);
    rom[header + 0x15] = 0x20;
    rom[header + 0x17] = 0x06;
    rom[header + 0x19] = 0x01;
    auto vector = [&](std::size_t offset) {
        rom[offset] = 0x00;
        rom[offset + 1] = 0x80;
    };
    vector(0x7FE4); vector(0x7FE6); vector(0x7FE8); vector(0x7FEA); vector(0x7FEE);
    vector(0x7FF4); vector(0x7FF8); vector(0x7FFA); vector(0x7FFC); vector(0x7FFE);
    return rom;
}
void drain(CPU& cpu) {
    int guard = 10000;
    while (cpu.hasPendingCycles() && --guard > 0) cpu.completeCycle();
    assert(guard > 0);
}
}
int main() {
    std::array<bool, 256> covered{};
    std::array<bool, 256> fallback{};
    for (int op = 0; op < 256; ++op) {
        Cartridge cart;
        assert(cart.loadData(makeRom(static_cast<uint8_t>(op)), "cpu_microcode_coverage.sfc", "coverage"));
        Bus bus;
        CPU cpu;
        bus.attachCPU(&cpu);
        bus.attachCartridge(&cart);
        cpu.attachBus(&bus);
        bus.reset();
        cpu.reset();
        cpu.setCycleStepping(true);
        cpu.step();
        drain(cpu);
        covered[op] = true;
        fallback[op] = cpu.legacyFallbackCount() != 0;
    }
    int covered_count = 0;
    int fallback_count = 0;
    for (int op = 0; op < 256; ++op) {
        if (covered[op]) ++covered_count;
        if (fallback[op]) {
            ++fallback_count;
            std::printf("fallback opcode: %02X\n", op);
        }
    }
    assert(covered_count == 256);
    assert(fallback_count == 0);
    std::printf("cpu microcode coverage: %d/256, fallbacks=%d\n", covered_count, fallback_count);
}

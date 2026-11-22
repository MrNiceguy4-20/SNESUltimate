#include "superfx2.hpp"
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>
namespace {
int failures = 0;
void expect(bool c, const char* m) { if (!c) { std::cerr << "FAIL: " << m << '\n'; ++failures; } }
uint8_t sfrLo(CartridgeDevice& g) {
    uint8_t v = 0;
    expect(g.read(0x003030, v), "SFR low read routed");
    return v;
}
bool running(CartridgeDevice& g) { return (sfrLo(g) & 0x20u) != 0; }
void cacheProgram(CartridgeDevice& g, const std::vector<uint8_t>& bytes) {
    for (unsigned i = 0; i < 16; ++i) {
        const uint8_t v = i < bytes.size() ? bytes[i] : 0x01;
        expect(g.write(0x003100 + i, v), "cache program byte write routed");
    }
}
void setupLdw(CartridgeDevice& g) {
    cacheProgram(g, {0x41, 0x00});
    expect(g.write(0x003002, 0x20), "R1 low write routed");
    expect(g.write(0x003003, 0x00), "R1 high write routed");
    expect(g.write(0x006020, 0x34), "RAM low test byte routed");
    expect(g.write(0x006021, 0x12), "RAM high test byte routed");
    expect(g.write(0x00303a, 0x18), "RON/RAN grant routed");
    expect(g.write(0x00301e, 0x00), "R15 low write routed");
    expect(g.write(0x00301f, 0x00), "R15 high starts GSU");
}
uint64_t clocksToStop(CartridgeDevice& g, uint64_t limit = 1000) {
    uint64_t n = 0;
    while (running(g) && n < limit) { g.clock(1); ++n; }
    return n;
}
uint16_t r0(CartridgeDevice& g) {
    uint8_t lo = 0, hi = 0;
    expect(g.read(0x003000, lo) && g.read(0x003001, hi), "R0 read routed");
    return static_cast<uint16_t>(lo | (uint16_t(hi) << 8));
}
std::unique_ptr<CartridgeDevice> makeDevice() {
    std::vector<uint8_t> rom(2 * 1024 * 1024, 0x01);
    return createSuperFX2Device(rom, 128 * 1024);
}
void testRamGrantWaitResumesPartialInstruction() {
    auto baseline = makeDevice();
    setupLdw(*baseline);
    baseline->clock(4);
    const uint64_t baselineRemaining = clocksToStop(*baseline);
    expect(!running(*baseline), "baseline LDW program reaches STOP");
    expect(r0(*baseline) == 0x1234, "baseline LDW result is correct");
    auto interrupted = makeDevice();
    setupLdw(*interrupted);
    interrupted->clock(4);
    expect(interrupted->write(0x00303a, 0x10), "S-CPU revokes RAN while retaining RON");
    interrupted->clock(100);
    expect(running(*interrupted), "GSU remains in WAIT while required RAM grant is absent");
    expect(interrupted->write(0x00303a, 0x18), "S-CPU restores RAN");
    const uint64_t resumedRemaining = clocksToStop(*interrupted);
    expect(resumedRemaining == baselineRemaining,
           "restored RAN resumes the partially elapsed LDW instead of restarting its timing budget");
    expect(r0(*interrupted) == 0x1234, "resumed LDW commits the expected RAM value");
}
void testWaitStateCheckpointPreservesRemainingPhase() {
    auto original = makeDevice();
    setupLdw(*original);
    original->clock(6);
    expect(original->write(0x00303a, 0x10), "checkpoint setup revokes RAN");
    original->clock(25);
    const auto state = original->saveState();
    expect(original->write(0x00303a, 0x18), "original restores RAN");
    const uint64_t originalRemaining = clocksToStop(*original);
    auto restored = makeDevice();
    expect(restored->loadState(state), "SuperFX WAIT checkpoint restores");
    expect(restored->write(0x00303a, 0x18), "restored checkpoint grants RAN");
    const uint64_t restoredRemaining = clocksToStop(*restored);
    expect(restoredRemaining == originalRemaining,
           "checkpoint replay preserves the exact mid-instruction WAIT countdown");
    expect(r0(*restored) == 0x1234, "restored WAIT path finishes with the expected LDW result");
}
}
int main() {
    testRamGrantWaitResumesPartialInstruction();
    testWaitStateCheckpointPreservesRemainingPhase();
    if (failures) return 1;
    std::cout << "Phase45 SuperFX WAIT/resume timing accuracy ok\n";
    return 0;
}

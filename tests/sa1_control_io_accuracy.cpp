#include "sa1.hpp"
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>
namespace {
int failures = 0;
void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}
struct Fixture {
    std::vector<uint8_t> rom = std::vector<uint8_t>(2 * 1024 * 1024, 0xEA);
    std::vector<uint8_t> bwram = std::vector<uint8_t>(256 * 1024, 0);
    bool dirty = false;
    std::unique_ptr<CartridgeDevice> dev;
    void create(bool pal = false) {
        dev = createSA1Device(rom, bwram, dirty, pal);
        dev->reset();
    }
    void start(uint16_t pc = 0x8000) {
        dev->write(0x002203, static_cast<uint8_t>(pc));
        dev->write(0x002204, static_cast<uint8_t>(pc >> 8));
        dev->write(0x002200, 0x00);
    }
    uint8_t hostRead(uint32_t address) {
        uint8_t value = 0xCC;
        expect(dev->read(address, value), "SA-1 device owns expected host read");
        return value;
    }
};
void emitLdaSta(std::vector<uint8_t>& rom, size_t& p, uint8_t value, uint16_t address) {
    rom[p++] = 0xA9; rom[p++] = value;
    rom[p++] = 0x8D; rom[p++] = static_cast<uint8_t>(address);
    rom[p++] = static_cast<uint8_t>(address >> 8);
}
void testFastRomReadPausesOnlyCcpu() {
    auto make = []() {
        auto f = std::make_unique<Fixture>();
        size_t p = 0;
        emitLdaSta(f->rom, p, 0xFF, 0x222A);
        for (unsigned i = 1; i <= 8; ++i) emitLdaSta(f->rom, p, static_cast<uint8_t>(i), 0x3000);
        f->rom[p++] = 0xDB;
        f->create();
        f->start();
        return f;
    };
    auto control = make();
    auto fast = make();
    fast->dev->hostAccessBegin(0x808000, false, 6);
    bool diverged = false;
    unsigned divergenceClock = 0;
    for (unsigned clocks = 2; clocks <= 160; clocks += 2) {
        control->dev->clock(2);
        fast->dev->clock(2);
        if (control->hostRead(0x003000) != fast->hostRead(0x003000)) {
            diverged = true;
            divergenceClock = clocks;
            break;
        }
    }
    expect(diverged, "high-bank 6-clock host ROM read removes C-CPU execution time");
    if (diverged) {
        fast->dev->clock(6);
        expect(fast->hostRead(0x003000) >= control->hostRead(0x003000),
               "C-CPU catches up after the FastROM ownership interval is repaid");
    }
    auto slowControl = make();
    auto slowNotified = make();
    slowNotified->dev->hostAccessBegin(0x808000, false, 8);
    slowControl->dev->clock(120);
    slowNotified->dev->clock(120);
    expect(slowControl->hostRead(0x003000) == slowNotified->hostRead(0x003000),
           "8-clock SlowROM host read does not invoke the FastROM C-CPU stop rule");
    auto lowControl = make();
    auto lowFast = make();
    lowFast->dev->hostAccessBegin(0x008000, false, 6);
    lowControl->dev->clock(120);
    lowFast->dev->clock(120);
    expect(lowControl->hostRead(0x003000) == lowFast->hostRead(0x003000),
           "low-bank ROM access does not invoke high-bank FastROM stopping");
    (void)divergenceClock;
}
void testPalHvTimerFrameLength() {
    auto prepare = [](Fixture& f, bool pal) {
        size_t p = 0;
        emitLdaSta(f.rom, p, 0xFF, 0x222A);
        f.rom[p++] = 0xAD; f.rom[p++] = 0x04; f.rom[p++] = 0x23;
        f.rom[p++] = 0x8D; f.rom[p++] = 0x00; f.rom[p++] = 0x30;
        f.rom[p++] = 0xDB;
        f.create(pal);
        f.start();
    };
    Fixture ntsc, pal;
    prepare(ntsc, false);
    prepare(pal, true);
    const uint64_t clocks = 4ull * 341ull * 300ull;
    ntsc.dev->clock(clocks);
    pal.dev->clock(clocks);
    expect(ntsc.hostRead(0x003000) == static_cast<uint8_t>(300 - 262),
           "NTSC SA-1 H/V timer wraps after 262 lines");
    expect(pal.hostRead(0x003000) == static_cast<uint8_t>(300),
           "PAL SA-1 H/V timer retains 312-line frame domain");
}
void testVariableBitHardwareProjectionAndReset() {
    Fixture f;
    f.rom[0x1000] = 0xB3;
    f.rom[0x1001] = 0x6C;
    f.rom[0x1002] = 0xA5;
    f.rom[0x1003] = 0x5A;
    size_t p = 0;
    emitLdaSta(f.rom, p, 0xFF, 0x222A);
    emitLdaSta(f.rom, p, 0x00, 0x2259);
    emitLdaSta(f.rom, p, 0x90, 0x225A);
    emitLdaSta(f.rom, p, 0x00, 0x225B);
    emitLdaSta(f.rom, p, 0x84, 0x2258);
    f.rom[p++] = 0xAD; f.rom[p++] = 0x0C; f.rom[p++] = 0x23;
    f.rom[p++] = 0x8D; f.rom[p++] = 0x00; f.rom[p++] = 0x30;
    f.rom[p++] = 0xAD; f.rom[p++] = 0x0D; f.rom[p++] = 0x23;
    f.rom[p++] = 0x8D; f.rom[p++] = 0x01; f.rom[p++] = 0x30;
    f.rom[p++] = 0xAD; f.rom[p++] = 0x0C; f.rom[p++] = 0x23;
    f.rom[p++] = 0x8D; f.rom[p++] = 0x02; f.rom[p++] = 0x30;
    emitLdaSta(f.rom, p, 0x01, 0x2259);
    f.rom[p++] = 0xAD; f.rom[p++] = 0x0C; f.rom[p++] = 0x23;
    f.rom[p++] = 0x8D; f.rom[p++] = 0x03; f.rom[p++] = 0x30;
    f.rom[p++] = 0xDB;
    f.create();
    f.start();
    f.dev->clock(1400);
    expect(f.hostRead(0x003000) == 0xB3 && f.hostRead(0x003001) == 0x6C,
           "automatic VBR exposes the selected even-aligned ROM word before advancing");
    expect(f.hostRead(0x003002) == 0xCB,
           "$230D automatic mode advances the 16-bit VBR shifter by four bits");
    expect(f.hostRead(0x003003) == 0xB3,
           "VDA writes clear VBD/VBIT and the VBR ROM interface discards address bit A0");
}
void testSignedDivisionRemainder() {
    Fixture f;
    size_t p = 0;
    emitLdaSta(f.rom, p, 0xFF, 0x222A);
    emitLdaSta(f.rom, p, 0x01, 0x2250);
    emitLdaSta(f.rom, p, 0xFB, 0x2251);
    emitLdaSta(f.rom, p, 0xFF, 0x2252);
    emitLdaSta(f.rom, p, 0x03, 0x2253);
    emitLdaSta(f.rom, p, 0x00, 0x2254);
    for (unsigned i = 0; i < 8; ++i) f.rom[p++] = 0xEA;
    for (uint16_t a = 0x2306, d = 0x3000; a <= 0x2309; ++a, ++d) {
        f.rom[p++] = 0xAD; f.rom[p++] = static_cast<uint8_t>(a); f.rom[p++] = static_cast<uint8_t>(a >> 8);
        f.rom[p++] = 0x8D; f.rom[p++] = static_cast<uint8_t>(d); f.rom[p++] = static_cast<uint8_t>(d >> 8);
    }
    f.rom[p++] = 0xDB;
    f.create();
    f.start();
    for (unsigned i = 0; i < 700; ++i) f.dev->clock(2);
    expect(f.hostRead(0x003000) == 0xFE && f.hostRead(0x003001) == 0xFF,
           "SA-1 signed division floors negative dividends to quotient -2 for -5/3");
    expect(f.hostRead(0x003002) == 0x01 && f.hostRead(0x003003) == 0x00,
           "SA-1 signed division returns a non-negative remainder for -5/3");
}
void testCumulativeMathSignExtension() {
    Fixture f;
    size_t p = 0;
    emitLdaSta(f.rom, p, 0xFF, 0x222A);
    emitLdaSta(f.rom, p, 0x02, 0x2250);
    emitLdaSta(f.rom, p, 0xFF, 0x2251);
    emitLdaSta(f.rom, p, 0xFF, 0x2252);
    emitLdaSta(f.rom, p, 0x01, 0x2253);
    emitLdaSta(f.rom, p, 0x00, 0x2254);
    for (unsigned i = 0; i < 8; ++i) f.rom[p++] = 0xEA;
    emitLdaSta(f.rom, p, 0xFF, 0x2251);
    emitLdaSta(f.rom, p, 0xFF, 0x2252);
    emitLdaSta(f.rom, p, 0x01, 0x2253);
    emitLdaSta(f.rom, p, 0x00, 0x2254);
    for (unsigned i = 0; i < 8; ++i) f.rom[p++] = 0xEA;
    for (uint16_t a = 0x2306, d = 0x3000; a <= 0x230A; ++a, ++d) {
        f.rom[p++] = 0xAD; f.rom[p++] = static_cast<uint8_t>(a); f.rom[p++] = static_cast<uint8_t>(a >> 8);
        f.rom[p++] = 0x8D; f.rom[p++] = static_cast<uint8_t>(d); f.rom[p++] = static_cast<uint8_t>(d >> 8);
    }
    f.rom[p++] = 0xDB;
    f.create();
    f.start();
    for (unsigned i = 0; i < 900; ++i) f.dev->clock(2);
    expect(f.hostRead(0x003000) == 0xFE &&
           f.hostRead(0x003001) == 0xFF &&
           f.hostRead(0x003002) == 0xFF &&
           f.hostRead(0x003003) == 0xFF &&
           f.hostRead(0x003004) == 0xFF,
           "successive negative SA-1 MAC operations preserve signed 40-bit accumulator state");
}
void testFastRomPauseCheckpoint() {
    Fixture f;
    f.create();
    f.start();
    f.dev->hostAccessBegin(0x808000, false, 6);
    const auto pending = f.dev->saveState();
    expect(!pending.empty(), "SA-1 serializes an in-flight FastROM C-CPU pause");
    Fixture clone;
    clone.create();
    expect(clone.dev->loadState(pending), "second SA-1 instance restores in-flight FastROM pause state");
    f.dev->clock(4);
    clone.dev->clock(4);
    expect(f.dev->saveState() == clone.dev->saveState(),
           "restored FastROM pause consumes the same clocks deterministically");
}
unsigned completionClockForTransfer(bool absoluteJump, bool oddTarget) {
    for (unsigned clocks = 2; clocks <= 80; clocks += 2) {
        Fixture f;
        const uint16_t target = oddTarget ? 0x8009 : 0x8008;
        size_t p = 0;
        if (absoluteJump) {
            f.rom[p++] = 0x4C;
            f.rom[p++] = static_cast<uint8_t>(target);
            f.rom[p++] = static_cast<uint8_t>(target >> 8);
        } else {
            f.rom[p++] = 0x80;
            f.rom[p++] = static_cast<uint8_t>(target - 0x8002);
        }
        while (p < static_cast<size_t>(target - 0x8000)) f.rom[p++] = 0xEA;
        emitLdaSta(f.rom, p, 0xFF, 0x222A);
        emitLdaSta(f.rom, p, 0x5A, 0x3007);
        f.rom[p++] = 0xDB;
        f.create();
        f.start();
        f.dev->clock(clocks);
        if (f.hostRead(0x003007) == 0x5A) return clocks;
    }
    return 0;
}
void testSa1ControlTransferWaitCycles() {
    const unsigned braEven = completionClockForTransfer(false, false);
    const unsigned braOdd = completionClockForTransfer(false, true);
    const unsigned jmpEven = completionClockForTransfer(true, false);
    expect(braEven != 0 && braOdd != 0 && jmpEven != 0,
           "SA-1 control-transfer timing probes reach their completion marker");
    expect(braOdd == braEven + 4,
           "odd-target BRA pays both the SA-1 branch wait and an unpaired ROM-word start");
    expect(jmpEven == braEven + 2,
           "absolute JMP abandons the prefetched ROM high byte in addition to its control-transfer wait");
}
unsigned completionClockForRomWordRead(bool oddAddress) {
    for (unsigned clocks = 2; clocks <= 120; clocks += 2) {
        Fixture f;
        f.rom[0x10000] = 0x34;
        f.rom[0x10001] = 0x12;
        f.rom[0x10002] = 0x56;
        size_t p = 0;
        emitLdaSta(f.rom, p, 0xFF, 0x222A);
        f.rom[p++] = 0x18;
        f.rom[p++] = 0xFB;
        f.rom[p++] = 0xC2; f.rom[p++] = 0x20;
        f.rom[p++] = 0xAF;
        const uint32_t source = 0xC10000u + (oddAddress ? 1u : 0u);
        f.rom[p++] = static_cast<uint8_t>(source);
        f.rom[p++] = static_cast<uint8_t>(source >> 8);
        f.rom[p++] = static_cast<uint8_t>(source >> 16);
        f.rom[p++] = 0xE2; f.rom[p++] = 0x20;
        emitLdaSta(f.rom, p, 0x5A, 0x3008);
        f.rom[p++] = 0xDB;
        f.create();
        f.start();
        f.dev->clock(clocks);
        if (f.hostRead(0x003008) == 0x5A) return clocks;
    }
    return 0;
}
void testRomWordPairingTiming() {
    const unsigned aligned = completionClockForRomWordRead(false);
    const unsigned unaligned = completionClockForRomWordRead(true);
    expect(aligned != 0 && unaligned != 0,
           "SA-1 ROM word-pair timing probes reach completion");
    expect(unaligned == aligned + 4,
           "unaligned 16-bit ROM read consumes two physical ROM words and costs two extra SA-1 cycles");
}
unsigned completionClockForWordStore(bool bwramTarget) {
    for (unsigned clocks = 2; clocks <= 140; clocks += 2) {
        Fixture f;
        size_t p = 0;
        emitLdaSta(f.rom, p, 0xFF, 0x222A);
        emitLdaSta(f.rom, p, 0x80, 0x2227);
        f.rom[p++] = 0x18;
        f.rom[p++] = 0xFB;
        f.rom[p++] = 0xC2; f.rom[p++] = 0x20;
        f.rom[p++] = 0xA9; f.rom[p++] = 0x34; f.rom[p++] = 0x12;
        f.rom[p++] = 0x8D;
        const uint16_t target = bwramTarget ? 0x6000u : 0x3010u;
        f.rom[p++] = static_cast<uint8_t>(target);
        f.rom[p++] = static_cast<uint8_t>(target >> 8);
        f.rom[p++] = 0xE2; f.rom[p++] = 0x20;
        emitLdaSta(f.rom, p, 0x5A, 0x3009);
        f.rom[p++] = 0xDB;
        f.create();
        f.start();
        f.dev->clock(clocks);
        if (f.hostRead(0x003009) == 0x5A) return clocks;
    }
    return 0;
}
void testBwramWriteBufferTiming() {
    const unsigned iram = completionClockForWordStore(false);
    const unsigned bwram = completionClockForWordStore(true);
    expect(iram != 0 && bwram != 0,
           "SA-1 BW-RAM write-buffer timing probes reach completion");
    expect(bwram == iram + 2,
           "16-bit BW-RAM store stalls only the second byte while the one-entry write buffer is full");
}
}
void testCcNtInterruptFlagsLatchUntilCic() {
    Fixture f;
    std::size_t p = 0;
    emitLdaSta(f.rom, p, 0xFF, 0x222A);
    f.rom[p++] = 0xAD; f.rom[p++] = 0x01; f.rom[p++] = 0x23;
    f.rom[p++] = 0x29; f.rom[p++] = 0x80;
    f.rom[p++] = 0x8D; f.rom[p++] = 0x0A; f.rom[p++] = 0x30;
    emitLdaSta(f.rom, p, 0x80, 0x220B);
    f.rom[p++] = 0xAD; f.rom[p++] = 0x01; f.rom[p++] = 0x23;
    f.rom[p++] = 0x29; f.rom[p++] = 0x80;
    f.rom[p++] = 0x8D; f.rom[p++] = 0x0B; f.rom[p++] = 0x30;
    f.rom[p++] = 0xDB;
    f.create();
    f.start();
    f.dev->write(0x002200, 0x80);
    f.dev->write(0x002200, 0x00);
    f.dev->clock(400);
    expect(f.hostRead(0x00300A) == 0x80,
           "CCNT IRQ request remains latched after a later CCNT write clears bit 7");
    expect(f.hostRead(0x00300B) == 0x00,
           "CIC bit 7 clears the latched SA-1 IRQ request flag");
}
int main() {
    testFastRomReadPausesOnlyCcpu();
    testPalHvTimerFrameLength();
    testVariableBitHardwareProjectionAndReset();
    testSignedDivisionRemainder();
    testCumulativeMathSignExtension();
    testFastRomPauseCheckpoint();
    testSa1ControlTransferWaitCycles();
    testRomWordPairingTiming();
    testBwramWriteBufferTiming();
    testCcNtInterruptFlagsLatchUntilCic();
    if (failures) {
        std::cerr << failures << " SA-1 control/I-O regression(s) failed\n";
        return 1;
    }
    std::cout << "SA-1 control/I-O accuracy regression passed\n";
    return 0;
}

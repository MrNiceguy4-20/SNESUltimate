#include "sa1.hpp"
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>
namespace {
int failures = 0;
void expect(bool c, const char* m) { if (!c) { std::cerr << "FAIL: " << m << '\n'; ++failures; } }
struct Fixture {
    std::vector<uint8_t> rom = std::vector<uint8_t>(2 * 1024 * 1024, 0xEA);
    std::vector<uint8_t> bwram = std::vector<uint8_t>(256 * 1024, 0);
    bool dirty = false;
    std::unique_ptr<CartridgeDevice> dev;
    void create() { dev = createSA1Device(rom, bwram, dirty); dev->reset(); }
    void start() {
        dev->write(0x002203, 0x00); dev->write(0x002204, 0x80); dev->write(0x002200, 0x00);
    }
    uint8_t read(uint32_t a) { uint8_t v = 0; expect(dev->read(a, v), "host probe is mapped"); return v; }
};
void emit(Fixture& f) {
    std::size_t p = 0;
    f.rom[p++] = 0xA9; f.rom[p++] = 0xFF;
    f.rom[p++] = 0x8D; f.rom[p++] = 0x2A; f.rom[p++] = 0x22;
    f.rom[p++] = 0xEA;
    f.rom[p++] = 0xEA;
    f.rom[p++] = 0xA9; f.rom[p++] = 0x5A;
    f.rom[p++] = 0x8D; f.rom[p++] = 0x01; f.rom[p++] = 0x30;
    f.rom[p++] = 0xDB;
    f.create();
    f.start();
}
void testTwoRomStealsAccumulate() {
    Fixture f; emit(f);
    unsigned base = 0;
    for (; base < 80 && f.read(0x003001) != 0x5A; ++base) f.dev->clock(1);
    expect(base < 80, "baseline probe completes");
    Fixture q; emit(q);
    (void)q.read(0x008000);
    (void)q.read(0x008001);
    q.dev->clock(base + 4);
    expect(q.read(0x003001) == 0x00, "two ROM ownership steals do not collapse to one slot");
    q.dev->clock(4);
    expect(q.read(0x003001) == 0x5A, "two ROM ownership steals cost two 5.37-MHz slots");
}
void testBurstBeyondLegacyFifteenSlotCeiling() {
    auto makeLong = []() {
        Fixture f;
        std::size_t p = 0;
        f.rom[p++] = 0xA9; f.rom[p++] = 0xFF;
        f.rom[p++] = 0x8D; f.rom[p++] = 0x2A; f.rom[p++] = 0x22;
        for (unsigned i = 0; i < 48; ++i) f.rom[p++] = 0xEA;
        f.rom[p++] = 0xA9; f.rom[p++] = 0x5A;
        f.rom[p++] = 0x8D; f.rom[p++] = 0x01; f.rom[p++] = 0x30;
        f.rom[p++] = 0xDB;
        f.create(); f.start();
        return f;
    };
    auto q = makeLong();
    for (unsigned i = 0; i < 20; ++i) (void)q.read(0x008000 + i);
    q.dev->clock(270);
    expect(q.read(0x003001) == 0x00,
           "SA-1 preserves host contention beyond the former fifteen-slot ceiling");
    q.dev->clock(20);
    expect(q.read(0x003001) == 0x5A,
           "all twenty queued ROM ownership slots are eventually consumed");
}
void testBurstBeyondByteCounterCeiling() {
    auto makeLong = []() {
        Fixture f;
        std::size_t p = 0;
        f.rom[p++] = 0xA9; f.rom[p++] = 0xFF;
        f.rom[p++] = 0x8D; f.rom[p++] = 0x2A; f.rom[p++] = 0x22;
        for (unsigned i = 0; i < 700; ++i) f.rom[p++] = 0xEA;
        f.rom[p++] = 0xA9; f.rom[p++] = 0x5A;
        f.rom[p++] = 0x8D; f.rom[p++] = 0x01; f.rom[p++] = 0x30;
        f.rom[p++] = 0xDB;
        f.create(); f.start();
        return f;
    };
    auto baseProbe = makeLong();
    unsigned base = 0;
    while (base < 5000 && baseProbe.read(0x003001) != 0x5A) {
        baseProbe.dev->clock(2);
        base += 2;
    }
    expect(base < 5000, "long SA-1 baseline completes");
    auto q = makeLong();
    for (unsigned i = 0; i < 300; ++i) (void)q.read(0x008000 + (i & 0x7fff));
    q.dev->clock(base + 255u * 4u);
    expect(q.read(0x003001) == 0x00,
           "SA-1 contention debt does not saturate at an eight-bit 255-slot ceiling");
    q.dev->clock(45u * 4u + 8u);
    expect(q.read(0x003001) == 0x5A,
           "all three hundred queued ROM ownership losses are eventually charged");
}
void testQueuedSlotsCheckpoint() {
    Fixture f; emit(f);
    (void)f.read(0x008000);
    (void)f.read(0x008001);
    const auto state = f.dev->saveState();
    expect(!state.empty(), "multi-slot arbitration state saves");
    f.dev->clock(4);
    expect(f.dev->loadState(state), "multi-slot arbitration state restores");
    const auto replay = f.dev->saveState();
    expect(replay == state, "queued ownership slots replay byte-exactly");
}
}
int main() {
    testTwoRomStealsAccumulate();
    testBurstBeyondLegacyFifteenSlotCeiling();
    testBurstBeyondByteCounterCeiling();
    testQueuedSlotsCheckpoint();
    if (failures) return 1;
    std::cout << "SA-1 arbitration queue accuracy regression passed\n";
    return 0;
}

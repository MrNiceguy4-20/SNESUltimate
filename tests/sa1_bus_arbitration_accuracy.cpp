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
    void create() {
        dev = createSA1Device(rom, bwram, dirty);
        dev->reset();
    }
    void start(uint16_t pc = 0x8000) {
        dev->write(0x002203, static_cast<uint8_t>(pc));
        dev->write(0x002204, static_cast<uint8_t>(pc >> 8));
        dev->write(0x002200, 0x00);
    }
    uint8_t hostRead(uint32_t address) {
        uint8_t value = 0xCC;
        expect(dev->read(address, value), "SA-1 owns expected S-CPU read");
        return value;
    }
};
void emitLdaImmStaAbs(std::vector<uint8_t>& rom, std::size_t& p, uint8_t value, uint16_t address) {
    rom[p++] = 0xA9; rom[p++] = value;
    rom[p++] = 0x8D; rom[p++] = static_cast<uint8_t>(address); rom[p++] = static_cast<uint8_t>(address >> 8);
}
enum class Resource { Rom, Iram, Bwram };
uint32_t hostProbe(Resource resource) {
    switch (resource) {
    case Resource::Rom: return 0x008000;
    case Resource::Iram: return 0x003000;
    case Resource::Bwram: return 0x400000;
    }
    return 0;
}
void buildProbeProgram(Fixture& f, Resource resource);
unsigned baselineCompletionClocks(Resource resource) {
    for (unsigned clocks = 2; clocks <= 96; clocks += 2) {
        Fixture f;
        buildProbeProgram(f, resource);
        f.dev->clock(clocks);
        if (f.hostRead(0x003001) == 0x5A) return clocks;
    }
    return 0;
}
void buildProbeProgram(Fixture& f, Resource resource) {
    std::size_t p = 0;
    emitLdaImmStaAbs(f.rom, p, 0xFF, 0x222A);
    switch (resource) {
    case Resource::Rom:
        f.rom[p++] = 0xAD; f.rom[p++] = 0x00; f.rom[p++] = 0x80;
        break;
    case Resource::Iram:
        f.rom[p++] = 0xAD; f.rom[p++] = 0x00; f.rom[p++] = 0x30;
        break;
    case Resource::Bwram:
        f.rom[p++] = 0xAD; f.rom[p++] = 0x00; f.rom[p++] = 0x60;
        break;
    }
    emitLdaImmStaAbs(f.rom, p, 0x5A, 0x3001);
    f.rom[p++] = 0xDB;
    f.create();
    f.start();
}
void testResource(Resource resource, const char* name) {
    const unsigned base = baselineCompletionClocks(resource);
    expect(base != 0, "baseline SA-1 arbitration probe reaches completion");
    {
        Fixture f;
        buildProbeProgram(f, resource);
        f.dev->clock(base);
        expect(f.hostRead(0x003001) == 0x5A, "baseline SA-1 probe reaches completion marker");
    }
    {
        Fixture f;
        buildProbeProgram(f, resource);
        (void)f.hostRead(hostProbe(resource));
        f.dev->clock(base);
        expect(f.hostRead(0x003001) == 0x00, name);
    }
    {
        Fixture f;
        buildProbeProgram(f, resource);
        (void)f.hostRead(hostProbe(resource));
        f.dev->clock(base + 4);
        expect(f.hostRead(0x003001) == 0x5A, "one shared-bus collision costs exactly one 5.37-MHz slot");
    }
}
void testIndependentResources() {
    Fixture f;
    buildProbeProgram(f, Resource::Rom);
    (void)f.hostRead(0x003000);
    (void)f.hostRead(0x400000);
    f.dev->clock(baselineCompletionClocks(Resource::Rom));
    expect(f.hostRead(0x003001) == 0x5A,
           "non-ROM host ownership does not delay a C-CPU ROM access");
}
void testPendingCollisionCheckpoint() {
    Fixture f;
    buildProbeProgram(f, Resource::Rom);
    (void)f.hostRead(0x008000);
    const auto state = f.dev->saveState();
    expect(!state.empty(), "SA-1 arbitration state is checkpointable");
    const unsigned base = baselineCompletionClocks(Resource::Rom);
    f.dev->clock(base);
    expect(f.hostRead(0x003001) == 0x00,
           "pending ROM collision survives until the matching C-CPU access");
    expect(f.dev->loadState(state), "SA-1 arbitration checkpoint restores");
    f.dev->clock(base);
    expect(f.hostRead(0x003001) == 0x00,
           "restored pending ROM collision reproduces the same delay");
    f.dev->clock(4);
    expect(f.hostRead(0x003001) == 0x5A,
           "restored arbitration state releases after one ownership slot");
}
}
int main() {
    testResource(Resource::Rom, "S-CPU ROM ownership delays the next conflicting C-CPU ROM access");
    testResource(Resource::Iram, "S-CPU I-RAM ownership delays the next conflicting C-CPU I-RAM access");
    testResource(Resource::Bwram, "S-CPU BW-RAM ownership delays the next conflicting C-CPU BW-RAM access");
    testIndependentResources();
    testPendingCollisionCheckpoint();
    if (failures) {
        std::cerr << failures << " SA-1 bus-arbitration regression(s) failed\n";
        return 1;
    }
    std::cout << "SA-1 bus arbitration accuracy regression passed\n";
    return 0;
}

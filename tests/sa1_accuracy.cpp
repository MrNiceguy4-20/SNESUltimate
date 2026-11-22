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
    uint8_t hostRead(uint32_t a) {
        uint8_t v = 0xCC;
        expect(dev->read(a, v), "SA-1 device owns expected host read");
        return v;
    }
};
void emitLdaSta(std::vector<uint8_t>& rom, size_t& p, uint8_t value, uint16_t address) {
    rom[p++] = 0xA9; rom[p++] = value;
    rom[p++] = 0x8D; rom[p++] = uint8_t(address);
    rom[p++] = uint8_t(address >> 8);
}
void testDeferredTimerMaterializesAtObservableBoundaries() {
    Fixture f;
    size_t p = 0;
    emitLdaSta(f.rom, p, 0xFF, 0x222A);
    f.rom[p++] = 0xAD; f.rom[p++] = 0x02; f.rom[p++] = 0x23;
    f.rom[p++] = 0x8D; f.rom[p++] = 0x00; f.rom[p++] = 0x30;
    f.rom[p++] = 0xAD; f.rom[p++] = 0x04; f.rom[p++] = 0x23;
    f.rom[p++] = 0x8D; f.rom[p++] = 0x01; f.rom[p++] = 0x30;
    f.rom[p++] = 0xDB;
    f.create();
    f.start();
    f.dev->clock(4u * 350u);
    expect(f.hostRead(0x003000) == 9,
           "disabled SA-1 timer materializes exact H counter on C-CPU read");
    expect(f.hostRead(0x003001) == 1,
           "disabled SA-1 timer materializes exact V counter on C-CPU read");
}
void testBwramProtectionRequiresBothDisabled() {
    Fixture f;
    size_t p = 0;
    emitLdaSta(f.rom, p, 0x80, 0x2227);
    emitLdaSta(f.rom, p, 0x5A, 0x6000);
    f.rom[p++] = 0xDB;
    f.create();
    f.start();
    f.dev->clock(200);
    expect(f.hostRead(0x400000) == 0x5A, "C-CPU CBWE permits C-CPU BW-RAM write in protected area");
    f.dev->write(0x400000, 0xA5);
    expect(f.hostRead(0x400000) == 0xA5, "protection is lifted for S-CPU writes while either write enable is set");
    Fixture g;
    size_t q = 0;
    g.rom[q++] = 0xDB;
    g.create();
    g.dev->write(0x400000, 0xA5);
    expect(g.hostRead(0x400000) == 0x00, "protected BW-RAM area blocks writes when both write enables are clear");
}
void testClockedNormalDma() {
    Fixture f;
    f.rom[0x100] = 0x11; f.rom[0x101] = 0x22; f.rom[0x102] = 0x33; f.rom[0x103] = 0x44;
    size_t p = 0;
    emitLdaSta(f.rom, p, 0x00, 0x2232);
    emitLdaSta(f.rom, p, 0x81, 0x2233);
    emitLdaSta(f.rom, p, 0x00, 0x2234);
    emitLdaSta(f.rom, p, 0x04, 0x2238);
    emitLdaSta(f.rom, p, 0x00, 0x2239);
    emitLdaSta(f.rom, p, 0x80, 0x2230);
    emitLdaSta(f.rom, p, 0x00, 0x2235);
    emitLdaSta(f.rom, p, 0x00, 0x2236);
    f.rom[p++] = 0xDB;
    f.create();
    f.start();
    f.dev->clock(200);
    expect(f.hostRead(0x003000) == 0x00, "normal DMA is not an instantaneous register-write memcpy");
    f.dev->clock(4);
    expect(f.hostRead(0x003000) == 0x00, "S-CPU I-RAM access delays an active ROM-to-I-RAM DMA transfer");
    f.dev->clock(6);
    expect(f.hostRead(0x003000) == 0x11, "ROM-to-I-RAM DMA resumes after shared I-RAM contention clears");
    expect(f.hostRead(0x003001) == 0x00, "clocked DMA has not completed entire transfer immediately");
    f.dev->clock(100);
    expect(f.hostRead(0x003000) == 0x11 && f.hostRead(0x003001) == 0x22 &&
           f.hostRead(0x003002) == 0x33 && f.hostRead(0x003003) == 0x44,
           "clocked normal DMA completes ROM to I-RAM transfer");
}
void testCharacterConversion2RowSequencer() {
    Fixture f;
    size_t p = 0;
    emitLdaSta(f.rom, p, 0xA0, 0x2230);
    emitLdaSta(f.rom, p, 0x02, 0x2231);
    emitLdaSta(f.rom, p, 0x00, 0x2235);
    emitLdaSta(f.rom, p, 0x00, 0x2236);
    for (unsigned x = 0; x < 8; ++x)
        emitLdaSta(f.rom, p, static_cast<uint8_t>(x & 3), static_cast<uint16_t>(0x2240 + x));
    for (unsigned x = 0; x < 8; ++x)
        emitLdaSta(f.rom, p, 0x01, static_cast<uint16_t>(0x2248 + x));
    f.rom[p++] = 0xDB;
    f.create();
    f.start();
    f.dev->clock(4000);
    expect(f.hostRead(0x003000) == 0x55 && f.hostRead(0x003001) == 0x33,
           "CC2 $2247 commit generates planar row 0 immediately");
    expect(f.hostRead(0x003002) == 0xFF && f.hostRead(0x003003) == 0x00,
           "CC2 $224F commit generates planar row 1 from the upper BRF half");
    expect(f.hostRead(0x003004) == 0x00 && f.hostRead(0x003005) == 0x00,
           "CC2 does not incorrectly batch four 16-byte BRF fills into a tile");
}
void testCharacterConversion2LineReset() {
    Fixture f;
    size_t p = 0;
    emitLdaSta(f.rom, p, 0xA0, 0x2230);
    emitLdaSta(f.rom, p, 0x02, 0x2231);
    emitLdaSta(f.rom, p, 0x00, 0x2235);
    emitLdaSta(f.rom, p, 0x00, 0x2236);
    for (unsigned x = 0; x < 8; ++x)
        emitLdaSta(f.rom, p, 0x01, static_cast<uint16_t>(0x2240 + x));
    emitLdaSta(f.rom, p, 0x00, 0x2230);
    emitLdaSta(f.rom, p, 0xA0, 0x2230);
    for (unsigned x = 0; x < 8; ++x)
        emitLdaSta(f.rom, p, 0x02, static_cast<uint16_t>(0x2240 + x));
    f.rom[p++] = 0xDB;
    f.create();
    f.start();
    f.dev->clock(5000);
    expect(f.hostRead(0x003000) == 0x00 && f.hostRead(0x003001) == 0xFF,
           "disabling SA-1 DMA resets the CC2 0..15 line sequencer");
    expect(f.hostRead(0x003002) == 0x00 && f.hostRead(0x003003) == 0x00,
           "CC2 reset does not continue at the stale next row");
}
void testCharacterConversion1AndHostIrq() {
    Fixture f;
    f.create();
    f.dev->write(0x002226, 0x80);
    f.dev->write(0x400000, 0xE4);
    f.dev->write(0x400001, 0xE4);
    for (uint32_t i = 2; i < 16; ++i) f.dev->write(0x400000 + i, 0x00);
    f.dev->write(0x002201, 0x20);
    size_t p = 0;
    emitLdaSta(f.rom, p, 0xB0, 0x2230);
    emitLdaSta(f.rom, p, 0x02, 0x2231);
    emitLdaSta(f.rom, p, 0x00, 0x2232);
    emitLdaSta(f.rom, p, 0x00, 0x2233);
    emitLdaSta(f.rom, p, 0x00, 0x2234);
    emitLdaSta(f.rom, p, 0xFF, 0x222A);
    emitLdaSta(f.rom, p, 0x00, 0x2235);
    emitLdaSta(f.rom, p, 0x00, 0x2236);
    f.rom[p++] = 0xAD; f.rom[p++] = 0x01; f.rom[p++] = 0x23;
    f.rom[p++] = 0x8D; f.rom[p++] = 0x10; f.rom[p++] = 0x30;
    f.rom[p++] = 0xDB;
    f.start();
    f.dev->clock(1000);
    expect((f.hostRead(0x003010) & 0x20) == 0,
           "CC1 does not spuriously raise the SA-1 CPU normal-DMA IRQ flag");
    expect(f.hostRead(0x003000) == 0x00 && f.hostRead(0x003001) == 0x00,
           "CC1 does not eagerly precompute the I-RAM conversion buffer");
    expect(f.hostRead(0x400000) == 0x55 && f.hostRead(0x400001) == 0x33,
           "CC1 converts one packed BW-RAM character on demand as the S-CPU drains it");
    expect(f.hostRead(0x003000) == 0x55 && f.hostRead(0x003001) == 0x33,
           "CC1 on-demand conversion materializes the planar character in I-RAM");
    f.dev->write(0x002231, 0x82);
    expect(f.hostRead(0x400000) == 0xE4, "CDMA bit 7 ends CC1 and restores raw BW-RAM reads");
    expect(f.dev->hostIrqPending(), "CC1 completion asserts host IRQ when SIE bit 5 is enabled");
    expect((f.hostRead(0x002300) & 0x20) != 0, "SFR exposes Character Conversion DMA flag separately");
    f.dev->write(0x002202, 0x20);
    expect(!f.dev->hostIrqPending(), "SIC bit 5 clears Character Conversion DMA host IRQ");
}
void testArithmeticLatencyAndCheckpoint() {
    Fixture f;
    size_t p = 0;
    emitLdaSta(f.rom, p, 0xFF, 0x222A);
    emitLdaSta(f.rom, p, 0x00, 0x2250);
    emitLdaSta(f.rom, p, 0x03, 0x2251);
    emitLdaSta(f.rom, p, 0x00, 0x2252);
    emitLdaSta(f.rom, p, 0x04, 0x2253);
    emitLdaSta(f.rom, p, 0x00, 0x2254);
    const size_t loop = p;
    f.rom[p++] = 0xAD; f.rom[p++] = 0x06; f.rom[p++] = 0x23;
    f.rom[p++] = 0xC9; f.rom[p++] = 0x0C;
    f.rom[p++] = 0xD0; f.rom[p++] = static_cast<uint8_t>(loop - (p + 1));
    f.rom[p++] = 0x8D; f.rom[p++] = 0x10; f.rom[p++] = 0x30;
    f.rom[p++] = 0xDB;
    f.create();
    f.start();
    f.dev->clock(200);
    expect(f.hostRead(0x003010) == 0x0C,
           "SA-1 arithmetic retires on the C-CPU timeline inside one host slice");
    auto completed = f.dev->saveState();
    expect(!completed.empty(), "SA-1 exposes deterministic checkpoint state");
    expect(f.dev->loadState(completed), "SA-1 restores completed arithmetic state");
    expect(f.hostRead(0x003010) == 0x0C,
           "SA-1 checkpoint preserves arithmetic result and C-CPU-visible state");
}
void testBmapByteAndBitmapProjection() {
    Fixture f;
    size_t p = 0;
    emitLdaSta(f.rom, p, 0x80, 0x2227);
    emitLdaSta(f.rom, p, 0xFF, 0x222A);
    emitLdaSta(f.rom, p, 0x01, 0x2225);
    emitLdaSta(f.rom, p, 0x5A, 0x6000);
    emitLdaSta(f.rom, p, 0x00, 0x223F);
    emitLdaSta(f.rom, p, 0x80, 0x2225);
    emitLdaSta(f.rom, p, 0x0A, 0x6001);
    f.rom[p++] = 0xAD; f.rom[p++] = 0x01; f.rom[p++] = 0x60;
    f.rom[p++] = 0x8D; f.rom[p++] = 0x20; f.rom[p++] = 0x30;
    f.rom[p++] = 0xDB;
    f.create();
    f.start();
    f.dev->clock(2000);
    expect(f.hostRead(0x402000) == 0x5A,
           "BMAP D7=0 maps the C-CPU $6000 window as ordinary 8 KiB BW-RAM blocks");
    expect((f.hostRead(0x400000) & 0xF0) == 0xA0,
           "BMAP D7=1 maps the C-CPU $6000 window through 4bpp virtual bitmap packing");
    expect(f.hostRead(0x003020) == 0x0A,
           "BMAP virtual projection reads the selected bitmap pixel rather than raw BW-RAM bytes");
}
void testDmaBusOwnershipStopsCcpuWhenRequired() {
    Fixture f;
    f.rom[0x100] = 0x5A;
    size_t p = 0;
    emitLdaSta(f.rom, p, 0xFF, 0x222A);
    emitLdaSta(f.rom, p, 0x00, 0x2232); emitLdaSta(f.rom, p, 0x81, 0x2233); emitLdaSta(f.rom, p, 0x00, 0x2234);
    emitLdaSta(f.rom, p, 0x01, 0x2238); emitLdaSta(f.rom, p, 0x00, 0x2239);
    emitLdaSta(f.rom, p, 0x80, 0x2230);
    emitLdaSta(f.rom, p, 0x00, 0x2235); emitLdaSta(f.rom, p, 0x00, 0x2236);
    emitLdaSta(f.rom, p, 0xA5, 0x3011);
    f.rom[p++] = 0xDB;
    f.create(); f.start(); f.dev->clock(300);
    expect(f.hostRead(0x003011) == 0x00, "ROM DMA stalls C-CPU even when explicit DMA priority is clear");
    f.dev->clock(8);
    expect(f.hostRead(0x003000) == 0x5A, "ROM DMA completes while C-CPU is bus-stalled");
    expect(f.hostRead(0x003011) == 0xA5, "C-CPU resumes after non-parallel DMA releases the bus");
}
}
int main() {
    testDeferredTimerMaterializesAtObservableBoundaries();
    testBwramProtectionRequiresBothDisabled();
    testClockedNormalDma();
    testCharacterConversion2RowSequencer();
    testCharacterConversion2LineReset();
    testCharacterConversion1AndHostIrq();
    testArithmeticLatencyAndCheckpoint();
    testBmapByteAndBitmapProjection();
    testDmaBusOwnershipStopsCcpuWhenRequired();
    if (failures) {
        std::cerr << failures << " SA-1 regression(s) failed\n";
        return 1;
    }
    std::cout << "SA-1 accuracy regression passed\n";
    return 0;
}

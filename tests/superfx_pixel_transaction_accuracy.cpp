#include "superfx.hpp"
#include "superfx2.hpp"
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
void writeReg(CartridgeDevice& g, unsigned r, uint16_t value) {
    expect(g.write(0x003000 + r * 2u, static_cast<uint8_t>(value)), "GSU register low write routed");
    expect(g.write(0x003001 + r * 2u, static_cast<uint8_t>(value >> 8)), "GSU register high write routed");
}
uint16_t readReg(CartridgeDevice& g, unsigned r) {
    uint8_t lo = 0, hi = 0;
    expect(g.read(0x003000 + r * 2u, lo), "GSU register low read routed");
    expect(g.read(0x003001 + r * 2u, hi), "GSU register high read routed");
    return static_cast<uint16_t>(lo | (uint16_t(hi) << 8));
}
bool running(CartridgeDevice& g) {
    uint8_t sfr = 0;
    expect(g.read(0x003030, sfr), "SFR low read routed");
    return (sfr & 0x20u) != 0;
}
void cacheProgram(CartridgeDevice& g, const std::vector<uint8_t>& bytes) {
    for (unsigned i = 0; i < 16; ++i) {
        const uint8_t value = i < bytes.size() ? bytes[i] : 0x01;
        expect(g.write(0x003100 + i, value), "cache byte write routed");
    }
}
void start(CartridgeDevice& g) {
    expect(g.write(0x00303a, 0x18), "RON/RAN grant configured");
    writeReg(g, 15, 0x0000);
}
std::unique_ptr<CartridgeDevice> makeGsu2(const std::vector<uint8_t>& rom) {
    return createSuperFX2Device(rom, 128 * 1024);
}
void enterTwoPlaneRpix(CartridgeDevice& g) {
    g.clock(2);
    g.clock(2);
    g.clock(2);
}
void completeOneSlowPixelTransaction(CartridgeDevice& g) {
    g.clock(2);
    g.clock(2);
    g.clock(2);
}
void testRpixSamplesEachPlaneAtItsOwnTransactionBoundary() {
    std::vector<uint8_t> rom(2 * 1024 * 1024, 0x01);
    auto device = makeGsu2(rom);
    auto& g = *device;
    cacheProgram(g, {0x3d, 0x4c, 0x00});
    writeReg(g, 0, 0x5555);
    writeReg(g, 1, 0x0000);
    writeReg(g, 2, 0x0000);
    expect(g.write(0x700000, 0x80), "plane 0 source byte initialized");
    expect(g.write(0x700001, 0x80), "plane 1 source byte initialized");
    start(g);
    enterTwoPlaneRpix(g);
    completeOneSlowPixelTransaction(g);
    expect(readReg(g, 0) == 0x5555, "RPIX has not retired after only plane 0 transaction");
    expect(g.write(0x00303a, 0x10), "RAN withdrawn between RPIX plane reads");
    expect(g.write(0x700000, 0x00), "CPU changes already-sampled plane 0 byte");
    expect(g.write(0x00303a, 0x18), "RAN restored for plane 1");
    completeOneSlowPixelTransaction(g);
    expect(readReg(g, 0) == 0x0003,
           "RPIX result preserves plane 0 sampled before the CPU RAM write");
}
void testRpixWaitsAtTheActualRamPort() {
    std::vector<uint8_t> rom(2 * 1024 * 1024, 0x01);
    auto device = makeGsu2(rom);
    auto& g = *device;
    cacheProgram(g, {0x3d, 0x4c, 0x00});
    writeReg(g, 0, 0x7777);
    writeReg(g, 1, 0x0000);
    writeReg(g, 2, 0x0000);
    expect(g.write(0x700000, 0x80), "wait test plane 0 initialized");
    expect(g.write(0x700001, 0x80), "wait test plane 1 initialized");
    start(g);
    enterTwoPlaneRpix(g);
    expect(g.write(0x00303a, 0x10), "RAN withdrawn before first RPIX RAM access");
    g.clock(20);
    expect(running(g), "RPIX remains running while a completed transaction waits for RAN");
    expect(readReg(g, 0) == 0x7777, "RPIX destination remains unchanged during RAN wait");
    expect(g.write(0x700000, 0x00), "CPU changes RAM while it owns the bus");
    expect(g.write(0x00303a, 0x18), "RAN restored for waiting RPIX transaction");
    g.clock(2);
    completeOneSlowPixelTransaction(g);
    expect(readReg(g, 0) == 0x0002,
           "waiting RPIX samples plane 0 only when the RAM grant actually returns");
}
void testPartialPlotFlushSeparatesReadAndWriteTransactions() {
    std::vector<uint8_t> rom(2 * 1024 * 1024, 0x01);
    auto device = makeGsu2(rom);
    auto& g = *device;
    cacheProgram(g, {0x4e, 0x4c, 0xa1, 0x08, 0x4c, 0xa1, 0x10, 0x4c, 0x00});
    writeReg(g, 0, 0x0001);
    writeReg(g, 1, 0x0000);
    writeReg(g, 2, 0x0000);
    expect(g.write(0x700000, 0x01), "partial-flush plane 0 old RAM byte initialized");
    expect(g.write(0x700001, 0x00), "partial-flush plane 1 old RAM byte initialized");
    start(g);
    g.clock(2);
    g.clock(2);
    g.clock(2);
    g.clock(2);
    g.clock(2);
    g.clock(2);
    g.clock(2);
    g.clock(2);
    g.clock(2);
    completeOneSlowPixelTransaction(g);
    uint8_t plane0 = 0;
    expect(g.read(0x700000, plane0) && plane0 == 0x01,
           "partial PLOT merge-read does not write RAM in the read transaction");
    expect(g.write(0x00303a, 0x10), "RAN withdrawn between partial flush read and write");
    expect(g.write(0x700000, 0x02), "CPU updates untouched plane bit between GSU RMW transactions");
    expect(g.write(0x00303a, 0x18), "RAN restored for partial flush write");
    completeOneSlowPixelTransaction(g);
    expect(g.read(0x700000, plane0) && plane0 == 0x81,
           "partial PLOT write uses the value latched by its preceding read transaction");
}
void testPixelTrafficDoesNotOvertakeBufferedStore() {
    std::vector<uint8_t> rom(2 * 1024 * 1024, 0x01);
    auto device = makeGsu2(rom);
    auto& g = *device;
    cacheProgram(g, {0x33, 0x3d, 0x4c, 0x00});
    writeReg(g, 0, 0x8080);
    writeReg(g, 1, 0x0000);
    writeReg(g, 2, 0x0000);
    writeReg(g, 3, 0x0000);
    start(g);
    unsigned guard = 0;
    while (running(g) && guard++ < 256) g.clock(1);
    expect(!running(g), "store->RPIX transaction sequence reaches STOP");
    expect(readReg(g, 0) == 0x0003,
           "RPIX observes both bytes of the older buffered store before its plane reads");
}
void testSaveStatePreservesInFlightRpixTransactions() {
    std::vector<uint8_t> rom(2 * 1024 * 1024, 0x01);
    auto source = makeGsu2(rom);
    auto& g = *source;
    cacheProgram(g, {0x3d, 0x4c, 0x00});
    writeReg(g, 0, 0x2468);
    writeReg(g, 1, 0x0000);
    writeReg(g, 2, 0x0000);
    expect(g.write(0x700000, 0x80), "save-state plane 0 initialized");
    expect(g.write(0x700001, 0x80), "save-state plane 1 initialized");
    start(g);
    enterTwoPlaneRpix(g);
    completeOneSlowPixelTransaction(g);
    const auto state = g.saveState();
    auto restored = makeGsu2(rom);
    expect(restored->loadState(state), "mid-RPIX SuperFX state restores");
    expect(restored->write(0x00303a, 0x10), "restored state releases RAM to CPU");
    expect(restored->write(0x700000, 0x00), "restored RAM changes after plane 0 was sampled");
    expect(restored->write(0x00303a, 0x18), "restored RAN returned to GSU");
    completeOneSlowPixelTransaction(*restored);
    expect(readReg(*restored, 0) == 0x0003,
           "save state retains already-sampled RPIX plane and remaining transaction queue");
}
void runUntilStopped(CartridgeDevice& g, unsigned limit = 4096) {
    unsigned guard = 0;
    while (running(g) && guard++ < limit) g.clock(1);
    expect(!running(g), "focused GSU program reaches STOP");
}
void restartAtZero(CartridgeDevice& g, uint8_t scmr) {
    expect(g.write(0x00303a, scmr), "focused test SCMR grant/mode write routed");
    writeReg(g, 15, 0x0000);
}
void testPlotUsesUnditheredColrForTransparency() {
    std::vector<uint8_t> rom(2 * 1024 * 1024, 0x01);
    auto device = makeGsu2(rom);
    auto& g = *device;
    cacheProgram(g, {0x4e, 0x4c, 0x00});
    expect(g.write(0x700000, 0x80), "COLR=04 test seeds plane 0");
    expect(g.write(0x700001, 0x80), "COLR=04 test seeds plane 1");
    writeReg(g, 0, 0x0004);
    writeReg(g, 1, 0x0000);
    writeReg(g, 2, 0x0000);
    start(g);
    runUntilStopped(g);
    uint8_t p0 = 0xff, p1 = 0xff;
    expect(g.read(0x700000, p0) && g.read(0x700001, p1), "COLR=04 result planes readable");
    expect((p0 & 0x80u) == 0 && (p1 & 0x80u) == 0,
           "2bpp COLR=04 is not transparent and writes zero to the two active planes");
    cacheProgram(g, {0x3d, 0x4e, 0x00});
    writeReg(g, 0, 0x0002);
    restartAtZero(g, 0x18);
    runUntilStopped(g);
    cacheProgram(g, {0x4e, 0x4c, 0x4c, 0x00});
    writeReg(g, 0, 0x00f0);
    writeReg(g, 1, 0x0000);
    writeReg(g, 2, 0x0000);
    restartAtZero(g, 0x19);
    runUntilStopped(g);
    uint8_t d0=0xff,d1=0xff,d2=0xff,d3=0xff;
    expect(g.read(0x700000,d0) && g.read(0x700001,d1) && g.read(0x700010,d2) && g.read(0x700011,d3),
           "dither transparency result planes readable");
    expect(((d0 | d1 | d2 | d3) & 0xc0u) == 0,
           "COLR=F0 is suppressed for both dither phases from undithered COLR low nibble");
}
void testColorHighNibblePreservesPreviousColrHighNibble() {
    std::vector<uint8_t> rom(2 * 1024 * 1024, 0x01);
    auto device = makeGsu2(rom);
    auto& g = *device;
    cacheProgram(g, {0x4e, 0x00});
    writeReg(g, 0, 0x0050);
    start(g);
    runUntilStopped(g);
    cacheProgram(g, {0x3d, 0x4e, 0x00});
    writeReg(g, 0, 0x0004);
    restartAtZero(g, 0x18);
    runUntilStopped(g);
    cacheProgram(g, {0x4e, 0x4c, 0x00});
    writeReg(g, 0, 0x00a0);
    writeReg(g, 1, 0x0000);
    writeReg(g, 2, 0x0000);
    restartAtZero(g, 0x1b);
    runUntilStopped(g);
    uint8_t p4=0,p5=0,p6=0,p7=0;
    expect(g.read(0x700020,p4) && g.read(0x700021,p5) && g.read(0x700030,p6) && g.read(0x700031,p7),
           "HN result high planes readable");
    expect((p4 & 0x80u) != 0 && (p5 & 0x80u) == 0 && (p6 & 0x80u) != 0 && (p7 & 0x80u) == 0,
           "HN transforms old COLR 0x50 plus source 0xA0 into COLR 0x5A");
}
void testCfgrMasksStopIrqGeneration() {
    std::vector<uint8_t> rom(2 * 1024 * 1024, 0x01);
    auto device = makeGsu2(rom);
    auto& g = *device;
    cacheProgram(g, {0x00});
    expect(g.write(0x003037, 0x80), "CFGR IRQ mask write routed");
    start(g);
    runUntilStopped(g);
    uint8_t sfrHi = 0xff;
    expect(g.read(0x003031, sfrHi), "masked STOP SFR high readable");
    expect((sfrHi & 0x80u) == 0, "CFGR.7 prevents STOP from creating latent SFR.IRQ");
}
}
int main() {
    testRpixSamplesEachPlaneAtItsOwnTransactionBoundary();
    testRpixWaitsAtTheActualRamPort();
    testPartialPlotFlushSeparatesReadAndWriteTransactions();
    testPixelTrafficDoesNotOvertakeBufferedStore();
    testSaveStatePreservesInFlightRpixTransactions();
    testPlotUsesUnditheredColrForTransparency();
    testColorHighNibblePreservesPreviousColrHighNibble();
    testCfgrMasksStopIrqGeneration();
    if (failures) return 1;
    std::cout << "SuperFX per-transaction pixel bus accuracy ok\n";
    return 0;
}

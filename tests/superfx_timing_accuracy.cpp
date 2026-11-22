#include "superfx.hpp"
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
bool running(CartridgeDevice& g) { return (sfrLo(g) & 0x20) != 0; }
void writeReg(CartridgeDevice& g, unsigned r, uint16_t value) {
    expect(g.write(0x003000 + r * 2u, static_cast<uint8_t>(value)), "GSU register low write routed");
    expect(g.write(0x003001 + r * 2u, static_cast<uint8_t>(value >> 8)), "GSU register high write routed");
}
void cacheProgram(CartridgeDevice& g, const std::vector<uint8_t>& bytes) {
    for (unsigned i = 0; i < 16; ++i) {
        const uint8_t v = i < bytes.size() ? bytes[i] : 0x01;
        expect(g.write(0x003100 + i, v), "cache byte write routed");
    }
}
void start(CartridgeDevice& g, uint16_t pc = 0) {
    expect(g.write(0x00303a, 0x18), "RON/RAN setup routed");
    expect(g.write(0x00301e, static_cast<uint8_t>(pc)), "R15 low routed");
    expect(g.write(0x00301f, static_cast<uint8_t>(pc >> 8)), "R15 high starts GSU");
}
struct GsuFixture {
    std::vector<uint8_t> rom;
    std::unique_ptr<CartridgeDevice> device;
    explicit GsuFixture(bool gsu2, const std::vector<uint8_t>& program = {})
        : rom(gsu2 ? 2 * 1024 * 1024 : 1024 * 1024, 0x01) {
        for (std::size_t i = 0; i < program.size(); ++i) rom[i] = program[i];
        device = gsu2 ? createSuperFX2Device(rom, 128 * 1024)
                      : createSuperFXDevice(rom, 64 * 1024);
    }
};
void testUncachedClockSelectUsesExternalBusRate() {
    GsuFixture slowFx(true, {0x01, 0x00});
    auto& slow = *slowFx.device;
    start(slow);
    slow.clock(98);
    expect(running(slow), "slow-mode first cache-line fill remains active through ninety-eight master clocks");
    slow.clock(2);
    expect(!running(slow), "slow-mode 16-byte line fill plus NOP/STOP completes at one hundred master clocks");
    GsuFixture fastFx(true, {0x01, 0x00});
    auto& fast = *fastFx.device;
    expect(fast.write(0x003039, 0x01), "CLSR fast-mode write routed");
    start(fast);
    fast.clock(81);
    expect(running(fast), "21-MHz first cache-line fill remains externally bus-limited through eighty-one master clocks");
    fast.clock(1);
    expect(!running(fast), "21-MHz 16-byte line fill plus NOP/STOP completes at eighty-two master clocks");
}
void testCachedIwtConsumesThreeCoreCycles() {
    GsuFixture fx(true);
    auto& g = *fx.device;
    cacheProgram(g, {0xf0, 0x34, 0x12, 0x00});
    start(g);
    g.clock(8);
    expect(running(g), "cached IWT is still running before its three-cycle payload plus STOP completes");
    g.clock(2);
    expect(!running(g), "cached IWT+STOP completes after five slow-core cycles including startup pipe");
    uint8_t lo=0, hi=0;
    expect(g.read(0x003000, lo) && g.read(0x003001, hi), "R0 read routed after IWT");
    expect(uint16_t(lo | (uint16_t(hi) << 8)) == 0x1234, "IWT result remains correct under corrected timing");
}
void testMultiplierSpeedAndRevision() {
    GsuFixture slowFx(true);
    auto& slow = *slowFx.device;
    cacheProgram(slow, {0x80, 0x00});
    start(slow);
    slow.clock(6);
    expect(running(slow), "GSU2 slow multiplier adds one cached core cycle");
    slow.clock(2);
    expect(!running(slow), "GSU2 slow cached MULT completes at eight master clocks including startup/STOP");
    GsuFixture fastFx(true);
    auto& fast = *fastFx.device;
    cacheProgram(fast, {0x80, 0x00});
    expect(fast.write(0x003037, 0x20), "CFGR MS0 fast-multiply write routed");
    start(fast);
    fast.clock(4);
    expect(running(fast), "GSU2 fast MULT has not yet executed following STOP at four master clocks");
    fast.clock(2);
    expect(!running(fast), "GSU2 fast cached MULT completes two master clocks earlier than slow mode");
    GsuFixture documentedFx(false);
    auto& documented = *documentedFx.device;
    cacheProgram(documented, {0x80, 0x00});
    expect(documented.write(0x003037, 0x20), "CFGR MS0 write routed on non-GSU2 profile");
    start(documented);
    documented.clock(4);
    expect(running(documented), "documented fast multiplier remains active without guessing physical MARIO revision");
    documented.clock(2);
    expect(!running(documented), "non-GSU2 profile follows documented MS0 timing absent reliable mask metadata");
}
uint64_t clocksToStop(CartridgeDevice& g, uint64_t limit = 2000) {
    uint64_t elapsed = 0;
    while (running(g) && elapsed < limit) { g.clock(1); ++elapsed; }
    return elapsed;
}
void testGetbWaitsOnlyForOutstandingR14Tail() {
    GsuFixture waiting(true);
    auto& g = *waiting.device;
    cacheProgram(g, {0xef, 0x00});
    expect(g.write(0x00301c, 0x20), "R14 low write routed for GET wait test");
    expect(g.write(0x00301d, 0x00), "R14 high write starts GET wait transfer");
    start(g);
    const uint64_t waitingClocks = clocksToStop(g);
    GsuFixture ready(true);
    auto& r = *ready.device;
    expect(r.write(0x00301c, 0x20), "R14 low write routed for ready GET test");
    expect(r.write(0x00301d, 0x00), "R14 high write starts ready GET transfer");
    cacheProgram(r, {0x01, 0x01, 0x01, 0xef, 0x00});
    start(r);
    const uint64_t readyClocks = clocksToStop(r);
    expect(readyClocks <= waitingClocks + 6,
           "completed R14 prefetch removes the fixed GETB worst-case stall");
}
void testStoreBufferOverlapsIndependentInstructions() {
    GsuFixture fx(true);
    auto& g = *fx.device;
    cacheProgram(g, {0x30, 0x01, 0x01, 0x00});
    start(g);
    const uint64_t elapsed = clocksToStop(g);
    expect(elapsed < 18, "cached STW overlaps its RAM-buffer drain with following independent instructions");
}
void testGsu2StoreStopDrainsRamBuffer() {
    GsuFixture fx(true);
    auto& g = *fx.device;
    cacheProgram(g, {0x31, 0x00});
    expect(g.write(0x003000, 0x34) && g.write(0x003001, 0x12), "R0 store value routed");
    expect(g.write(0x003002, 0x20) && g.write(0x003003, 0x00), "R1 store address routed");
    start(g);
    const uint64_t elapsed = clocksToStop(g);
    expect(elapsed > 0 && !running(g), "GSU2 store->STOP eventually retires");
    uint8_t lo = 0, hi = 0;
    expect(g.read(0x006020, lo) && g.read(0x006021, hi), "stored RAM word readable after STOP");
    expect(lo == 0x34 && hi == 0x12,
           "GSU2 STOP drains the final asynchronous RAM-buffer byte before GO clears");
}
void testAutomaticCacheFillsWholeLine() {
    GsuFixture fx(true, {0x01, 0x00, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f,
                         0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78});
    auto& g = *fx.device;
    start(g);
    (void)clocksToStop(g);
    uint8_t cached0 = 0xff, cached1 = 0xff, future = 0xff, lineEnd = 0xff;
    expect(g.read(0x003100, cached0), "cache byte 0 readable after STOP");
    expect(g.read(0x003101, cached1), "cache byte 1 readable after STOP");
    expect(g.read(0x003108, future), "unreached cache byte readable after STOP");
    expect(g.read(0x00310f, lineEnd), "cache line-end byte readable after STOP");
    expect(cached0 == 0x01 && cached1 == 0x00,
           "automatic cache records bytes that actually crossed the program bus");
    expect(future == 0x71 && lineEnd == 0x78,
           "automatic cache miss fills all sixteen bytes of the cache line");
}
void testProgramFetchSerializesWithR14Bus() {
    GsuFixture clearFx(true, {0x01, 0x00});
    auto& clear = *clearFx.device;
    start(clear);
    const uint64_t clearClocks = clocksToStop(clear);
    GsuFixture busyFx(true, {0x01, 0x00});
    auto& busy = *busyFx.device;
    expect(busy.write(0x00301c, 0x20), "R14 low write routed for program-bus collision test");
    expect(busy.write(0x00301d, 0x00), "R14 high write starts program-bus collision transfer");
    start(busy);
    const uint64_t busyClocks = clocksToStop(busy);
    expect(busyClocks == clearClocks + 4,
           "uncached ROM opcode fetch pays the unresolved R14 bus tail");
}
void testJumpCompletesOriginAndTargetCacheStripes() {
    std::vector<uint8_t> program(32, 0x01);
    program[0] = 0x05;
    program[1] = 0x11;
    program[2] = 0x01;
    for (unsigned i = 3; i < 0x13; ++i) program[i] = static_cast<uint8_t>(0x40u + i);
    program[0x13] = 0x00;
    program[0x1f] = 0x9a;
    GsuFixture fx(true, program);
    auto& g = *fx.device;
    start(g);
    (void)clocksToStop(g, 1000);
    expect(!running(g), "forward-branch cache-fill program reaches STOP");
    uint8_t originTail = 0, originEnd = 0, targetLead = 0, targetBefore = 0, targetFuture = 0xff;
    expect(g.read(0x003103, originTail), "origin stripe tail cache byte readable");
    expect(g.read(0x00310f, originEnd), "origin stripe final cache byte readable");
    expect(g.read(0x003110, targetLead), "target stripe leading cache byte readable");
    expect(g.read(0x003112, targetBefore), "target pre-byte cache byte readable");
    expect(g.read(0x00311f, targetFuture), "target stripe future cache byte readable");
    expect(originTail == program[3] && originEnd == program[0x0f],
           "forward jump completes the unfinished origin cache stripe");
    expect(targetLead == program[0x10] && targetBefore == program[0x12],
           "unaligned jump target backfills leading bytes of its cache stripe");
    expect(targetFuture == program[0x1f],
           "jump target miss fills the complete target cache stripe");
}
void testRombWaitsForOutstandingTransferWhenRonIsWithdrawn() {
    GsuFixture fx(true);
    auto& g = *fx.device;
    cacheProgram(g, {0x3f, 0xdf, 0x00});
    expect(g.write(0x003000, 0x01) && g.write(0x003001, 0x00), "R0=1 written for ROMB target bank");
    expect(g.write(0x00301c, 0x20), "R14 low starts ROMB-wait setup");
    expect(g.write(0x00301d, 0x00), "R14 high starts outstanding ROM transfer");
    start(g);
    expect(g.write(0x00303a, 0x08), "SCMR withdraws RON before GSU advances");
    g.clock(20);
    expect(running(g), "ROMB remains in GO/WAIT while its older R14 transfer lacks ROM grant");
    expect(g.write(0x00303a, 0x18), "SCMR restores RON for ROMB resume");
    g.clock(40);
    expect(!running(g), "ROMB resumes and reaches STOP after ROM grant returns");
    uint8_t bank = 0xff;
    expect(g.read(0x003036, bank), "ROMBR readable after resumed ROMB");
    expect(bank == 0x01, "ROMB commits new bank only after pending R14 transfer completes");
}
void testIndependentBusProgressDuringWait() {
    {
        GsuFixture fx(true);
        auto& g = *fx.device;
        cacheProgram(g, {0x31, 0xef, 0x00});
        writeReg(g, 0, 0x5a3c);
        writeReg(g, 1, 0x0000);
        writeReg(g, 14, 0x0040);
        start(g);
        expect(g.write(0x00303a, 0x08), "withdraw RON while retaining RAN for independent RAM progress");
        g.clock(40);
        expect(running(g), "GETB remains in WAIT without RON after preceding STW");
        expect(g.write(0x00303a, 0x00), "release RAN to inspect completed buffered store");
        uint8_t lo = 0, hi = 0;
        expect(g.read(0x700000, lo) && g.read(0x700001, hi), "host reads RAM after RAN release");
        expect(lo == 0x3c && hi == 0x5a,
               "RAM write buffer completes while CPU waits on unrelated ROM bus");
    }
    {
        GsuFixture fx(true);
        auto& g = *fx.device;
        cacheProgram(g, {0xde, 0x41, 0x00});
        writeReg(g, 14, 0x0040);
        writeReg(g, 1, 0x0000);
        start(g);
        expect(g.write(0x00303a, 0x10), "withdraw RAN while retaining RON for independent ROM progress");
        g.clock(40);
        expect(running(g), "LDW remains in WAIT without RAN");
        const uint8_t sfr = sfrLo(g);
        expect((sfr & 0x40u) == 0,
               "R14 ROM transfer completes while CPU waits on unrelated RAM bus");
    }
}
void testRonRanGrantMatrixWithConcurrentBuffers() {
    struct Case { uint8_t scmr; bool ron; bool ran; const char* name; };
    const Case cases[] = {
        {0x00, false, false, "RON0/RAN0"},
        {0x08, false, true,  "RON0/RAN1"},
        {0x10, true,  false, "RON1/RAN0"},
        {0x18, true,  true,  "RON1/RAN1"},
    };
    for (const auto& c : cases) {
        GsuFixture fx(true);
        auto& g = *fx.device;
        cacheProgram(g, {0x31, 0xef, 0x00});
        writeReg(g, 0, 0x5a3c);
        writeReg(g, 1, 0x0000);
        writeReg(g, 14, 0x0040);
        start(g);
        expect(g.write(0x00303a, c.scmr), c.name);
        g.clock(48);
        const bool romDone = (sfrLo(g) & 0x40u) == 0;
        expect(romDone == c.ron, c.ron
            ? "RON grant completes outstanding R14 transfer"
            : "without RON outstanding R14 transfer remains pending");
        uint8_t lo = 0, hi = 0;
        expect(g.read(0x700000, lo) && g.read(0x700001, hi), "grant-matrix RAM inspection routed");
        if (c.ran) {
            expect(lo == 0x3c && hi == 0x5a,
                   "RAN grant lets STW and its buffered high byte reach Game Pak RAM");
        } else {
            expect(lo == 0x00 && hi == 0x00,
                   "without RAN cached STW cannot modify Game Pak RAM");
        }
        if (c.ron && c.ran) expect(!running(g), "RON1/RAN1 completes STW, GETB and STOP");
        else expect(running(g), "missing required bus grant leaves GSU in GO/WAIT");
    }
}
void testCacheValidityUsesIndependentLineBits() {
    GsuFixture fx(true);
    auto& g = *fx.device;
    for (unsigned i = 0; i < 16; ++i)
        expect(g.write(0x003100 + i, i == 0 ? 0x01 : (i == 1 ? 0x00 : 0x01)), "line0 cache write routed");
    start(g, 0x0000);
    g.clock(8);
    expect(!running(g), "manually validated cache line executes without external fill");
    expect(g.write(0x003034, 0x00), "PBR write invalidates cache-valid line mask");
    for (unsigned i = 0; i < 16; ++i)
        expect(g.write(0x003100 + i, i == 0 ? 0x00 : 0x01), "cache line rewrite routed");
    const auto state = g.saveState();
    auto restored = createSuperFX2Device(fx.rom, 128 * 1024);
    expect(restored->loadState(state), "phase3 compact cache-valid save state restores");
    start(*restored, 0x0000);
    restored->clock(4);
    expect(!running(*restored), "restored line-valid mask keeps manually loaded STOP executable");
}
void testPendingTransferCompletesInsideLongInstruction() {
    GsuFixture fx(true);
    auto& g = *fx.device;
    cacheProgram(g, {0x9f, 0x00});
    writeReg(g, 0, 0x0002);
    writeReg(g, 6, 0x0003);
    writeReg(g, 14, 0x0040);
    start(g);
    g.clock(6);
    expect(running(g), "long cached FMULT remains in flight at ROM-buffer completion point");
    const uint8_t sfr = sfrLo(g);
    expect((sfr & 0x40u) == 0,
           "R14 ROM-buffer event becomes visible before long instruction retirement");
}
void testCacheOutsideWindowInstallsNewBase() {
    std::vector<uint8_t> program(0x1002, 0x01);
    program[0x1000] = 0x02;
    program[0x1001] = 0x00;
    GsuFixture fx(true, program);
    auto& g = *fx.device;
    start(g, 0x1000);
    const uint64_t clocks = clocksToStop(g);
    expect(clocks == 16,
           "external CACHE installs the new CBR and reaches the now-cacheable STOP at documented slow-bus timing");
    uint8_t lo = 0xff, hi = 0xff;
    expect(g.read(0x00303e, lo) && g.read(0x00303f, hi), "CBR readable after external CACHE");
    expect(uint16_t(lo | (uint16_t(hi) << 8)) == 0x1000,
           "CACHE aligns CBR to the executing $1000 stripe");
}
void testRamResidentStoresPaySameBusContention() {
    std::vector<uint8_t> romStwProgram(0x1002, 0x01);
    romStwProgram[0x1000] = 0x31; romStwProgram[0x1001] = 0x00;
    GsuFixture romStwFx(false, romStwProgram);
    auto& romStw = *romStwFx.device;
    writeReg(romStw, 0, 0x5a3c);
    writeReg(romStw, 1, 0x2000);
    start(romStw, 0x1000);
    const uint64_t romStwClocks = clocksToStop(romStw);
    std::vector<uint8_t> ramStwBacking(1024 * 1024, 0x01);
    auto ramStw = createSuperFXDevice(ramStwBacking, 64 * 1024);
    writeReg(*ramStw, 0, 0x5a3c);
    writeReg(*ramStw, 1, 0x2000);
    expect(ramStw->write(0x701000, 0x31), "RAM-resident STW opcode written");
    expect(ramStw->write(0x701001, 0x00), "RAM-resident STW STOP written");
    expect(ramStw->write(0x003034, 0x70), "PBR selects Game Pak RAM for STW timing");
    expect(ramStw->write(0x00303a, 0x18), "RON/RAN grant for RAM-resident STW");
    writeReg(*ramStw, 15, 0x1000);
    const uint64_t ramStwClocks = clocksToStop(*ramStw);
    expect(ramStwClocks == romStwClocks + 8,
           "RAM-resident STW pays four extra low-speed GSU cycles for same-bus contention");
    std::vector<uint8_t> romStbProgram(0x1003, 0x01);
    romStbProgram[0x1000] = 0x3d; romStbProgram[0x1001] = 0x31; romStbProgram[0x1002] = 0x00;
    GsuFixture romStbFx(false, romStbProgram);
    auto& romStb = *romStbFx.device;
    writeReg(romStb, 0, 0x5a3c);
    writeReg(romStb, 1, 0x2000);
    start(romStb, 0x1000);
    const uint64_t romStbClocks = clocksToStop(romStb);
    std::vector<uint8_t> ramStbBacking(1024 * 1024, 0x01);
    auto ramStb = createSuperFXDevice(ramStbBacking, 64 * 1024);
    writeReg(*ramStb, 0, 0x5a3c);
    writeReg(*ramStb, 1, 0x2000);
    expect(ramStb->write(0x701000, 0x3d), "RAM-resident STB ALT1 byte written");
    expect(ramStb->write(0x701001, 0x31), "RAM-resident STB opcode written");
    expect(ramStb->write(0x701002, 0x00), "RAM-resident STB STOP written");
    expect(ramStb->write(0x003034, 0x70), "PBR selects Game Pak RAM for STB timing");
    expect(ramStb->write(0x00303a, 0x18), "RON/RAN grant for RAM-resident STB");
    writeReg(*ramStb, 15, 0x1000);
    const uint64_t ramStbClocks = clocksToStop(*ramStb);
    expect(ramStbClocks == romStbClocks + 4,
           "RAM-resident STB pays two extra low-speed GSU cycles beyond ROM execution");
}
void testPixelRamTrafficSerializesOlderStore() {
    GsuFixture fx(true);
    auto& g = *fx.device;
    cacheProgram(g, {0x33, 0x3d, 0x4c, 0x00});
    writeReg(g, 0, 0x8080);
    writeReg(g, 1, 0x0000);
    writeReg(g, 2, 0x0000);
    writeReg(g, 3, 0x0000);
    start(g);
    const uint64_t elapsed = clocksToStop(g);
    uint8_t lo=0, hi=0;
    expect(g.read(0x003000, lo) && g.read(0x003001, hi), "RPIX destination register readable");
    expect(uint16_t(lo | (uint16_t(hi) << 8)) == 3,
           "RPIX observes the buffered STW byte before reading the same RAM planes");
    expect(elapsed == 30,
           "RPIX serializes its RAM-controller work behind the surviving STW buffer tail");
}
void testR14LowSpeedTransferIsThreeCycles() {
    GsuFixture fx(true);
    auto& g = *fx.device;
    cacheProgram(g, {0x01, 0x01, 0x01, 0x00});
    expect(g.write(0x00301c, 0x20), "R14 low latch write routed");
    expect(g.write(0x00301d, 0x00), "R14 high write starts ROM-buffer transfer");
    expect((sfrLo(g) & 0x40) != 0, "R14 transfer starts with SFR.R set");
    start(g);
    g.clock(4);
    expect((sfrLo(g) & 0x40) != 0, "10.7-MHz ROM transfer remains pending after two GSU cycles");
    g.clock(2);
    expect((sfrLo(g) & 0x40) == 0, "10.7-MHz ROM transfer completes after three GSU cycles");
}
}
void testHostAccessRemainsByteGranularWhileRunning() {
    std::vector<uint8_t> rom(2 * 1024 * 1024, 0x01);
    auto g = createGSUDevice(rom, 128 * 1024, GSUTimingProfile{GSURevision::GSU2});
    uint8_t lo = 0, hi = 0;
    expect(g->write(0x003001, 0x12), "host R0 high-byte write routed");
    expect(g->read(0x003000, lo) && g->read(0x003001, hi) && lo == 0x00 && hi == 0x12,
           "host R0 high-byte write commits immediately");
    expect(g->write(0x003000, 0x34), "host R0 low-byte write routed");
    expect(g->read(0x003000, lo) && g->read(0x003001, hi) && lo == 0x34 && hi == 0x12,
           "host R0 low-byte write preserves the existing high byte");
    expect(g->write(0x00303a, 0x08), "RAN grant configured before GO");
    expect(g->write(0x00301e, 0x00) && g->write(0x00301f, 0x00), "GSU GO started through R15");
    uint8_t open = 0;
    expect(g->readHost(0x700000, 0xA5, open) && open == 0xA5,
           "host RAM read is denied/open-bus while GO+RAN is active");
    expect(g->write(0x700000, 0x5A),
           "host RAM write is accepted while GO+RAN is active");
    expect(g->write(0x003000, 0x78) && g->write(0x003001, 0x56),
           "host general-register writes remain accepted while GO is set");
    expect(g->read(0x003000, lo) && g->read(0x003001, hi) && lo == 0x78 && hi == 0x56,
           "host general-register reads remain visible while GO is set");
    expect(g->write(0x003100, 0x9B), "host cache write remains accepted while GO is set");
    expect(g->read(0x003100, lo) && lo == 0x9B, "host cache read remains visible while GO is set");
    expect(g->write(0x003030, 0x00), "host clears GO through SFR");
    expect(g->readHost(0x700000, 0x00, open) && open == 0x5A,
           "host RAM write made during GO+RAN survives ownership release");
}
void testHostRegisterPermissionsAndRevisionMirrors() {
    std::vector<uint8_t> rom(2 * 1024 * 1024, 0x01);
    auto gsu2 = createGSUDevice(rom, 128 * 1024, GSUTimingProfile{GSURevision::GSU2});
    uint8_t v = 0xff;
    expect(gsu2->read(0x00303b, v) && v == 0x04, "GSU2 VCR exposes measured revision code 04");
    expect(gsu2->write(0x003036, 0x55), "host ROMBR write is consumed");
    expect(gsu2->read(0x003036, v) && v == 0x00, "host ROMBR write cannot modify GSU ROM bank");
    expect(gsu2->write(0x00303c, 0x01), "host RAMBR write is consumed");
    expect(gsu2->read(0x00303c, v) && v == 0x00, "host RAMBR write cannot modify GSU RAM bank");
    expect(gsu2->write(0x00303e, 0x34) && gsu2->write(0x00303f, 0x12), "host CBR writes are consumed");
    uint8_t cbrLo = 0xff, cbrHi = 0xff;
    expect(gsu2->read(0x00303e, cbrLo) && gsu2->read(0x00303f, cbrHi) && cbrLo == 0 && cbrHi == 0,
           "host CBR writes cannot modify cache base");
    expect(gsu2->write(0x003033, 0xff), "BRAMR write routed");
    expect(gsu2->read(0x003033, v) && v == 0x00, "BRAMR remains write-only to the host");
    expect(gsu2->write(0x003040, 0x5a) && gsu2->write(0x003041, 0xa5), "GSU2 R0 mirror write routed");
    uint8_t lo = 0, hi = 0;
    expect(gsu2->read(0x003000, lo) && gsu2->read(0x003001, hi) && lo == 0x5a && hi == 0xa5,
           "GSU2 3040 mirror aliases canonical R0");
    expect(gsu2->write(0x003340, 0x34) && gsu2->write(0x003341, 0x12), "GSU2 33xx R0 mirror write routed");
    expect(gsu2->read(0x003000, lo) && gsu2->read(0x003001, hi) && lo == 0x34 && hi == 0x12,
           "GSU2 33xx mirror aliases canonical R0");
    expect(gsu2->read(0x00302b, v) && v == 0x04, "GSU2 3020-302f mirrors 3030-303f including VCR");
    auto mc1 = createGSUDevice(rom, 64 * 1024, GSUTimingProfile{GSURevision::MC1});
    expect(mc1->read(0x00303b, v) && v == 0x01, "MC1 VCR exposes measured revision code 01");
    expect(mc1->write(0x003034, 0x2a), "MC1 canonical PBR write routed");
    expect(mc1->read(0x003034, v) && v == 0x2a, "MC1 canonical 3034 remains PBR, not an SFR alias");
    expect(mc1->write(0x00303a, 0x18), "MC1 canonical SCMR write routed");
    expect(mc1->read(0x003030, v) && (v & 0x20) == 0, "MC1 SCMR write does not alias SFR.G");
    expect(mc1->read(0x00307b, v) && v == 0x01, "MC1 307b VCR mirror is present");
    expect(mc1->read(0x00333b, v) && v == 0x01, "MC1 333b VCR mirror is present");
    expect(mc1->read(0x00343b, v) && v == 0x01, "MC1 343b VCR mirror is present");
    expect(mc1->write(0x003040, 0xcd) && mc1->write(0x003041, 0xab), "MC1 R0 mirror write routed");
    expect(mc1->read(0x003000, lo) && mc1->read(0x003001, hi) && lo == 0xcd && hi == 0xab,
           "MC1 3040-305f mirrors R0-R15");
    expect(mc1->read(0x003020, v) && v == 0x00, "MC1 3020-302f is open bus in current bus abstraction");
    auto gsu1 = createGSUDevice(rom, 64 * 1024, GSUTimingProfile{GSURevision::GSU1});
    expect(gsu1->read(0x00303b, v) && v == 0x04, "GSU1 VCR exposes measured revision code 04");
    auto gsu1a = createGSUDevice(rom, 64 * 1024, GSUTimingProfile{GSURevision::GSU1A});
    expect(gsu1a->read(0x00303b, v) && v == 0x04, "GSU1A VCR exposes measured revision code 04");
}
void testProgramRamWindowUsesRanAcross60To7f() {
    GsuFixture fx(true);
    auto& g = *fx.device;
    expect(g.write(0x700000, 0x01), "program RAM NOP write routed");
    expect(g.write(0x700001, 0x00), "program RAM STOP write routed");
    expect(g.write(0x003034, 0x60), "PBR selects the first GSU RAM execution bank");
    start(g);
    g.clock(100);
    expect(!running(g), "PBR $60 executes from expansion RAM under RAN grant");
}
int main() {
    testHostAccessRemainsByteGranularWhileRunning();
    testHostRegisterPermissionsAndRevisionMirrors();
    testProgramRamWindowUsesRanAcross60To7f();
    testUncachedClockSelectUsesExternalBusRate();
    testCachedIwtConsumesThreeCoreCycles();
    testMultiplierSpeedAndRevision();
    testCacheOutsideWindowInstallsNewBase();
    testRombWaitsForOutstandingTransferWhenRonIsWithdrawn();
    testIndependentBusProgressDuringWait();
    testRonRanGrantMatrixWithConcurrentBuffers();
    testCacheValidityUsesIndependentLineBits();
    testPendingTransferCompletesInsideLongInstruction();
    testR14LowSpeedTransferIsThreeCycles();
    testRamResidentStoresPaySameBusContention();
    testGetbWaitsOnlyForOutstandingR14Tail();
    testStoreBufferOverlapsIndependentInstructions();
    testGsu2StoreStopDrainsRamBuffer();
    testPixelRamTrafficSerializesOlderStore();
    testAutomaticCacheFillsWholeLine();
    testProgramFetchSerializesWithR14Bus();
    testJumpCompletesOriginAndTargetCacheStripes();
    if (failures) return 1;
    std::cout << "phase29 SuperFX variable-bus timing accuracy ok\n";
    return 0;
}

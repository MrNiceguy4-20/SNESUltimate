#include "cartridge.hpp"
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
namespace {
int failures = 0;
void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}
void write16(std::vector<uint8_t>& rom, std::size_t offset, uint16_t value) {
    rom[offset] = static_cast<uint8_t>(value);
    rom[offset + 1] = static_cast<uint8_t>(value >> 8);
}
std::vector<uint8_t> makeSuperFXRom() {
    constexpr std::size_t header = 0x7FC0;
    std::vector<uint8_t> rom(1024 * 1024, 0);
    const char* title = "PHASE110 SUPERFX";
    for (std::size_t i = 0; i < 21; ++i) rom[header + i] = ' ';
    for (std::size_t i = 0; title[i] && i < 21; ++i) rom[header + i] = static_cast<uint8_t>(title[i]);
    rom[header + 0x15] = 0x20;
    rom[header + 0x16] = 0x13;
    rom[header + 0x17] = 0x0A;
    rom[header + 0x18] = 0x05;
    rom[header + 0x19] = 0x01;
    rom[header + 0x1A] = 0x01;
    rom[header - 3] = 0x05;
    write16(rom, header + 0x1C, 0xEDCB);
    write16(rom, header + 0x1E, 0x1234);
    write16(rom, header + 0x3C, 0x8000);
    return rom;
}
void writeReg(Cartridge& cart, unsigned r, uint16_t value) {
    const uint32_t a = 0x003000u + r * 2u;
    expect(cart.writeHardware(a, static_cast<uint8_t>(value)), "GSU register low-byte write routed");
    if (r == 15) {
        return;
    }
    expect(cart.writeHardware(a + 1, static_cast<uint8_t>(value >> 8)), "GSU register high-byte write routed");
}
uint16_t readReg(Cartridge& cart, unsigned r) {
    const uint32_t a = 0x003000u + r * 2u;
    uint8_t lo = 0, hi = 0;
    expect(cart.readHardware(a, lo), "GSU register low-byte read routed");
    expect(cart.readHardware(a + 1, hi), "GSU register high-byte read routed");
    return static_cast<uint16_t>(lo | (uint16_t(hi) << 8));
}
void startProgram(Cartridge& cart, uint16_t pc) {
    expect(cart.writeHardware(0x00303A, 0x18), "GSU RON/RAN grant write routed");
    expect(cart.writeHardware(0x00301E, static_cast<uint8_t>(pc)), "GSU R15 low-byte write routed");
    expect(cart.writeHardware(0x00301F, static_cast<uint8_t>(pc >> 8)), "GSU R15 high-byte starts execution");
}
void clockSliced(Cartridge& cart, uint64_t clocks) {
    while (clocks != 0) {
        const uint64_t slice = clocks >= 2 ? 2 : 1;
        cart.clockHardware(slice);
        clocks -= slice;
    }
}
void runProgram(Cartridge& cart, uint64_t clocks = 4096) {
    uint64_t remaining = clocks;
    while (remaining != 0) {
        const uint64_t slice = remaining >= 2 ? 2 : 1;
        cart.clockHardware(slice);
        remaining -= slice;
        uint8_t sfr = 0;
        if (cart.readHardware(0x003030, sfr) && (sfr & 0x20u) == 0) break;
    }
}
void testInstructionCacheMissTiming() {
    auto rom = makeSuperFXRom();
    rom[0x0000] = 0xD0;
    rom[0x0001] = 0x00;
    Cartridge cart; cart.setSRAMPersistence(false);
    expect(cart.loadData(std::move(rom), "/tmp/phase8_gsu_cache_miss_timing.sfc"), "cache-miss timing ROM loads");
    startProgram(cart, 0x0000);
    cart.clockHardware(2);
    uint8_t sfr = 0;
    expect(cart.readHardware(0x003030, sfr) && (sfr & 0x20) != 0,
           "startup pipe NOP leaves GSU running before the first cache-line fill completes");
    cart.clockHardware(96);
    expect(cart.readHardware(0x003030, sfr) && (sfr & 0x20) != 0,
           "16-byte cache-line fill keeps the first ROM opcode in flight through ninety-eight master clocks");
    cart.clockHardware(2);
    expect(cart.readHardware(0x003030, sfr) && (sfr & 0x20) == 0,
           "first line fill makes the following STOP a cache hit and completes at one hundred master clocks");
    expect(readReg(cart, 0) == 1, "uncached first ROM opcode executed exactly once");
}
void testBranchDelaySlotPreservesWithPrefix() {
    auto rom = makeSuperFXRom();
    const uint8_t program[] = {0xA3,0x05, 0xA0,0x02, 0x23, 0x05,0x01, 0x50, 0x00};
    for (std::size_t i = 0; i < sizeof(program); ++i) rom[i] = program[i];
    Cartridge cart;
    cart.setSRAMPersistence(false);
    expect(cart.loadData(std::move(rom), "/tmp/phase207_gsu_branch_delay_prefix.sfc"), "branch-delay prefix ROM loads");
    startProgram(cart, 0x0000);
    runProgram(cart, 20000);
    expect(readReg(cart, 3) == 0x0007, "BRA preserves WITH destination/source into delay-slot ADD");
    expect(readReg(cart, 0) == 0x0002, "delay-slot ADD does not fall back to R0 after BRA");
}
void testSignedBranchPredicates() {
    auto make = [](uint8_t branch) {
        auto rom = makeSuperFXRom();
        const uint8_t program[] = {
            0xA0,0x00,
            0x3E,0x61,
            branch,0x04,
            0x01,
            0xA3,0x11,
            0x00,
            0xA3,0x22,
            0x00
        };
        for (std::size_t i=0; i<sizeof(program); ++i) rom[i]=program[i];
        return rom;
    };
    Cartridge blt; blt.setSRAMPersistence(false);
    expect(blt.loadData(make(0x07), "/tmp/phase208_gsu_blt.sfc"), "BLT predicate ROM loads");
    startProgram(blt,0); runProgram(blt,20000);
    expect(readReg(blt,3)==0x0022, "BLT $07 branches when S!=V");
    Cartridge bge; bge.setSRAMPersistence(false);
    expect(bge.loadData(make(0x06), "/tmp/phase208_gsu_bge.sfc"), "BGE predicate ROM loads");
    startProgram(bge,0); runProgram(bge,20000);
    expect(readReg(bge,3)==0x0011, "BGE $06 does not branch when S!=V");
}
void testR14RomTransferIsPending() {
    auto rom = makeSuperFXRom();
    rom[0x20] = 0xA5;
    rom[0x100] = 0xEF;
    rom[0x101] = 0x00;
    Cartridge cart;
    cart.setSRAMPersistence(false);
    expect(cart.loadData(std::move(rom), "/tmp/phase208_gsu_r14_pending.sfc"), "R14 pending ROM loads");
    writeReg(cart, 14, 0x0020);
    uint8_t sfr = 0;
    expect(cart.readHardware(0x003030, sfr), "R14 pending SFR read routed");
    expect((sfr & 0x40) != 0, "R14 write raises SFR.R while ROM transfer is pending");
    startProgram(cart, 0x0100);
    runProgram(cart, 20000);
    expect(readReg(cart, 0) == 0x00A5, "GETB consumes completed R14 ROM-buffer transfer");
    expect(cart.readHardware(0x003030, sfr), "completed R14 SFR read routed");
    expect((sfr & 0x40) == 0, "SFR.R clears when R14 ROM transfer completes");
}
void testPendingBusOperationMaturesInsideLongInstruction() {
    auto rom = makeSuperFXRom();
    rom[0x20] = 0x5A;
    rom[0x0000] = 0x40;
    rom[0x0001] = 0x00;
    Cartridge cart; cart.setSRAMPersistence(false);
    expect(cart.loadData(std::move(rom), "/tmp/phase10_gsu_bus_event_timing.sfc"), "GSU bus-event timing ROM loads");
    writeReg(cart, 14, 0x0020);
    startProgram(cart, 0x0000);
    clockSliced(cart, 12);
    uint8_t sfr=0;
    expect(cart.readHardware(0x003030, sfr), "mid-instruction SFR read routed");
    expect((sfr & 0x40) == 0, "pending ROM-buffer transaction matures at its own deadline inside a long instruction");
    expect((sfr & 0x20) != 0, "long instruction is still executing when independent ROM transfer completes");
    runProgram(cart, 20000);
}
void testRunningHostRegisterAccess() {
    auto rom = makeSuperFXRom();
    Cartridge cart; cart.setSRAMPersistence(false);
    expect(cart.loadData(std::move(rom), "/tmp/phase208_gsu_host_access.sfc"), "host-access ROM loads");
    writeReg(cart, 0, 0x1234);
    expect(cart.writeHardware(0x00303A, 0x08), "host-access RAN setup routed");
    expect(cart.writeHardware(0x003034, 0x00), "host-access PBR rewrite invalidates cache");
    expect(cart.writeHardware(0x00301E, 0x00), "host-access R15 low routed");
    expect(cart.writeHardware(0x00301F, 0x00), "host-access starts GSU");
    runProgram(cart, 2000);
    uint8_t sfr = 0;
    expect(cart.readHardware(0x003030, sfr), "running SFR remains host-readable");
    expect((sfr & 0x20) != 0, "host-access test GSU remains running in ROM wait");
    uint8_t visible = 0xFF;
    expect(cart.readHardware(0x003000, visible), "running GSU register read is intercepted");
    expect(visible == 0x34, "running GSU general register remains host-readable");
    expect(cart.writeHardware(0x003000, 0x78), "running low register write is accepted");
    expect(cart.writeHardware(0x003001, 0x56), "running high register write is accepted");
    expect(cart.writeHardware(0x003030, 0x00), "SFR write can halt running GSU");
    expect(readReg(cart, 0) == 0x5678, "running host writes update GSU registers byte-by-byte");
}
void testCacheWindowUsesCbrRotation() {
    auto rom = makeSuperFXRom();
    rom[0x1230] = 0x02;
    rom[0x1231] = 0x00;
    Cartridge cart; cart.setSRAMPersistence(false);
    expect(cart.loadData(std::move(rom), "/tmp/phase208_gsu_cache_cbr.sfc"), "cache-CBR ROM loads");
    startProgram(cart, 0x1230); runProgram(cart, 20000);
    uint8_t cbrLo=0, cbrHi=0;
    expect(cart.readHardware(0x00303E, cbrLo), "CBR low readable");
    expect(cart.readHardware(0x00303F, cbrHi), "CBR high readable");
    expect(uint16_t(cbrLo | (uint16_t(cbrHi)<<8)) == 0x1230, "CACHE records aligned CBR");
    expect(cart.writeHardware(0x003100, 0xA5), "host cache-window write routed");
    uint8_t v=0;
    expect(cart.readHardware(0x003100, v), "host cache-window read routed");
    expect(v == 0xA5, "host cache window is CBR-rotated consistently");
    expect(cart.writeHardware(0x003030, 0x00), "host GO=0 write is accepted while already stopped");
    expect(cart.readHardware(0x00303E, cbrLo) && cart.readHardware(0x00303F, cbrHi),
           "CBR remains readable after explicit host GO=0");
    expect(uint16_t(cbrLo | (uint16_t(cbrHi)<<8)) == 0x0000,
           "host SFR write with GO=0 resets CBR even when GSU was already stopped");
}
void testRambMasksToSingleBankBit() {
    auto rom = makeSuperFXRom();
    rom.resize(2 * 1024 * 1024, 0);
    constexpr std::size_t header = 0x7FC0;
    rom[header + 0x17] = 0x0B;
    rom[header - 3] = 0x08;
    const uint8_t program[] = {
        0xA0,0x02,
        0x3E,0xDF,
        0xA1,0x00,
        0x41,
        0x00
    };
    for (std::size_t i=0; i<sizeof(program); ++i) rom[i]=program[i];
    Cartridge cart; cart.setSRAMPersistence(false);
    expect(cart.loadData(std::move(rom), "/tmp/phase208_gsu_ramb_mask.sfc"), "RAMB mask ROM loads");
    expect(cart.writeHardware(0x700000, 0x34), "bank0 low test byte writes");
    expect(cart.writeHardware(0x700001, 0x12), "bank0 high test byte writes");
    startProgram(cart,0); runProgram(cart,20000);
    expect(readReg(cart,0)==0x1234, "RAMB masks source to one hardware bank-select bit");
    uint8_t rambr = 0xff;
    expect(cart.readHardware(0x00303C, rambr) && rambr == 0, "GSU2 RAMB masks high source bits");
    auto gsu1Rom = makeSuperFXRom();
    gsu1Rom[0] = 0xA0; gsu1Rom[1] = 0x01;
    gsu1Rom[2] = 0x3E; gsu1Rom[3] = 0xDF; gsu1Rom[4] = 0x00;
    Cartridge gsu1;
    gsu1.setSRAMPersistence(false);
    expect(gsu1.loadData(std::move(gsu1Rom), "/tmp/phase208_gsu1_ramb.sfc"), "GSU1 RAMB ROM loads");
    startProgram(gsu1, 0); runProgram(gsu1, 20000);
    expect(gsu1.readHardware(0x00303C, rambr) && rambr == 0, "GSU1 does not expose the GSU2 second RAM bank");
}
void testMergeAndFlags() {
    auto rom = makeSuperFXRom();
    rom[0] = 0x70;
    rom[1] = 0x00;
    Cartridge cart;
    cart.setSRAMPersistence(false);
    expect(cart.loadData(std::move(rom), "/tmp/phase110_superfx.sfc"), "synthetic SuperFX ROM loads");
    expect(cart.hardwareType() == CartridgeHardware::SuperFX, "SuperFX header is recognized");
    expect(cart.hardwareImplemented(), "SuperFX instruction device is reported implemented");
    writeReg(cart, 7, 0x0000);
    writeReg(cart, 8, 0x0000);
    startProgram(cart, 0x0000);
    expect(readReg(cart, 0) == 0x0000, "clocked GSU does not execute synchronously on start");
    runProgram(cart);
    expect(readReg(cart, 0) == 0x0000, "MERGE zero case returns 0000");
    uint8_t sfrLo = 0;
    expect(cart.readHardware(0x003030, sfrLo), "GSU SFR is readable");
    expect((sfrLo & 0x1E) == 0x00, "MERGE zero case clears VSCZ flags");
    writeReg(cart, 7, 0xC000);
    writeReg(cart, 8, 0x3000);
    startProgram(cart, 0x0000);
    runProgram(cart);
    expect(readReg(cart, 0) == 0xC030, "MERGE combines high bytes of R7/R8");
    expect(cart.readHardware(0x003030, sfrLo), "GSU SFR second read succeeds");
    expect((sfrLo & 0x1E) == 0x1E, "MERGE C030 case sets VSCZ per GSU rules");
}
void testPlotRpixAndModes() {
    auto rom = makeSuperFXRom();
    rom[0x0000] = 0x4e;
    rom[0x0001] = 0x4c;
    rom[0x0002] = 0x00;
    rom[0x0010] = 0x3d;
    rom[0x0011] = 0x4c;
    rom[0x0012] = 0x00;
    Cartridge cart;
    cart.setSRAMPersistence(false);
    expect(cart.loadData(std::move(rom), "/tmp/phase_big_plot.sfc"), "plot test ROM loads");
    writeReg(cart, 0, 0x0003);
    writeReg(cart, 1, 0x0000);
    writeReg(cart, 2, 0x0000);
    startProgram(cart, 0x0000);
    runProgram(cart);
    expect(cart.writeHardware(0x00303a, 0x00), "PLOT returns RAM bus to S-CPU");
    uint8_t p0=0, p1=0;
    expect(cart.readHardware(0x700000, p0), "GSU framebuffer plane 0 readable");
    expect(cart.readHardware(0x700001, p1), "GSU framebuffer plane 1 readable");
    expect((p0 & 0x80) && (p1 & 0x80), "STOP flushes an incomplete 8-pixel PLOT group to Game Pak RAM");
    expect(readReg(cart, 1) == 1, "PLOT increments R1");
    writeReg(cart, 1, 0);
    startProgram(cart, 0x0010);
    expect(cart.writeHardware(0x00303a, 0x18), "RPIX receives ROM/RAM bus grants");
    cart.clockHardware(30);
    uint8_t rpixSfr = 0;
    expect(cart.readHardware(0x003030, rpixSfr) && (rpixSfr & 0x20) != 0,
           "RPIX remains busy while pixel-cache RAM transactions occupy the GSU bus");
    runProgram(cart);
    expect(readReg(cart, 0) == 3, "RPIX flushes pending pixel caches and reconstructs 2bpp color");
    expect(cart.writeHardware(0x00303a, 0x00), "RPIX returns RAM bus to S-CPU");
    expect(cart.readHardware(0x700000, p0) && cart.readHardware(0x700001, p1), "flushed 2bpp planes are readable");
    expect((p0 & 0x80) && (p1 & 0x80), "RPIX flush commits cached 2bpp PLOT data");
    writeReg(cart, 0, 0x000f);
    writeReg(cart, 1, 0x0008);
    writeReg(cart, 2, 0x0000);
    startProgram(cart, 0x0000);
    expect(cart.writeHardware(0x00303a, 0x1A), "SCMR mode 2 + RON/RAN write routed");
    runProgram(cart);
    writeReg(cart, 1, 0x0008);
    startProgram(cart, 0x0010);
    expect(cart.writeHardware(0x00303a, 0x1A), "RPIX restores 4bpp SCMR after generic start helper");
    runProgram(cart);
    expect(cart.writeHardware(0x00303a, 0x02), "4bpp test returns RAM bus to S-CPU");
    uint8_t q0=0,q1=0,q2=0,q3=0,q4=0;
    expect(cart.readHardware(0x700200, q0), "4bpp plane0 byte readable");
    expect(cart.readHardware(0x700201, q1), "4bpp plane1 byte readable");
    expect(cart.readHardware(0x700210, q2), "4bpp plane2 byte readable");
    expect(cart.readHardware(0x700211, q3), "4bpp plane3 byte readable");
    expect(cart.readHardware(0x700220, q4), "byte beyond 4bpp tile readable");
    expect((q0 & 0x80) && (q1 & 0x80) && (q2 & 0x80) && (q3 & 0x80), "SCMR mode 2 uses four bitplanes");
    expect((q4 & 0x80) == 0, "SCMR mode 2 does not write an 8bpp fifth plane");
}
void testPlotClipsToConfiguredScreenHeight() {
    auto rom = makeSuperFXRom();
    rom[0x0000] = 0x4e;
    rom[0x0001] = 0x4c;
    rom[0x0002] = 0x00;
    Cartridge cart;
    cart.setSRAMPersistence(false);
    expect(cart.loadData(std::move(rom), "/tmp/phase_gsu_plot_height_clip.sfc"),
           "PLOT height-clip ROM loads");
    writeReg(cart, 0, 0x0003);
    writeReg(cart, 1, 0x0000);
    writeReg(cart, 2, 0x007f);
    startProgram(cart, 0x0000);
    runProgram(cart, 20000);
    expect(cart.writeHardware(0x00303a, 0x00), "in-range PLOT returns RAM bus to S-CPU");
    uint8_t in0 = 0, in1 = 0;
    expect(cart.readHardware(0x7000fe, in0) && cart.readHardware(0x7000ff, in1),
           "last legal 128-line framebuffer row is readable");
    expect((in0 & 0x80) && (in1 & 0x80),
           "PLOT at Y=127 draws into the final legal scanline");
    expect(cart.writeHardware(0x700100, 0x00) && cart.writeHardware(0x700101, 0x00),
           "out-of-range PLOT corruption sentinel is cleared");
    writeReg(cart, 0, 0x0003);
    writeReg(cart, 1, 0x0000);
    writeReg(cart, 2, 0x0080);
    startProgram(cart, 0x0000);
    runProgram(cart, 20000);
    expect(cart.writeHardware(0x00303a, 0x00), "out-of-range PLOT returns RAM bus to S-CPU");
    uint8_t out0 = 0xff, out1 = 0xff;
    expect(cart.readHardware(0x700100, out0) && cart.readHardware(0x700101, out1),
           "out-of-range PLOT sentinel is readable");
    expect(out0 == 0x00 && out1 == 0x00,
           "PLOT at Y=128 is clipped instead of corrupting the next framebuffer tile");
    expect(readReg(cart, 1) == 1,
           "clipped PLOT still increments R1 exactly once");
}
void testPlotTransparencyAndColorNibbleSemantics() {
    auto plotRom = makeSuperFXRom();
    plotRom[0x0000] = 0x4e;
    plotRom[0x0001] = 0x4c;
    plotRom[0x0002] = 0x00;
    Cartridge plot;
    plot.setSRAMPersistence(false);
    expect(plot.loadData(std::move(plotRom), "/tmp/phase_gsu_plot_transparency_nibble.sfc"),
           "2bpp transparency-nibble ROM loads");
    expect(plot.writeHardware(0x700000, 0x80), "2bpp transparency test seeds plane 0");
    expect(plot.writeHardware(0x700001, 0x80), "2bpp transparency test seeds plane 1");
    writeReg(plot, 0, 0x0004);
    writeReg(plot, 1, 0x0000);
    writeReg(plot, 2, 0x0000);
    startProgram(plot, 0x0000);
    runProgram(plot, 20000);
    expect(plot.writeHardware(0x00303a, 0x00), "2bpp transparency test returns RAM bus to S-CPU");
    uint8_t p0 = 0xff, p1 = 0xff;
    expect(plot.readHardware(0x700000, p0) && plot.readHardware(0x700001, p1),
           "2bpp transparency test reads framebuffer planes");
    expect((p0 & 0x80) == 0 && (p1 & 0x80) == 0,
           "2bpp PLOT with COLR=04 is not suppressed and writes zero into the active two planes");
    auto ditherRom = makeSuperFXRom();
    ditherRom[0x0000] = 0x3d;
    ditherRom[0x0001] = 0x4e;
    ditherRom[0x0002] = 0x00;
    ditherRom[0x0010] = 0x4e;
    ditherRom[0x0011] = 0x4c;
    ditherRom[0x0012] = 0x4c;
    ditherRom[0x0013] = 0x00;
    Cartridge dither;
    dither.setSRAMPersistence(false);
    expect(dither.loadData(std::move(ditherRom), "/tmp/phase_gsu_plot_dither_transparency.sfc"),
           "PLOT dither/transparency ROM loads");
    writeReg(dither, 0, 0x0002);
    startProgram(dither, 0x0000);
    runProgram(dither, 20000);
    writeReg(dither, 0, 0x00f0);
    writeReg(dither, 1, 0x0000);
    writeReg(dither, 2, 0x0000);
    startProgram(dither, 0x0010);
    expect(dither.writeHardware(0x00303a, 0x19), "dither test selects 4bpp plus RON/RAN");
    runProgram(dither, 20000);
    uint8_t d0=0,d1=0,d2=0,d3=0;
    expect(dither.readHardware(0x700000, d0) && dither.readHardware(0x700001, d1) &&
           dither.readHardware(0x700010, d2) && dither.readHardware(0x700011, d3),
           "dither test reads four bitplanes");
    expect((d0 & 0x80) == 0 && (d1 & 0x80) == 0 && (d2 & 0x80) == 0 && (d3 & 0x80) == 0,
           "dither low-nibble color zero leaves x=0 transparent");
    expect((d0 & 0x40) == 0 && (d1 & 0x40) == 0 && (d2 & 0x40) == 0 && (d3 & 0x40) == 0,
           "undithered COLR transparency suppresses x=1 before high-nibble color F is selected");
    auto colorRom = makeSuperFXRom();
    colorRom[0x0010] = 0x4e;
    colorRom[0x0011] = 0x00;
    colorRom[0x0020] = 0x3d;
    colorRom[0x0021] = 0x4e;
    colorRom[0x0022] = 0x00;
    colorRom[0x0030] = 0x4e;
    colorRom[0x0031] = 0x4c;
    colorRom[0x0032] = 0x00;
    Cartridge color;
    color.setSRAMPersistence(false);
    expect(color.loadData(std::move(colorRom), "/tmp/phase_gsu_color_hn_semantics.sfc"),
           "COLOR HN/FH semantics ROM loads");
    writeReg(color, 0, 0x0050);
    startProgram(color, 0x0010);
    runProgram(color, 20000);
    writeReg(color, 0, 0x0004);
    startProgram(color, 0x0020);
    runProgram(color, 20000);
    writeReg(color, 0, 0x00a0);
    writeReg(color, 1, 0x0000);
    writeReg(color, 2, 0x0000);
    expect(color.writeHardware(0x00303a, 0x1b), "8bpp COLOR test selects mode 3 with ROM/RAM grants");
    expect(color.writeHardware(0x00301e, 0x30), "8bpp COLOR test R15 low routed");
    expect(color.writeHardware(0x00301f, 0x00), "8bpp COLOR test starts GSU");
    runProgram(color, 20000);
    expect(color.writeHardware(0x00303a, 0x03), "8bpp COLOR test returns buses to S-CPU");
    uint8_t p4 = 0, p5 = 0, p6 = 0, p7 = 0;
    expect(color.readHardware(0x700020, p4) && color.readHardware(0x700021, p5) &&
           color.readHardware(0x700030, p6) && color.readHardware(0x700031, p7),
           "8bpp COLOR test reads high framebuffer planes");
    expect((p4 & 0x80) != 0 && (p5 & 0x80) == 0 && (p6 & 0x80) != 0 && (p7 & 0x80) == 0,
           "COLOR HN preserves old COLR high nibble and loads source high nibble into COLR low nibble");
}
void testAlternateMemoryAndImmediateForms() {
    auto rom = makeSuperFXRom();
    std::size_t i=0;
    rom[i++]=0xf3; rom[i++]=0xef; rom[i++]=0xbe;
    rom[i++]=0x3e; rom[i++]=0xf3; rom[i++]=0x40; rom[i++]=0x00;
    rom[i++]=0x3d; rom[i++]=0xf4; rom[i++]=0x40; rom[i++]=0x00;
    rom[i++]=0x00;
    Cartridge cart;
    cart.setSRAMPersistence(false);
    expect(cart.loadData(std::move(rom), "/tmp/phase_big_mem.sfc"), "alternate memory test ROM loads");
    startProgram(cart,0);
    runProgram(cart, 20000);
    expect(cart.writeHardware(0x00303a, 0x00), "memory test returns RAM bus to S-CPU");
    uint8_t lo=0,hi=0;
    expect(cart.readHardware(0x700040,lo), "SM low byte readable");
    expect(cart.readHardware(0x700041,hi), "SM high byte readable");
    expect(lo==0xef && hi==0xbe, "ALT2 SM stores immediate-loaded word");
    expect(readReg(cart,4)==0xbeef, "ALT1 LM reloads stored word");
}
void testBusOwnershipAndWait() {
    auto rom = makeSuperFXRom();
    rom[0] = 0x70;
    rom[1] = 0x00;
    rom[0x10] = 0xA5;
    Cartridge cart;
    cart.setSRAMPersistence(false);
    expect(cart.loadData(std::move(rom), "/tmp/phase125_gsu_bus.sfc"), "GSU bus ownership ROM loads");
    uint8_t hiMirror = 0, loMirror = 0;
    expect(cart.readHardware(0x400010, 0, hiMirror), "SuperFX full-bank ROM mirror is device-routed");
    expect(hiMirror == 0xA5, "$40:0010 maps to physical ROM $000010");
    uint8_t c0Mirror = 0;
    expect(cart.readHardware(0xC00010, 0, c0Mirror), "SuperFX $C0-$DF full-bank ROM mirror is device-routed");
    expect(c0Mirror == 0xA5, "$C0:0010 mirrors physical ROM $000010");
    expect(cart.writeHardware(0xF00020, 0x5C), "SuperFX $F0-$F1 RAM mirror write is routed");
    uint8_t f0Mirror = 0;
    expect(cart.readHardware(0x700020, 0, f0Mirror), "canonical SuperFX RAM readable after $F0 mirror write");
    expect(f0Mirror == 0x5C, "$F0:0020 mirrors $70:0020");
    writeReg(cart, 7, 0xC000);
    writeReg(cart, 8, 0x3000);
    expect(cart.writeHardware(0x00301E, 0x00), "GSU wait test R15 low routed");
    expect(cart.writeHardware(0x00301F, 0x00), "GSU wait test starts");
    clockSliced(cart, 10000);
    expect(readReg(cart, 0) == 0x0000, "GSU waits while S-CPU owns ROM bus");
    expect(cart.writeHardware(0x00303A, 0x10), "RON grants ROM to GSU");
    uint8_t dummy = 0xff;
    expect(cart.readHardware(0x008004, 0x77, dummy), "denied S-CPU ROM read is intercepted");
    expect(dummy == 0x04, "denied S-CPU ROM read returns SuperFX dummy byte");
    clockSliced(cart, 10000);
    expect(readReg(cart, 0) == 0xC030, "GSU resumes when ROM bus is granted");
    expect(cart.writeHardware(0x00303A, 0x08), "RAN grants RAM to GSU");
    expect(cart.writeHardware(0x003034, 0x00), "PBR rewrite invalidates cache before ROM-wait test");
    expect(cart.writeHardware(0x00301E, 0x00), "RAN wait R15 low routed");
    expect(cart.writeHardware(0x00301F, 0x00), "RAN wait starts GSU");
    clockSliced(cart, 1000);
    uint8_t runningSfr = 0;
    expect(cart.readHardware(0x003030, runningSfr), "RAN wait SFR readable");
    expect((runningSfr & 0x20) != 0, "GSU remains running while waiting for ROM");
    uint8_t blocked = 0;
    expect(cart.readHardware(0x700000, 0x5A, blocked), "denied S-CPU RAM read is intercepted");
    expect(blocked == 0x5A, "running GSU denied RAM read returns open bus");
    expect(cart.writeHardware(0x700000, 0xCC), "running GSU host RAM write still reaches Game Pak RAM");
    expect(cart.writeHardware(0x00303A, 0x00), "RAM returned to S-CPU");
    uint8_t ram = 0xff;
    expect(cart.readHardware(0x700000, ram), "S-CPU RAM readable after RAN release");
    expect(ram == 0xCC, "host RAM write during GO+RAN remains visible after release");
    rom = makeSuperFXRom();
    rom[0] = 0x00;
    rom[0x10] = 0xA5;
    Cartridge stopped;
    stopped.setSRAMPersistence(false);
    expect(stopped.loadData(std::move(rom), "/tmp/phase207_gsu_stopped_bus.sfc"), "stopped ownership ROM loads");
    expect(stopped.writeHardware(0x00303A, 0x18), "stopped ownership grants RON/RAN");
    expect(stopped.writeHardware(0x00301E, 0x00), "stopped ownership R15 low routed");
    expect(stopped.writeHardware(0x00301F, 0x00), "stopped ownership starts GSU");
    clockSliced(stopped, 1000);
    uint8_t stoppedSfr = 0;
    expect(stopped.readHardware(0x003030, stoppedSfr), "stopped ownership SFR readable");
    expect((stoppedSfr & 0x20) == 0, "STOP clears SFR.G");
    uint8_t stoppedRom = 0;
    expect(stopped.readHardware(0x400010, 0x77, stoppedRom), "stopped GSU ROM is host-visible despite RON");
    expect(stoppedRom == 0xA5, "stopped GSU does not deny host ROM");
    expect(stopped.writeHardware(0x700000, 0xCC), "stopped GSU RAM write reaches Game Pak RAM despite RAN");
    uint8_t stoppedRam = 0;
    expect(stopped.readHardware(0x700000, 0, stoppedRam), "stopped GSU RAM is host-visible despite RAN");
    expect(stoppedRam == 0xCC, "stopped GSU does not deny host RAM");
}
void testIrqAndRevision() {
    auto rom = makeSuperFXRom();
    rom[0] = 0x00;
    Cartridge cart;
    cart.setSRAMPersistence(false);
    expect(cart.loadData(std::move(rom), "/tmp/phase116_gsu1.sfc"), "GSU-1 sized ROM loads");
    uint8_t vcr = 0;
    expect(cart.readHardware(0x00303B, vcr), "GSU VCR readable");
    expect(vcr == 0x04, "unidentified 1 MiB GSU-1 image reports the measured GSU-1 VCR code 04");
    auto starFoxRom = makeSuperFXRom();
    constexpr std::size_t sfHeader = 0x7FC0;
    for (std::size_t i = 0; i < 21; ++i) starFoxRom[sfHeader + i] = ' ';
    const char* sfTitle = "BOARD-ID TEST";
    for (std::size_t i = 0; sfTitle[i]; ++i) starFoxRom[sfHeader + i] = static_cast<uint8_t>(sfTitle[i]);
    write16(starFoxRom, sfHeader + 0x1E, 0xF930);
    Cartridge starFox;
    starFox.setSRAMPersistence(false);
    expect(starFox.loadData(std::move(starFoxRom), "/tmp/phase1_mc1_identity.sfc"), "MC1 board identity ROM loads");
    expect(starFox.readHardware(0x00303B, vcr) && vcr == 0x01,
           "checksum-identified MC1 board selects measured VCR revision");
    expect(starFox.writeHardware(0x700000, 0x11) && starFox.writeHardware(0x708000, 0x22),
           "Star Fox 32KiB work-RAM mirror writes are routed");
    uint8_t sfRam = 0;
    expect(starFox.readHardware(0x700000, sfRam) && sfRam == 0x22,
           "Star Fox MC1 uses 32KiB Game Pak RAM mirrored at +0x8000");
    startProgram(cart, 0);
    expect(!cart.hardwareIrqPending(), "GSU IRQ is clear before STOP executes");
    runProgram(cart);
    expect(cart.hardwareIrqPending(), "STOP raises cartridge IRQ when CFGR IRQ mask is clear");
    uint8_t sfr = 0;
    expect(cart.readHardware(0x003030, sfr), "SFR low read succeeds");
    expect(cart.hardwareIrqPending(), "SFR low read does not acknowledge GSU IRQ");
    uint8_t sfrHi = 0;
    expect(cart.readHardware(0x003031, sfrHi), "SFR high read succeeds");
    expect(!cart.hardwareIrqPending(), "SFR high read acknowledges GSU IRQ");
    auto maskedRom = makeSuperFXRom();
    maskedRom[0] = 0x00;
    Cartridge masked;
    masked.setSRAMPersistence(false);
    expect(masked.loadData(std::move(maskedRom), "/tmp/phase_gsu_irq_mask_status.sfc"),
           "GSU masked IRQ ROM loads");
    expect(masked.writeHardware(0x003037, 0x80), "CFGR IRQ mask write routed");
    startProgram(masked, 0);
    runProgram(masked);
    expect(!masked.hardwareIrqPending(), "CFGR.7 masks the cartridge IRQ line after STOP");
    uint8_t maskedHi = 0;
    expect(masked.readHardware(0x003031, maskedHi) && (maskedHi & 0x80) == 0,
           "CFGR.7 inhibits STOP IRQ generation instead of leaving latent SFR.IRQ status");
    auto rom2 = makeSuperFXRom();
    rom2.resize(2 * 1024 * 1024);
    rom2[0x7FC0 + 0x17] = 0x0B;
    Cartridge cart2;
    cart2.setSRAMPersistence(false);
    expect(cart2.loadData(std::move(rom2), "/tmp/phase116_gsu2.sfc"), "GSU-2 sized ROM loads");
    expect(cart2.readHardware(0x00303B, vcr), "GSU-2 VCR readable");
    expect(vcr == 0x04, "2 MiB SuperFX image reports GSU-2 revision");
}
void testGsu1GamePakRamPersistsThroughCartridgeSram() {
    const auto root = std::filesystem::temp_directory_path() / "snesultimate-gsu1-sram";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    const std::string path = (root / "game.sfc").string();
    Cartridge writer;
    expect(writer.loadData(makeSuperFXRom(), path), "GSU1 SRAM persistence ROM loads");
    expect(writer.writeHardware(0x700020, 0x5a), "GSU1 Game Pak RAM write is routed");
    expect(writer.saveSRAM(), "GSU1 Game Pak RAM saves through cartridge SRAM");
    Cartridge reader;
    expect(reader.loadData(makeSuperFXRom(), path), "GSU1 SRAM persistence ROM reloads");
    uint8_t restored = 0;
    expect(reader.readHardware(0x700020, restored), "GSU1 Game Pak RAM read is routed after reload");
    expect(restored == 0x5a, "GSU1 Game Pak RAM is restored from cartridge SRAM");
    std::filesystem::remove_all(root, ec);
}
void testGsu2GamePakRamPersistsThroughCartridgeSram() {
    const auto root = std::filesystem::temp_directory_path() / "snesultimate-gsu2-sram";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    const std::string path = (root / "game.sfc").string();
    auto rom = makeSuperFXRom();
    rom.resize(2 * 1024 * 1024, 0);
    rom[0x7FC0 + 0x17] = 0x0B;
    Cartridge writer;
    expect(writer.loadData(rom, path), "GSU2 SRAM persistence ROM loads");
    expect(writer.hardwareType() == CartridgeHardware::SuperFX2, "2 MiB cartridge selects GSU2");
    expect(writer.writeHardware(0x700020, 0xa5), "GSU2 Game Pak RAM write is routed");
    expect(writer.saveSRAM(), "GSU2 Game Pak RAM saves through cartridge SRAM");
    Cartridge reader;
    expect(reader.loadData(std::move(rom), path), "GSU2 SRAM persistence ROM reloads");
    uint8_t restored = 0;
    expect(reader.readHardware(0x700020, restored), "GSU2 Game Pak RAM read is routed after reload");
    expect(restored == 0xa5, "GSU2 Game Pak RAM is restored from cartridge SRAM");
    std::filesystem::remove_all(root, ec);
}
void testGsuProfileDetectionAndBackupRamProtection() {
    constexpr std::size_t header = 0x7FC0;
    auto doom = makeSuperFXRom();
    doom[header + 0x16] = 0x15;
    Cartridge knownGsu2;
    knownGsu2.setSRAMPersistence(false);
    expect(knownGsu2.loadData(std::move(doom), "/tmp/known_gsu2.sfc"), "GSU2-header ROM loads");
    expect(knownGsu2.hardwareType() == CartridgeHardware::SuperFX2, "GSU2 cartridge metadata selects GSU2 without relying on title or padded image size");
    auto paddedGsu1 = makeSuperFXRom();
    paddedGsu1.resize(2 * 1024 * 1024, 0);
    Cartridge gsu1;
    gsu1.setSRAMPersistence(false);
    expect(gsu1.loadData(std::move(paddedGsu1), "/tmp/padded_gsu1.sfc"), "padded GSU1 image loads");
    expect(gsu1.hardwareType() == CartridgeHardware::SuperFX, "padded image does not override declared GSU1 ROM metadata");
    Cartridge backupRam;
    backupRam.setSRAMPersistence(false);
    expect(backupRam.loadData(makeSuperFXRom(), "/tmp/gsu_backup_ram.sfc"), "GSU backup RAM test ROM loads");
    uint8_t value = 0;
    expect(backupRam.writeHardware(0x7C0020, 0x5A), "unprotected GSU backup RAM is writable");
    expect(backupRam.readHardware(0x7C0020, value) && value == 0x5A, "GSU backup RAM write is visible");
    expect(backupRam.writeHardware(0x003033, 0x01), "BRAMR protection write is routed");
    expect(backupRam.writeHardware(0x7C0020, 0xA5), "protected GSU backup RAM write is consumed");
    expect(backupRam.readHardware(0x7C0020, value) && value == 0x5A, "BRAMR protects GSU backup RAM contents");
    backupRam.reset();
    expect(backupRam.writeHardware(0x7C0020, 0xA5), "reset clears BRAMR protection");
    expect(backupRam.readHardware(0x7C0020, value) && value == 0xA5, "backup RAM is writable after reset");
}
}
int main() {
    testInstructionCacheMissTiming();
    testBranchDelaySlotPreservesWithPrefix();
    testR14RomTransferIsPending();
    testSignedBranchPredicates();
    testMergeAndFlags();
    testPlotRpixAndModes();
    testPlotClipsToConfiguredScreenHeight();
    testPlotTransparencyAndColorNibbleSemantics();
    testAlternateMemoryAndImmediateForms();
    testBusOwnershipAndWait();
    testIrqAndRevision();
    testGsu1GamePakRamPersistsThroughCartridgeSram();
    testGsu2GamePakRamPersistsThroughCartridgeSram();
    testGsuProfileDetectionAndBackupRamProtection();
    testRambMasksToSingleBankBit();
    testRunningHostRegisterAccess();
    testCacheWindowUsesCbrRotation();
    testPendingBusOperationMaturesInsideLongInstruction();
    if (failures) {
        std::cerr << failures << " SuperFX instruction regression(s) failed\n";
        return 1;
    }
    std::cout << "SuperFX instruction conformance regression passed\n";
    return 0;
}

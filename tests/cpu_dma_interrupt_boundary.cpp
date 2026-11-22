#include "snes.hpp"
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
namespace {
std::vector<uint8_t> makeRom() {
    std::vector<uint8_t> rom(0x10000, 0xEA);
    rom[0x0000] = 0x58;
    rom[0x0001] = 0xEA;
    rom[0x0002] = 0xEA;
    rom[0x1000] = 0x40;
    rom[0x1020] = 0x40;
    constexpr std::size_t h = 0x7FC0;
    const char title[] = "CPU DMA IRQ BOUNDARY ";
    for (std::size_t i = 0; i < 21; ++i) rom[h + i] = static_cast<uint8_t>(title[i]);
    rom[h + 0x15] = 0x20;
    rom[h + 0x17] = 0x06;
    rom[h + 0x19] = 0x01;
    rom[0x7FFA] = 0x00; rom[0x7FFB] = 0x90;
    rom[0x7FFC] = 0x00; rom[0x7FFD] = 0x80;
    rom[0x7FFE] = 0x20; rom[0x7FFF] = 0x90;
    return rom;
}
std::filesystem::path writeRom() {
    const auto path = std::filesystem::temp_directory_path() / "snesultimate_phase1s_irq_dma.sfc";
    auto rom = makeRom();
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(rom.data()), static_cast<std::streamsize>(rom.size()));
    return path;
}
void configureOneByteDMA(SNES& snes) {
    snes.bus().write(0x7E0000, 0x5A);
    snes.bus().write(0x004300, 0x00);
    snes.bus().write(0x004301, 0x00);
    snes.bus().write(0x004302, 0x00);
    snes.bus().write(0x004303, 0x00);
    snes.bus().write(0x004304, 0x7E);
    snes.bus().write(0x004305, 0x01);
    snes.bus().write(0x004306, 0x00);
    snes.bus().write(0x00420B, 0x01);
}
template<class Predicate>
void runUntil(SNES& snes, Predicate predicate, uint64_t limit) {
    for (uint64_t i = 0; i < limit && !predicate(); ++i) snes.runCycles(1);
    assert(predicate());
}
}
int main() {
    const auto path = writeRom();
    {
        SNES snes;
        assert(snes.loadROM(path.string()));
        snes.runCycles(14);
        assert(snes.cpu().pc() == 0x8001);
        assert((snes.cpu().p() & Flag_I) == 0);
        configureOneByteDMA(snes);
        assert(snes.dma().dmaActive());
        snes.cpu().setIRQLine(true);
        const uint16_t stalled_pc = snes.cpu().pc();
        snes.runCycles(20);
        assert(snes.dma().dmaActive());
        assert(snes.cpu().pc() == stalled_pc);
        runUntil(snes, [&] { return !snes.dma().dmaActive(); }, 64);
        runUntil(snes, [&] { return snes.cpu().pc() == 0x9020; }, 160);
        assert(snes.bus().read(0x0001FF) == 0x80);
        assert(snes.bus().read(0x0001FE) == 0x02);
        snes.cpu().setIRQLine(false);
    }
    {
        SNES snes;
        assert(snes.loadROM(path.string()));
        snes.runCycles(14);
        configureOneByteDMA(snes);
        assert(snes.dma().dmaActive());
        const uint16_t stalled_pc = snes.cpu().pc();
        snes.cpu().requestNMI();
        assert(snes.cpu().nmiPending());
        snes.runCycles(20);
        assert(snes.dma().dmaActive());
        assert(snes.cpu().pc() == stalled_pc);
        assert(snes.cpu().nmiPending());
        runUntil(snes, [&] { return !snes.dma().dmaActive(); }, 64);
        runUntil(snes, [&] { return snes.cpu().pc() == 0x9000; }, 160);
        assert(!snes.cpu().nmiPending());
    }
    {
        SNES snes;
        assert(snes.loadROM(path.string()));
        snes.bus().write(0x004300, 0x00);
        snes.bus().write(0x004302, 0x00);
        snes.bus().write(0x004303, 0x00);
        snes.bus().write(0x004304, 0x7E);
        snes.bus().write(0x7E0000, 0x00);
        snes.bus().write(0x00420C, 0x01);
        runUntil(snes, [&] { return snes.dma().hdmaActive(); }, 64);
        const uint16_t pc_before_nmi = snes.cpu().pc();
        snes.cpu().requestNMI();
        assert(snes.cpu().nmiPending());
        for (int i = 0; i < 8 && snes.dma().hdmaActive(); ++i) {
            snes.runCycles(1);
            assert(snes.cpu().pc() == pc_before_nmi);
            assert(snes.cpu().nmiPending());
        }
        runUntil(snes, [&] { return !snes.dma().hdmaActive(); }, 80);
        runUntil(snes, [&] { return snes.cpu().pc() == 0x9000; }, 160);
        assert(!snes.cpu().nmiPending());
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::cout << "CPU DMA/HDMA interrupt-boundary regression passed\n";
    return 0;
}

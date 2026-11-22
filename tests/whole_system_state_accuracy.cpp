#include "snes.hpp"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>
namespace {
int failures = 0;
void expect(bool cond, const char* msg) {
    if (!cond) { std::cerr << "FAIL: " << msg << '\n'; ++failures; }
}
void write16(std::vector<uint8_t>& rom, std::size_t off, uint16_t value) {
    rom[off] = static_cast<uint8_t>(value);
    rom[off + 1] = static_cast<uint8_t>(value >> 8);
}
std::vector<uint8_t> makeRom() {
    std::vector<uint8_t> rom(1024 * 1024, 0xEA);
    rom[0] = 0xE6; rom[1] = 0x00; rom[2] = 0x80; rom[3] = 0xFC;
    const std::size_t h = 0x7FC0;
    const char title[] = "PHASE212 STATE TEST";
    std::fill(rom.begin() + h, rom.begin() + h + 21, ' ');
    std::copy(title, title + sizeof(title) - 1, rom.begin() + h);
    rom[h + 0x15] = 0x20;
    rom[h + 0x16] = 0x02;
    rom[h + 0x17] = 0x0A;
    rom[h + 0x18] = 0x05;
    rom[h + 0x19] = 0x01;
    write16(rom, h + 0x1C, 0xEDCB);
    write16(rom, h + 0x1E, 0x1234);
    write16(rom, h + 0x3C, 0x8000);
    return rom;
}
uint64_t framebufferHash(const SNES& , const PPU& ppu) {
    const uint32_t* fb = ppu.framebuffer();
    const std::size_t count = static_cast<std::size_t>(ppu.framebufferStride()) * 239;
    uint64_t h = 1469598103934665603ull;
    for (std::size_t i = 0; i < count; ++i) {
        uint32_t v = fb[i];
        for (int b = 0; b < 4; ++b) { h ^= static_cast<uint8_t>(v); h *= 1099511628211ull; v >>= 8; }
    }
    return h;
}
}
int main() {
    SNES snes;
    snes.cartridge().setSRAMPersistence(false);
    expect(snes.cartridge().loadData(makeRom(), "/tmp/phase212_state.sfc", "state"), "synthetic ROM loads");
    snes.reset();
    snes.setButton(0, Controller::A, true);
    snes.bus().write(0x4016, 1);
    snes.bus().write(0x4016, 0);
    (void)snes.bus().read(0x4016);
    for (unsigned i = 0; i < 128; ++i) snes.bus().write(0x7E0100 + i, static_cast<uint8_t>(i * 3 + 1));
    snes.bus().write(0x4300, 0x00);
    snes.bus().write(0x4301, 0x22);
    snes.bus().write(0x4302, 0x00);
    snes.bus().write(0x4303, 0x01);
    snes.bus().write(0x4304, 0x7E);
    snes.bus().write(0x4305, 0x80);
    snes.bus().write(0x4306, 0x00);
    snes.bus().write(0x420B, 0x01);
    snes.cartridge().write_sram(0x123, 0x5A);
    snes.runCycles(37);
    std::vector<uint8_t> checkpoint = snes.saveState();
    expect(!checkpoint.empty(), "whole-system checkpoint is produced mid-frame");
    expect(snes.scheduler().now() != 0, "checkpoint retains non-zero master time");
    const uint64_t checkpoint_time = snes.scheduler().now();
    constexpr uint64_t replay_clocks = 50000;
    snes.runCycles(replay_clocks);
    const CPU::RuntimeState cpu_a = snes.cpu().runtimeState();
    const int scanline_a = snes.ppu().scanline();
    const int dot_a = snes.ppu().dot();
    const uint64_t frame_a = snes.ppu().frameCounter();
    const uint64_t fb_a = framebufferHash(snes, snes.ppu());
    const uint8_t wram_a = snes.bus().wram()[0];
    const uint8_t sram_a = snes.cartridge().read_sram(0x123);
    const bool dma_a = snes.dma().dmaActive();
    const uint64_t apu_cycles_a = snes.apu().cycles();
    const std::vector<int16_t> audio_a = snes.apu().takeAudioSamples();
    snes.bus().write(0x7E0000, 0xEE);
    snes.cartridge().write_sram(0x123, 0xA5);
    snes.setButton(0, Controller::A, false);
    snes.runCycles(1234);
    expect(snes.loadState(checkpoint), "whole-system checkpoint restores");
    expect(snes.scheduler().now() == checkpoint_time, "master scheduler time restores exactly");
    expect(snes.cartridge().read_sram(0x123) == 0x5A, "cartridge SRAM restores");
    snes.runCycles(replay_clocks);
    const CPU::RuntimeState cpu_b = snes.cpu().runtimeState();
    expect(cpu_b.architectural.pc == cpu_a.architectural.pc && cpu_b.architectural.pb == cpu_a.architectural.pb &&
           cpu_b.architectural.a == cpu_a.architectural.a && cpu_b.architectural.x == cpu_a.architectural.x &&
           cpu_b.architectural.y == cpu_a.architectural.y && cpu_b.cycles == cpu_a.cycles,
           "S-CPU replay is deterministic");
    expect(snes.ppu().scanline() == scanline_a && snes.ppu().dot() == dot_a && snes.ppu().frameCounter() == frame_a,
           "PPU timing position replays exactly");
    expect(framebufferHash(snes, snes.ppu()) == fb_a, "PPU framebuffer replay is deterministic");
    expect(snes.bus().wram()[0] == wram_a, "WRAM replay is deterministic");
    expect(snes.cartridge().read_sram(0x123) == sram_a, "SRAM replay is deterministic");
    expect(snes.dma().dmaActive() == dma_a, "DMA state replay is deterministic");
    expect(snes.apu().cycles() == apu_cycles_a, "APU cycle position replays exactly");
    expect(snes.apu().takeAudioSamples() == audio_a, "queued/generated DSP audio replays exactly");
    checkpoint[0] ^= 0xFF;
    expect(!snes.loadState(checkpoint), "invalid state magic/integrity is rejected");
    {
        auto rom = makeRom();
        rom.resize(2 * 1024 * 1024, 0xEA);
        const std::size_t h = 0x7FC0;
        rom[h + 0x15] = 0x23;
        rom[h + 0x16] = 0x34;
        SNES sa1;
        sa1.cartridge().setSRAMPersistence(false);
        expect(sa1.cartridge().loadData(std::move(rom), "/tmp/phase212_sa1_state.sfc", "sa1-state"), "SA-1 state ROM loads");
        sa1.reset();
        sa1.bus().write(0x002229, 0xFF);
        sa1.bus().write(0x003045, 0xA5);
        const auto sa1_state = sa1.saveState();
        expect(!sa1_state.empty(), "whole-system checkpoint includes SA-1 device state");
        sa1.bus().write(0x003045, 0x22);
        expect(sa1.loadState(sa1_state), "whole-system SA-1 checkpoint restores");
        expect(sa1.bus().read(0x003045) == 0xA5, "SA-1 I-RAM restored through whole-system checkpoint");
    }
    if (failures) {
        std::cerr << failures << " whole-system state regression(s) failed\n";
        return 1;
    }
    std::cout << "whole-system state accuracy regression passed\n";
    return 0;
}

#include "snes.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
namespace {
std::vector<uint8_t> makeLoRom(uint8_t destination) {
    std::vector<uint8_t> rom(0x8000, 0xEA);
    constexpr std::size_t h = 0x7FC0;
    const char title[] = "REGION TIMING TEST   ";
    for (std::size_t i = 0; i < 21; ++i) rom[h + i] = static_cast<uint8_t>(title[i]);
    rom[h + 0x15] = 0x20;
    rom[h + 0x16] = 0x00;
    rom[h + 0x17] = 0x05;
    rom[h + 0x18] = 0x00;
    rom[h + 0x19] = destination;
    rom[h + 0x1C] = 0xCB;
    rom[h + 0x1D] = 0xED;
    rom[h + 0x1E] = 0x34;
    rom[h + 0x1F] = 0x12;
    rom[h + 0x3C] = 0x00;
    rom[h + 0x3D] = 0x80;
    return rom;
}
std::filesystem::path writeRom(const char* name, uint8_t destination) {
    const auto path = std::filesystem::temp_directory_path() / name;
    const auto rom = makeLoRom(destination);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(rom.data()), static_cast<std::streamsize>(rom.size()));
    return path;
}
}
int main() {
    struct RegionCase {
        uint8_t destination;
        bool pal;
        const char* name;
    };
    const RegionCase cases[] = {
        {0x00, false, "Japan"},
        {0x01, false, "North America"},
        {0x02, true,  "Europe"},
        {0x03, true,  "Scandinavia"},
        {0x04, true,  "Finland"},
        {0x05, true,  "Denmark"},
        {0x06, true,  "France"},
        {0x07, true,  "Netherlands"},
        {0x08, true,  "Spain"},
        {0x09, true,  "Germany"},
        {0x0A, true,  "Italy"},
        {0x0B, true,  "China"},
        {0x0C, true,  "Indonesia"},
        {0x0D, false, "South Korea"},
        {0x0E, false, "Common"},
        {0x0F, false, "Canada"},
        {0x10, false, "Brazil / Gateway"},
        {0x11, true,  "Australia"},
        {0x12, false, "Other X"},
        {0x13, false, "Other Y"},
        {0x14, false, "Other Z"},
    };
    for (const RegionCase& tc : cases) {
        const std::string filename = "snesultimate_region_" + std::to_string(tc.destination) + ".sfc";
        const auto path = writeRom(filename.c_str(), tc.destination);
        SNES snes;
        assert(snes.loadROM(path.string()));
        assert(snes.cartridge().isPalRegion() == tc.pal);
        assert(snes.ppu().palRegion() == tc.pal);
        assert(snes.ppu().linesThisFrame() == (tc.pal ? 312 : 262));
        const double expectedRate = tc.pal ? SNES::PAL_FRAME_RATE_HZ : SNES::NTSC_FRAME_RATE_HZ;
        assert(std::abs(snes.nominalFrameRate() - expectedRate) < 1e-9);
        assert(((snes.ppu().readRegister(0x213F) & 0x10) != 0) == tc.pal);
        snes.reset();
        assert(snes.ppu().palRegion() == tc.pal);
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
    std::cout << "Console PAL/NTSC full destination-table regression passed\n";
    return 0;
}

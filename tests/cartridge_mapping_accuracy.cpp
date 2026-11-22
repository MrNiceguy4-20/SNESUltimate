#include "bus.hpp"
#include "cartridge.hpp"
#include <cstdint>
#include <cstdio>
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
    rom[offset] = static_cast<uint8_t>(value & 0xFF);
    rom[offset + 1] = static_cast<uint8_t>(value >> 8);
}
void installHeader(std::vector<uint8_t>& rom, std::size_t header,
                   uint8_t mapMode, uint8_t romSizeExp, uint8_t ramSizeExp,
                   const char* title) {
    for (std::size_t i = 0; i < 21; ++i) rom[header + i] = ' ';
    for (std::size_t i = 0; title[i] && i < 21; ++i) {
        rom[header + i] = static_cast<uint8_t>(title[i]);
    }
    rom[header + 0x15] = mapMode;
    rom[header + 0x16] = ramSizeExp ? 0x02 : 0x00;
    rom[header + 0x17] = romSizeExp;
    rom[header + 0x18] = ramSizeExp;
    rom[header + 0x19] = 0x01;
    write16(rom, header + 0x1C, 0xEDCB);
    write16(rom, header + 0x1E, 0x1234);
    write16(rom, header + 0x3C, 0x8000);
}
void testLoROMAndNonPowerOfTwoMirroring() {
    constexpr std::size_t MiB = 1024 * 1024;
    std::vector<uint8_t> rom(3 * MiB, 0x00);
    std::fill(rom.begin(), rom.begin() + MiB, 0x11);
    std::fill(rom.begin() + MiB, rom.begin() + 2 * MiB, 0x22);
    std::fill(rom.begin() + 2 * MiB, rom.end(), 0x33);
    installHeader(rom, 0x7FC0, 0x20, 0x0C, 0, "PHASE67 LOROM");
    Cartridge cart;
    expect(cart.loadData(std::move(rom), "/tmp/phase67_lorom.sfc"), "LoROM synthetic image loads");
    expect(cart.mapperType() == MapperType::LoROM, "LoROM header is detected");
    Bus bus;
    bus.attachCartridge(&cart);
    expect(bus.read(0x008000) == 0x11, "LoROM $00:8000 maps to ROM offset $000000");
    expect(bus.read(0x408000) == 0x33, "LoROM $40:8000 maps to ROM offset $200000");
    expect(bus.read(0x400000) == 0x33, "LoROM full cart bank lower half mirrors its upper half");
    expect(bus.read(0x608000) == 0x33, "3 MiB LoROM uses mask-ROM mirroring, not modulo wrapping");
}
void testHiROMMapping() {
    constexpr std::size_t MiB = 1024 * 1024;
    std::vector<uint8_t> rom(4 * MiB, 0x00);
    std::fill(rom.begin(), rom.begin() + MiB, 0x41);
    std::fill(rom.begin() + MiB, rom.begin() + 2 * MiB, 0x42);
    std::fill(rom.begin() + 2 * MiB, rom.begin() + 3 * MiB, 0x43);
    std::fill(rom.begin() + 3 * MiB, rom.end(), 0x44);
    installHeader(rom, 0xFFC0, 0x21, 0x0C, 0, "PHASE67 HIROM");
    Cartridge cart;
    expect(cart.loadData(std::move(rom), "/tmp/phase67_hirom.sfc"), "HiROM synthetic image loads");
    expect(cart.mapperType() == MapperType::HiROM, "HiROM header is detected");
    Bus bus;
    bus.attachCartridge(&cart);
    expect(bus.read(0xC00000) == 0x41, "HiROM $C0:0000 starts physical ROM");
    expect(bus.read(0x400000) == 0x41, "HiROM $40:0000 mirrors $C0:0000");
    expect(bus.read(0xD00000) == 0x42, "HiROM bank progression is 64 KiB linear");
    expect(bus.read(0x008000) == 0x41, "HiROM system-bank upper half maps directly");
}
void testExHiROMMappingAndDetection() {
    constexpr std::size_t MiB = 1024 * 1024;
    std::vector<uint8_t> rom(6 * MiB, 0x00);
    for (std::size_t m = 0; m < 6; ++m) {
        std::fill(rom.begin() + m * MiB, rom.begin() + (m + 1) * MiB,
                  static_cast<uint8_t>(0x50 + m));
    }
    installHeader(rom, 0x40FFC0, 0x25, 0x0D, 0, "PHASE67 EXHIROM");
    Cartridge cart;
    expect(cart.loadData(std::move(rom), "/tmp/phase67_exhirom.sfc"), "ExHiROM synthetic image loads");
    expect(cart.mapperType() == MapperType::ExHiROM, "ExHiROM $40FFC0 header is detected");
    expect(cart.mapMode() == 0x25, "ExHiROM map mode metadata is retained");
    Bus bus;
    bus.attachCartridge(&cart);
    expect(bus.read(0xC00000) == 0x50, "ExHiROM $C0:0000 maps to first 4 MiB half");
    expect(bus.read(0xD00000) == 0x51, "ExHiROM normal HiROM bank progression is retained");
    expect(bus.read(0x400000) == 0x54, "ExHiROM $40:0000 maps to physical offset $400000");
    expect(bus.read(0x500000) == 0x55, "ExHiROM extended bank progression reaches physical offset $500000");
    expect(bus.read(0x008000) == 0x54, "ExHiROM $00:8000 maps into the extended half");
    expect(bus.read(0x808000) == 0x50, "ExHiROM $80:8000 maps into the first half");
    expect(bus.read(0x600000) == 0x54, "6 MiB ExHiROM upper hole mirrors the final 2 MiB mask ROM");
}
void testExLoROMMappingAndDetection() {
    constexpr std::size_t MiB = 1024 * 1024;
    std::vector<uint8_t> rom(6 * MiB, 0x00);
    for (std::size_t m = 0; m < 6; ++m) {
        std::fill(rom.begin() + m * MiB, rom.begin() + (m + 1) * MiB,
                  static_cast<uint8_t>(0x60 + m));
    }
    installHeader(rom, 0x407FC0, 0x20, 0x0D, 0, "PHASE212 EXLOROM");
    Cartridge cart;
    expect(cart.loadData(std::move(rom), "/tmp/phase212_exlorom.sfc"), "ExLoROM synthetic image loads");
    expect(cart.mapperType() == MapperType::ExLoROM, "ExLoROM $407FC0 header is detected");
    Bus bus;
    bus.attachCartridge(&cart);
    expect(bus.read(0x808000) == 0x60, "ExLoROM $80:8000 maps to physical offset $000000");
    expect(bus.read(0xA08000) == 0x61, "ExLoROM high-bank progression remains 32 KiB linear");
    expect(bus.read(0x008000) == 0x64, "ExLoROM $00:8000 maps to physical offset $400000");
    expect(bus.read(0x208000) == 0x65, "ExLoROM low-bank extension reaches physical offset $500000");
    expect(bus.read(0x400000) == 0xFF, "ExLoROM lower bank halves are not fabricated as full-bank ROM");
}
void testHiROMFamilySRAM() {
    constexpr std::size_t MiB = 1024 * 1024;
    std::vector<uint8_t> rom(6 * MiB, 0x00);
    installHeader(rom, 0x40FFC0, 0x25, 0x0D, 0x05, "PHASE67 EX SRAM");
    const std::string source = "/tmp/phase67_exhirom_sram.sfc";
    const std::string save = "/tmp/phase67_exhirom_sram.srm";
    std::remove(save.c_str());
    Cartridge cart;
    expect(cart.loadData(std::move(rom), source), "ExHiROM SRAM image loads");
    expect(cart.sramSize() == 32 * 1024, "SRAM exponent decodes to 32 KiB");
    Bus bus;
    bus.attachCartridge(&cart);
    bus.write(0x206000, 0xA5);
    expect(bus.read(0x206000) == 0xA5, "ExHiROM SRAM is writable at $20:6000");
    expect(bus.read(0xA06000) == 0xA5, "ExHiROM SRAM mirrors into $A0:6000");
    std::remove(save.c_str());
}
}
int main() {
    testLoROMAndNonPowerOfTwoMirroring();
    testHiROMMapping();
    testExHiROMMappingAndDetection();
    testExLoROMMappingAndDetection();
    testHiROMFamilySRAM();
    if (failures != 0) {
        std::cerr << failures << " cartridge mapping regression(s) failed\n";
        return 1;
    }
    std::cout << "cartridge mapping accuracy regression passed\n";
    return 0;
}

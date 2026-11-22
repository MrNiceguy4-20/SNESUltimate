#include "bus.hpp"
#include "cartridge.hpp"
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
std::vector<uint8_t> makeRom(std::size_t header, uint8_t mapMode, uint8_t romType,
                             uint8_t romSizeExp = 0x0A, uint8_t ramSizeExp = 0x03,
                             uint8_t company = 0x01) {
    const std::size_t size = header >= 0xFFC0 ? 2 * 1024 * 1024 : 1024 * 1024;
    std::vector<uint8_t> rom(size, 0);
    const char* title = "PHASE109 HW TEST";
    for (std::size_t i = 0; i < 21; ++i) rom[header + i] = ' ';
    for (std::size_t i = 0; title[i] && i < 21; ++i) rom[header + i] = static_cast<uint8_t>(title[i]);
    rom[header + 0x15] = mapMode;
    rom[header + 0x16] = romType;
    rom[header + 0x17] = romSizeExp;
    rom[header + 0x18] = ramSizeExp;
    rom[header + 0x19] = 0x01;
    rom[header + 0x1A] = company;
    write16(rom, header + 0x1C, 0xEDCB);
    write16(rom, header + 0x1E, 0x1234);
    write16(rom, header + 0x3C, 0x8000);
    return rom;
}
void expectHardware(std::size_t header, uint8_t mapMode, uint8_t romType,
                    CartridgeHardware expected, const char* label,
                    uint8_t romSizeExp = 0x0A, uint8_t company = 0x01) {
    Cartridge cart;
    cart.setSRAMPersistence(false);
    auto rom = makeRom(header, mapMode, romType, romSizeExp, 0x03, company);
    expect(cart.loadData(std::move(rom), std::string("/tmp/") + label + ".sfc"), "synthetic hardware ROM loads");
    if (cart.hardwareType() != expected) {
        std::cerr << "FAIL: " << label << " detected as " << cart.hardwareName() << '\n';
        ++failures;
    }
}
void testDetectionMatrix() {
    expectHardware(0x7FC0, 0x23, 0x34, CartridgeHardware::SA1, "sa1");
    expectHardware(0x7FC0, 0x20, 0x13, CartridgeHardware::SuperFX, "superfx");
    expectHardware(0x7FC0, 0x32, 0x43, CartridgeHardware::SDD1, "sdd1");
    expectHardware(0xFFC0, 0x3A, 0xF5, CartridgeHardware::SPC7110, "spc7110");
    expectHardware(0xFFC0, 0x3A, 0xF9, CartridgeHardware::SPC7110RTC, "spc7110rtc");
    expectHardware(0x7FC0, 0x30, 0x25, CartridgeHardware::OBC1, "obc1");
    expectHardware(0x7FC0, 0x20, 0xF3, CartridgeHardware::CX4, "cx4");
    expectHardware(0x7FC0, 0x35, 0x55, CartridgeHardware::SRTC, "srtc");
    expectHardware(0x7FC0, 0x20, 0x03, CartridgeHardware::DSP1, "dsp1");
    expectHardware(0x7FC0, 0x20, 0x05, CartridgeHardware::DSP2, "dsp2");
    expectHardware(0x7FC0, 0x30, 0x05, CartridgeHardware::DSP3, "dsp3", 0x0A, 0xB2);
    expectHardware(0x7FC0, 0x30, 0x03, CartridgeHardware::DSP4, "dsp4");
    expectHardware(0x7FC0, 0x30, 0xF6, CartridgeHardware::ST010, "st010", 0x08);
    expectHardware(0x7FC0, 0x30, 0xF6, CartridgeHardware::ST011, "st011", 0x09);
    expectHardware(0x7FC0, 0x30, 0xF5, CartridgeHardware::ST018, "st018");
}
void testSupportLevels() {
    struct Case { uint8_t map, type, size, company; CartridgeSupportLevel level; const char* name; };
    const Case cases[] = {
        {0x20,0xF3,0x0A,0x01,CartridgeSupportLevel::PartialHLE,"CX4"},
        {0x20,0x03,0x0A,0x01,CartridgeSupportLevel::PartialHLE,"DSP1"},
        {0x30,0x05,0x0A,0xB2,CartridgeSupportLevel::PartialHLE,"DSP3"},
        {0x30,0x03,0x0A,0x01,CartridgeSupportLevel::PartialHLE,"DSP4"},
        {0x30,0xF6,0x08,0x01,CartridgeSupportLevel::PartialHLE,"ST010"},
        {0x30,0xF6,0x09,0x01,CartridgeSupportLevel::PartialHLE,"ST011"},
        {0x30,0xF5,0x09,0x01,CartridgeSupportLevel::PartialHLE,"ST018"},
    };
    for (const auto& tc : cases) {
        Cartridge cart; cart.setSRAMPersistence(false);
        auto rom = makeRom(0x7FC0, tc.map, tc.type, tc.size, 0x03, tc.company);
        expect(cart.loadData(std::move(rom), std::string("/tmp/support_") + tc.name + ".sfc"), "support-level ROM loads");
        expect(cart.hardwareSupportLevel() == tc.level, "HLE coprocessor reports partial support instead of full implementation");
        expect(cart.hardwareImplemented(), "partial/HLE coprocessor remains runnable");
    }
}
void testRejectsWrongFirmwareIdentity() {
    const std::filesystem::path dir = "/tmp/snesultimate_bad_firmware";
    std::filesystem::create_directories(dir);
    {
        std::ofstream p(dir / "dsp1.program.rom", std::ios::binary);
        std::vector<uint8_t> bytes(0x1800, 0x00); p.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
    {
        std::ofstream d(dir / "dsp1.data.rom", std::ios::binary);
        std::vector<uint8_t> bytes(0x0800, 0xFF); d.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
#if defined(_WIN32)
    _putenv_s("SNESULTIMATE_FIRMWARE_DIR", dir.string().c_str());
#else
    setenv("SNESULTIMATE_FIRMWARE_DIR", dir.string().c_str(), 1);
#endif
    Cartridge cart; cart.setSRAMPersistence(false);
    auto rom = makeRom(0x7FC0, 0x20, 0x03);
    expect(cart.loadData(std::move(rom), "/tmp/firmware_identity_dsp1.sfc"), "DSP1 ROM loads with bad sidecar present");
    expect(cart.hardwareSupportLevel() == CartridgeSupportLevel::PartialHLE,
           "wrong-size-correct DSP firmware is rejected instead of advertised as FirmwareLLE");
#if defined(_WIN32)
    _putenv_s("SNESULTIMATE_FIRMWARE_DIR", "");
#else
    unsetenv("SNESULTIMATE_FIRMWARE_DIR");
#endif
    std::filesystem::remove_all(dir);
}
void testOBC1Routing() {
    Cartridge cart;
    cart.setSRAMPersistence(false);
    auto rom = makeRom(0x7FC0, 0x30, 0x25, 0x0A, 0x03);
    expect(cart.loadData(std::move(rom), "/tmp/phase109_obc1.sfc"), "OBC1 synthetic ROM loads");
    expect(cart.hardwareType() == CartridgeHardware::OBC1, "OBC1 header is recognized");
    expect(cart.hardwareImplemented(), "OBC1 is reported implemented");
    expect(cart.sramSize() >= 0x2000, "OBC1 has its 8 KiB RAM window");
    Bus bus;
    bus.attachCartridge(&cart);
    bus.write(0x007FF5, 0x01);
    bus.write(0x007FF6, 0x05);
    bus.write(0x007FF0, 0x12);
    bus.write(0x007FF1, 0x34);
    bus.write(0x007FF2, 0x56);
    bus.write(0x007FF3, 0x78);
    expect(bus.read(0x807FF0) == 0x12, "OBC1 object byte 0 is bank-mirrored");
    expect(bus.read(0x007FF3) == 0x78, "OBC1 object byte 3 round-trips");
    expect(bus.read(0x007814) == 0x12, "OBC1 virtual object register writes backing RAM");
    bus.write(0x007FF4, 0x03);
    const uint8_t packed = bus.read(0x007FF4);
    expect((packed & 0x0C) == 0x0C, "OBC1 packed attribute register updates selected two-bit field");
    bus.write(0x006123, 0xA5);
    expect(bus.read(0x806123) == 0xA5, "OBC1 ordinary RAM window is bank-mirrored");
    const uint32_t tag = cart.hardwareStateTag();
    const auto state = cart.saveHardwareState();
    expect(tag != 0 && !state.empty(), "OBC1 exposes checkpoint state");
    bus.write(0x007FF5, 0x00);
    bus.write(0x007FF6, 0x02);
    expect(cart.loadHardwareState(tag, state), "OBC1 checkpoint restores selector state");
    expect(bus.read(0x007FF0) == 0x12, "OBC1 restored object/base selector resumes same virtual object");
}
}
int main() {
    testDetectionMatrix();
    testSupportLevels();
    testRejectsWrongFirmwareIdentity();
    testOBC1Routing();
    if (failures != 0) {
        std::cerr << failures << " cartridge hardware regression(s) failed\n";
        return 1;
    }
    std::cout << "cartridge hardware accuracy regression passed\n";
    return 0;
}

#include "bus.hpp"
#include "cartridge.hpp"
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
namespace {
int failures = 0;
void expect(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
void put16(std::vector<uint8_t>& rom, std::size_t off, uint16_t value) {
    rom[off] = static_cast<uint8_t>(value);
    rom[off + 1] = static_cast<uint8_t>(value >> 8);
}
void put32(std::vector<uint8_t>& data, std::size_t off, uint32_t value) {
    data[off] = static_cast<uint8_t>(value);
    data[off + 1] = static_cast<uint8_t>(value >> 8);
    data[off + 2] = static_cast<uint8_t>(value >> 16);
    data[off + 3] = static_cast<uint8_t>(value >> 24);
}
std::vector<uint8_t> makeRom() {
    std::vector<uint8_t> rom(1024 * 1024, 0);
    const std::size_t h = 0x7fc0;
    for (int i = 0; i < 21; ++i) rom[h + i] = ' ';
    rom[h + 0x15] = 0x30;
    rom[h + 0x16] = 0xf5;
    rom[h + 0x17] = 9;
    rom[h + 0x18] = 3;
    rom[h + 0x19] = 1;
    put16(rom, h + 0x1c, 0xedcb);
    put16(rom, h + 0x1e, 0x1234);
    put16(rom, h + 0x3c, 0x8000);
    return rom;
}
void writeFile(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}
}
int main() {
    const auto root = std::filesystem::temp_directory_path() / "snesultimate_st018_fw_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    std::vector<uint8_t> program(0x20000, 0);
    std::vector<uint8_t> data(0x8000, 0);
    put32(program, 0x00, 0xE59F1008u);
    put32(program, 0x04, 0xE3A0005Au);
    put32(program, 0x08, 0xE5C10000u);
    put32(program, 0x0c, 0xEAFFFFFEu);
    put32(program, 0x10, 0x40000000u);
    writeFile(root / "st018.program.rom", program);
    writeFile(root / "st018.data.rom", data);
#if defined(_WIN32)
    _putenv_s("SNESULTIMATE_FIRMWARE_DIR", root.string().c_str());
    _putenv_s("SNESULTIMATE_ALLOW_UNVERIFIED_FIRMWARE", "1");
#else
    setenv("SNESULTIMATE_FIRMWARE_DIR", root.string().c_str(), 1);
    setenv("SNESULTIMATE_ALLOW_UNVERIFIED_FIRMWARE", "1", 1);
#endif
    Cartridge cart;
    cart.setSRAMPersistence(false);
    expect(cart.loadData(makeRom(), (root / "synthetic_st018.sfc").string()), "ST018 cartridge loads with synthetic firmware");
    expect(cart.hardwareType() == CartridgeHardware::ST018, "synthetic cartridge detects ST018");
    expect(cart.hardwareSupportLevel() == CartridgeSupportLevel::FirmwareLLE,
           "valid ST018 program/data firmware selects firmware execution path");
    Bus bus;
    bus.attachCartridge(&cart);
    expect((bus.read(0x003804) & 0x80) == 0, "ST018 bridge stays not-ready during ARM reset sequence");
    cart.clockHardware(65540);
    expect((bus.read(0x003804) & 0x80) != 0, "ST018 bridge becomes ready after documented reset delay");
    cart.clockHardware(32);
    expect((bus.read(0x003804) & 0x01) != 0, "ARM firmware can assert ARM-to-SCPU mailbox ready");
    expect(bus.read(0x003800) == 0x5a, "ARM firmware byte reaches S-CPU through $3800 mailbox");
    expect((bus.read(0x003804) & 0x01) == 0, "$3800 read consumes ARM-to-SCPU mailbox byte");
    auto state = cart.saveHardwareState();
    expect(!state.empty(), "firmware-backed ST018 exposes checkpoint state");
    bus.write(0x003804, 0x01);
    expect((bus.read(0x003804) & 0x80) == 0, "ST018 reset edge restarts the ARM reset sequence");
    expect(cart.loadHardwareState(cart.hardwareStateTag(), state), "ST018 firmware checkpoint restores ARM and bridge state");
    expect((bus.read(0x003804) & 0x80) != 0, "restored ST018 state recovers ready bridge state");
#if defined(_WIN32)
    _putenv_s("SNESULTIMATE_FIRMWARE_DIR", "");
    _putenv_s("SNESULTIMATE_ALLOW_UNVERIFIED_FIRMWARE", "");
#else
    unsetenv("SNESULTIMATE_FIRMWARE_DIR");
    unsetenv("SNESULTIMATE_ALLOW_UNVERIFIED_FIRMWARE");
#endif
    std::filesystem::remove_all(root);
    if (failures) {
        std::cerr << failures << " ST018 firmware regression(s) failed\n";
        return 1;
    }
    std::cout << "ST018 firmware accuracy regression passed\n";
    return 0;
}

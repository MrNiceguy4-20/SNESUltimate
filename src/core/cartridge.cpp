#include "cartridge.hpp"
#include "embedded_firmware.hpp"
#include "superfx.hpp"
#include "superfx2.hpp"
#include "sa1.hpp"
#include "dsp1.hpp"
#include "dsp2.hpp"
#include "dsp3.hpp"
#include "dsp4.hpp"
#include "sdd1.hpp"
#include "cx4.hpp"
#include "spc7110.hpp"
#include "srtc.hpp"
#include "seta.hpp"
#include "nec_dsp.hpp"
#include "state_io.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <system_error>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif
namespace {
constexpr std::size_t kLoROMHeader = 0x7FC0;
constexpr std::size_t kHiROMHeader = 0xFFC0;
constexpr std::size_t kExLoROMHeader = 0x407FC0;
constexpr std::size_t kExHiROMHeader = 0x40FFC0;
constexpr std::size_t kTitleLength = 21;
constexpr std::size_t kMaxRomImageBytes = 64u * 1024u * 1024u;
bool isGSU2Header(uint8_t romType, uint8_t romSizeExponent) {
    return (romType & 0x0f) == 0x05 || romSizeExponent > 0x0a;
}
bool isMC1Image(uint16_t checksum) {
    switch (checksum) {
    case 0xF930:
    case 0xF6B9:
    case 0xAB12:
        return true;
    default:
        return false;
    }
}
bool readExactFile(const std::filesystem::path& path, std::size_t expected, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const auto n = f.tellg();
    if (n < 0 || static_cast<std::size_t>(n) != expected) return false;
    f.seekg(0, std::ios::beg);
    out.resize(expected);
    return expected == 0 || static_cast<bool>(f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(expected)));
}
uint32_t crc32Firmware(const std::vector<uint8_t>& bytes) {
    uint32_t crc = 0xFFFFFFFFu;
    for (uint8_t byte : bytes) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
std::string environmentVariable(const char* name) {
#if defined(_MSC_VER)
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) {
        return {};
    }
    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return (value != nullptr) ? std::string(value) : std::string{};
#endif
}
bool embeddedFirmware(const std::string& name, std::size_t expected, std::vector<uint8_t>& out) {
    if (!environmentVariable("SNESULTIMATE_DISABLE_EMBEDDED_FIRMWARE").empty()) return false;
    const EmbeddedBlob* blob = findEmbeddedFirmware(name.c_str());
    if (blob == nullptr || blob->size != expected) return false;
    out.assign(blob->data, blob->data + blob->size);
    return true;
}
bool allowUnverifiedFirmware() {
    const std::string value = environmentVariable("SNESULTIMATE_ALLOW_UNVERIFIED_FIRMWARE");
    return !value.empty() && (value[0] == '1' || value[0] == 'y' || value[0] == 'Y' || value[0] == 't' || value[0] == 'T');
}
bool validNecFirmwarePair(const std::string& base, const std::vector<uint8_t>& program,
                          const std::vector<uint8_t>& data) {
    struct Identity { const char* name; uint32_t programCrc; uint32_t dataCrc; };
    static constexpr Identity known[] = {
        {"dsp1",  0xDB9A4C92u, 0xC8347CBDu},
        {"dsp1b", 0x66A73998u, 0x37A252C6u},
        {"dsp2",  0x0C1CF838u, 0xB3893C70u},
        {"dsp3",  0xF29BE51Cu, 0x8B41A2BDu},
        {"dsp4",  0x14B77AE3u, 0xEFA8B9B2u},
        {"st010", 0x691CBB4Fu, 0x216081B2u},
        {"st011", 0xA741E9FBu, 0x803256A7u},
    };
    for (const auto& id : known) {
        if (base == id.name)
            return allowUnverifiedFirmware() ||
                   (crc32Firmware(program) == id.programCrc && crc32Firmware(data) == id.dataCrc);
    }
    return false;
}
bool validST018FirmwarePair(const std::vector<uint8_t>& program, const std::vector<uint8_t>& data) {
    return allowUnverifiedFirmware() ||
           (crc32Firmware(program) == 0xF73D5E10u && crc32Firmware(data) == 0xB5255459u);
}
std::string firmwareDirectoryFromEnvironment() {
    return environmentVariable("SNESULTIMATE_FIRMWARE_DIR");
}
std::filesystem::path executableDirectory() {
#if defined(_WIN32)
    std::wstring buffer(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (n != 0 && n < buffer.size()) {
        buffer.resize(n);
        return std::filesystem::path(buffer).parent_path();
    }
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    if (size != 0) {
        std::string buffer(size, '\0');
        if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
            std::error_code ec;
            const auto resolved = std::filesystem::weakly_canonical(std::filesystem::path(buffer.c_str()), ec);
            return (ec ? std::filesystem::path(buffer.c_str()) : resolved).parent_path();
        }
    }
#elif defined(__linux__)
    std::string buffer(4096, '\0');
    const ssize_t n = ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (n > 0) {
        buffer.resize(static_cast<std::size_t>(n));
        return std::filesystem::path(buffer).parent_path();
    }
#endif
    return {};
}
void appendFirmwareDirectoryVariants(std::vector<std::filesystem::path>& roots,
                                     const std::filesystem::path& base) {
    if (base.empty()) return;
    roots.push_back(base / "firmware");
    roots.push_back(base / "Firmware");
}
std::vector<std::filesystem::path> firmwareRoots(const std::string& romPath) {
    std::vector<std::filesystem::path> roots;
    const std::string firmwareDir = firmwareDirectoryFromEnvironment();
    if (!firmwareDir.empty()) roots.emplace_back(firmwareDir);
    if (!romPath.empty()) {
        const auto parent = std::filesystem::path(romPath).parent_path();
        appendFirmwareDirectoryVariants(roots, parent);
        if (!parent.empty()) roots.push_back(parent);
    }
    appendFirmwareDirectoryVariants(roots, executableDirectory());
    std::error_code ec;
    const auto cwd = std::filesystem::current_path(ec);
    if (!ec) appendFirmwareDirectoryVariants(roots, cwd);
    roots.emplace_back("firmware");
    roots.emplace_back("Firmware");
    std::vector<std::filesystem::path> unique;
    for (const auto& root : roots) {
        const auto normalized = root.lexically_normal();
        if (std::find(unique.begin(), unique.end(), normalized) == unique.end())
            unique.push_back(normalized);
    }
    return unique;
}
bool loadNecFirmware(const std::string& romPath, const std::vector<std::string>& bases,
                     std::size_t programSize, std::size_t dataSize, NecDspFirmware& fw) {
    for (const auto& base : bases) {
        std::vector<uint8_t> p, d;
        if (embeddedFirmware(base + ".program.rom", programSize, p) &&
            embeddedFirmware(base + ".data.rom", dataSize, d) && validNecFirmwarePair(base, p, d)) {
            fw.program = std::move(p); fw.data = std::move(d); return true;
        }
    }
    for (const auto& root : firmwareRoots(romPath)) {
        for (const auto& base : bases) {
            std::vector<uint8_t> p, d;
            if (readExactFile(root / (base + ".program.rom"), programSize, p) &&
                readExactFile(root / (base + ".data.rom"), dataSize, d) &&
                validNecFirmwarePair(base, p, d)) {
                fw.program = std::move(p); fw.data = std::move(d); return true;
            }
            std::vector<uint8_t> combined;
            if (readExactFile(root / (base + ".rom"), programSize + dataSize, combined)) {
                std::vector<uint8_t> p2(combined.begin(), combined.begin() + static_cast<std::ptrdiff_t>(programSize));
                std::vector<uint8_t> d2(combined.begin() + static_cast<std::ptrdiff_t>(programSize), combined.end());
                if (validNecFirmwarePair(base, p2, d2)) {
                    fw.program = std::move(p2); fw.data = std::move(d2); return true;
                }
            }
            std::error_code ec;
            if (std::filesystem::is_directory(root, ec)) {
                for (std::filesystem::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
                    if (!it->is_regular_file(ec)) continue;
                    const auto ext = it->path().extension().string();
                    if (ext != ".bin" && ext != ".BIN") continue;
                    std::vector<uint8_t> candidate;
                    if (!readExactFile(it->path(), programSize + dataSize, candidate)) continue;
                    std::vector<uint8_t> p2(candidate.begin(), candidate.begin() + static_cast<std::ptrdiff_t>(programSize));
                    std::vector<uint8_t> d2(candidate.begin() + static_cast<std::ptrdiff_t>(programSize), candidate.end());
                    if (validNecFirmwarePair(base, p2, d2)) {
                        fw.program = std::move(p2); fw.data = std::move(d2); return true;
                    }
                }
            }
        }
    }
    return false;
}
bool loadCX4DataRom(const std::string& romPath, std::vector<uint8_t>& data) {
    std::vector<uint8_t> embedded;
    if (embeddedFirmware("cx4.data.rom", 3072, embedded) && crc32Firmware(embedded) == 0xB6E76A6Au) {
        data = std::move(embedded);
        return true;
    }
    for (const auto& root : firmwareRoots(romPath)) {
        std::vector<uint8_t> d;
        if (readExactFile(root / "cx4.data.rom", 3072, d) &&
            (allowUnverifiedFirmware() || crc32Firmware(d) == 0xB6E76A6Au)) {
            data = std::move(d);
            return true;
        }
    }
    return false;
}
bool loadST018Firmware(const std::string& romPath, ST018Firmware& fw) {
    {
        std::vector<uint8_t> p, d;
        if (embeddedFirmware("st018.program.rom", 0x20000, p) && embeddedFirmware("st018.data.rom", 0x08000, d) &&
            validST018FirmwarePair(p, d)) {
            fw.program = std::move(p); fw.data = std::move(d); return true;
        }
    }
    for (const auto& root : firmwareRoots(romPath)) {
        std::vector<uint8_t> p, d;
        if (readExactFile(root / "st018.program.rom", 0x20000, p) &&
            readExactFile(root / "st018.data.rom", 0x08000, d) && validST018FirmwarePair(p, d)) {
            fw.program = std::move(p); fw.data = std::move(d); return true;
        }
        std::vector<uint8_t> combined;
        if (readExactFile(root / "st018.rom", 0x28000, combined)) {
            std::vector<uint8_t> p2(combined.begin(), combined.begin() + 0x20000);
            std::vector<uint8_t> d2(combined.begin() + 0x20000, combined.end());
            if (validST018FirmwarePair(p2, d2)) {
                fw.program = std::move(p2); fw.data = std::move(d2); return true;
            }
        }
    }
    return false;
}
class UnsupportedCartridgeDevice final : public CartridgeDevice {
public:
    bool implemented() const override { return false; }
};
class OBC1Device final : public CartridgeDevice {
public:
    OBC1Device(std::vector<uint8_t>& ram, bool& dirty) : ram_(ram), dirty_(dirty) {}
    void reset() override {
        if (ram_.size() < 0x2000) ram_.resize(0x2000, 0xFF);
        base_ptr_ = (ram_[0x1FF5] & 1) ? 0x1800 : 0x1C00;
        object_address_ = static_cast<uint8_t>(ram_[0x1FF6] & 0x7F);
        shift_ = static_cast<uint8_t>((ram_[0x1FF6] & 3) << 1);
    }
    bool read(uint32_t address, uint8_t& value) override {
        const uint8_t bank = static_cast<uint8_t>(address >> 16);
        const uint16_t addr = static_cast<uint16_t>(address);
        if (!owns(bank, addr)) return false;
        switch (addr) {
        case 0x7FF0: value = ram_[base_ptr_ + (object_address_ << 2) + 0]; break;
        case 0x7FF1: value = ram_[base_ptr_ + (object_address_ << 2) + 1]; break;
        case 0x7FF2: value = ram_[base_ptr_ + (object_address_ << 2) + 2]; break;
        case 0x7FF3: value = ram_[base_ptr_ + (object_address_ << 2) + 3]; break;
        case 0x7FF4: value = ram_[base_ptr_ + (object_address_ >> 2) + 0x200]; break;
        default: value = ram_[addr - 0x6000]; break;
        }
        return true;
    }
    bool write(uint32_t address, uint8_t value) override {
        const uint8_t bank = static_cast<uint8_t>(address >> 16);
        const uint16_t addr = static_cast<uint16_t>(address);
        if (!owns(bank, addr)) return false;
        if (addr >= 0x7FF0 && addr <= 0x7FF3) {
            setRam(base_ptr_ + (object_address_ << 2) + (addr - 0x7FF0), value);
        } else if (addr == 0x7FF4) {
            const std::size_t index = base_ptr_ + (object_address_ >> 2) + 0x200;
            const uint8_t updated = static_cast<uint8_t>((ram_[index] & ~(3u << shift_)) |
                ((value & 3u) << shift_));
            setRam(index, updated);
        } else if (addr == 0x7FF5) {
            base_ptr_ = (value & 1) ? 0x1800 : 0x1C00;
        } else if (addr == 0x7FF6) {
            object_address_ = static_cast<uint8_t>(value & 0x7F);
            shift_ = static_cast<uint8_t>((value & 3) << 1);
        }
        setRam(addr - 0x6000, value);
        return true;
    }
    bool implemented() const override { return true; }
    uint32_t stateTag() const override { return 0x3143424Fu; }
    std::vector<uint8_t> saveState() const override {
        stateio::Writer w; uint32_t version = 1; w.pod(version);
        w.pod(base_ptr_); w.pod(object_address_); w.pod(shift_);
        return w.take();
    }
    bool loadState(const std::vector<uint8_t>& state) override {
        stateio::Reader r(state); uint32_t version = 0;
        if (!r.pod(version) || version != 1 || !r.pod(base_ptr_) ||
            !r.pod(object_address_) || !r.pod(shift_) || !r.done()) return false;
        return (base_ptr_ == 0x1800 || base_ptr_ == 0x1C00) &&
               object_address_ < 0x80 && shift_ <= 6 && (shift_ & 1u) == 0;
    }
private:
    static bool owns(uint8_t bank, uint16_t addr) {
        return (bank <= 0x3F || (bank >= 0x80 && bank <= 0xBF)) &&
               addr >= 0x6000 && addr <= 0x7FFF;
    }
    void setRam(std::size_t index, uint8_t value) {
        if (index >= ram_.size()) return;
        if (ram_[index] != value) {
            ram_[index] = value;
            dirty_ = true;
        }
    }
    std::vector<uint8_t>& ram_;
    bool& dirty_;
    uint16_t base_ptr_ = 0x1C00;
    uint8_t object_address_ = 0;
    uint8_t shift_ = 0;
};
uint16_t read16le(const std::vector<uint8_t>& data, std::size_t offset) {
    if (offset + 1 >= data.size()) {
        return 0;
    }
    return static_cast<uint16_t>(data[offset]) |
           (static_cast<uint16_t>(data[offset + 1]) << 8);
}
}
Cartridge::~Cartridge() {
    saveSRAM();
    saveRTC();
}
std::string Cartridge::makeSavePath(const std::string& romPath) {
    const std::size_t slash = romPath.find_last_of("/\\");
    const std::size_t dot = romPath.find_last_of('.');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
        return romPath.substr(0, dot) + ".srm";
    }
    return romPath + ".srm";
}
void Cartridge::loadSRAM() {
    if (!sram_persistence_enabled_ || sram_data_.empty() || save_path_.empty()) return;
    std::ifstream save(save_path_, std::ios::binary);
    if (!save.is_open()) return;
    if (!save.read(reinterpret_cast<char*>(sram_data_.data()), static_cast<std::streamsize>(sram_data_.size()))) return;
    sram_dirty_ = false;
    rtc_dirty_ = false;
}
bool Cartridge::saveSRAM() {
    if (!sram_persistence_enabled_ || !sram_dirty_ || sram_data_.empty() || save_path_.empty()) return true;
    std::ofstream save(save_path_, std::ios::binary | std::ios::trunc);
    if (!save.is_open()) return false;
    save.write(reinterpret_cast<const char*>(sram_data_.data()), static_cast<std::streamsize>(sram_data_.size()));
    if (!save.good()) return false;
    sram_dirty_ = false;
    return true;
}
void Cartridge::loadRTC() {
    if (!sram_persistence_enabled_ || rtc_data_.empty() || rtc_path_.empty()) return;
    std::ifstream f(rtc_path_, std::ios::binary);
    if (!f.is_open()) return;
    if (!f.read(reinterpret_cast<char*>(rtc_data_.data()), static_cast<std::streamsize>(rtc_data_.size()))) return;
    rtc_dirty_ = false;
}
bool Cartridge::saveRTC() {
    if (!sram_persistence_enabled_ || rtc_data_.empty() || rtc_path_.empty()) return true;
    if (device_ != nullptr) device_->preparePersistentSave();
    if (!rtc_dirty_) return true;
    std::ofstream f(rtc_path_, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) return false;
    f.write(reinterpret_cast<const char*>(rtc_data_.data()), static_cast<std::streamsize>(rtc_data_.size()));
    if (!f.good()) return false;
    rtc_dirty_ = false;
    return true;
}
void Cartridge::clearMetadata() {
    sram_data_.clear();
    rtc_data_.clear();
    title_.clear();
    mapper_type_ = MapperType::LoROM;
    map_mode_ = 0;
    rom_type_ = 0;
    region_ = 0;
    hardware_type_ = CartridgeHardware::None;
    device_.reset();
    hardware_needs_clock_ = false;
    checksum_ = 0;
    checksum_complement_ = 0;
    source_name_.clear();
    sram_dirty_ = false;
}
bool Cartridge::loadData(std::vector<uint8_t> data, const std::string& sourcePath, const std::string& displayName) {
    saveSRAM();
    saveRTC();
    if (data.empty()) {
        std::cerr << "ROM data is empty.\n";
        return false;
    }
    if (data.size() > 512 && (data.size() & 0x3FF) == 512) {
        data.erase(data.begin(), data.begin() + 512);
    }
    rom_data_ = std::move(data);
    clearMetadata();
    rom_path_ = sourcePath;
    source_name_ = displayName.empty() ? sourcePath : displayName;
    save_path_ = makeSavePath(sourcePath);
    rtc_path_ = save_path_;
    const auto rtcDot = rtc_path_.find_last_of('.');
    if (rtcDot != std::string::npos) rtc_path_.replace(rtcDot, std::string::npos, ".rtc"); else rtc_path_ += ".rtc";
    parse_header();
    if (hardware_type_ == CartridgeHardware::OBC1 && sram_data_.size() < 0x2000) {
        sram_data_.resize(0x2000, 0xFF);
    }
    loadSRAM();
    if (hardware_type_ == CartridgeHardware::SRTC || hardware_type_ == CartridgeHardware::SPC7110RTC) { rtc_data_.assign(20, 0); loadRTC(); }
    create_hardware_device();
    return true;
}
bool Cartridge::load(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "Failed to open ROM file: " << filepath << '\n';
        return false;
    }
    const std::streamsize size = file.tellg();
    if (size <= 0) {
        std::cerr << "ROM file is empty: " << filepath << '\n';
        return false;
    }
    if (static_cast<std::uintmax_t>(size) > kMaxRomImageBytes) {
        std::cerr << "ROM file exceeds the 64 MiB safety limit: " << filepath << '\n';
        return false;
    }
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> new_rom(static_cast<std::size_t>(size));
    if (!file.read(reinterpret_cast<char*>(new_rom.data()), size)) {
        std::cerr << "Failed to read ROM data.\n";
        return false;
    }
    return loadData(std::move(new_rom), filepath, filepath);
}
void Cartridge::reset() {
    if (device_ != nullptr) device_->reset();
}
const char* Cartridge::hardwareName() const {
    switch (hardware_type_) {
    case CartridgeHardware::None: return "None";
    case CartridgeHardware::DSP1: return "DSP-1";
    case CartridgeHardware::DSP2: return "DSP-2";
    case CartridgeHardware::DSP3: return "DSP-3";
    case CartridgeHardware::DSP4: return "DSP-4";
    case CartridgeHardware::SuperFX:
        return isMC1Image(checksum_) ? "SuperFX / MC1" : "SuperFX / GSU-1";
    case CartridgeHardware::SuperFX2: return "SuperFX 2 / GSU-2";
    case CartridgeHardware::SA1: return "SA-1";
    case CartridgeHardware::SDD1: return "S-DD1";
    case CartridgeHardware::SPC7110: return "SPC7110";
    case CartridgeHardware::SPC7110RTC: return "SPC7110 + RTC";
    case CartridgeHardware::OBC1: return "OBC-1";
    case CartridgeHardware::SRTC: return "S-RTC";
    case CartridgeHardware::CX4: return "CX4";
    case CartridgeHardware::ST010: return "ST010";
    case CartridgeHardware::ST011: return "ST011";
    case CartridgeHardware::ST018: return "ST018";
    case CartridgeHardware::UnknownCoprocessor: return "Unknown coprocessor";
    }
    return "Unknown";
}
CartridgeSupportLevel Cartridge::hardwareSupportLevel() const {
    if (hardware_type_ == CartridgeHardware::None || hardware_type_ == CartridgeHardware::OBC1) return CartridgeSupportLevel::Full;
    return device_ != nullptr ? device_->supportLevel() : CartridgeSupportLevel::Unsupported;
}
const char* Cartridge::hardwareSupportName() const {
    switch (hardwareSupportLevel()) {
    case CartridgeSupportLevel::Full: return "implemented";
    case CartridgeSupportLevel::FirmwareLLE: return "firmware LLE";
    case CartridgeSupportLevel::TimingApproximate: return "functional / timing approximate";
    case CartridgeSupportLevel::PartialHLE: return "partial / HLE";
    case CartridgeSupportLevel::Unsupported: return "recognized, not implemented";
    }
    return "unknown";
}
bool Cartridge::hardwareImplemented() const {
    return hardwareSupportLevel() != CartridgeSupportLevel::Unsupported;
}
bool Cartridge::readHardware(uint32_t address, uint8_t& value) {
    return readHardware(address, 0xFF, value);
}
bool Cartridge::readHardware(uint32_t address, uint8_t openBus, uint8_t& value) {
    return device_ != nullptr && device_->readHost(address & 0xFFFFFFu, openBus, value);
}
bool Cartridge::writeHardware(uint32_t address, uint8_t value) {
    return device_ != nullptr && device_->write(address & 0xFFFFFFu, value);
}
bool Cartridge::clockHardware(uint64_t masterClocks) {
    if (device_ == nullptr || !hardware_needs_clock_) return false;
    return device_->clockAndHostIrq(masterClocks);
}
void Cartridge::notifyHardwareDMARegisterWrite(int channel, int offset, uint8_t value) {
    if (device_ != nullptr) device_->dmaRegisterWrite(channel, offset, value);
}
void Cartridge::beginHardwareDMA(int channel, uint32_t aAddress, uint32_t length, uint8_t control) {
    if (device_ != nullptr) device_->dmaBegin(channel, aAddress & 0xFFFFFFu, length, control);
}
bool Cartridge::readHardwareDMA(int channel, uint32_t address, uint8_t& value) {
    return device_ != nullptr && device_->dmaRead(channel, address & 0xFFFFFFu, value);
}
void Cartridge::endHardwareDMA(int channel) {
    if (device_ != nullptr) device_->dmaEnd(channel);
}
void Cartridge::create_hardware_device() {
    device_.reset();
    switch (hardware_type_) {
    case CartridgeHardware::None:
        break;
    case CartridgeHardware::OBC1:
        if (sram_data_.size() < 0x2000) sram_data_.resize(0x2000, 0xFF);
        device_ = std::make_unique<OBC1Device>(sram_data_, sram_dirty_);
        device_->reset();
        break;
    case CartridgeHardware::SA1:
        if (sram_data_.empty()) sram_data_.resize(0x10000, 0xFF);
        device_ = createSA1Device(rom_data_, sram_data_, sram_dirty_, isPalRegion());
        device_->reset();
        break;
    case CartridgeHardware::DSP1: {
        DSP1Map map = DSP1Map::LoROMLarge;
        if (mapper_type_ == MapperType::HiROM || mapper_type_ == MapperType::ExHiROM) map = DSP1Map::HiROM;
        else if (rom_data_.size() < 1024u * 1024u) map = DSP1Map::LoROMSmall;
        std::string normalizedTitle = title_;
        std::transform(normalizedTitle.begin(), normalizedTitle.end(), normalizedTitle.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
        const bool pilotwings = normalizedTitle.find("PILOTWINGS") != std::string::npos;
        const std::vector<std::string> revisions = pilotwings
            ? std::vector<std::string>{"dsp1", "dsp1b"}
            : std::vector<std::string>{"dsp1b", "dsp1"};
        NecDspFirmware fw;
        if (loadNecFirmware(rom_path_, revisions, 0x1800, 0x0800, fw)) {
            device_ = createNecDspDevice(NecDspType::DSP1, map, std::move(fw), isPalRegion() ? 21281370u : 21477272u);
        }
        if (!device_) {
            device_ = createDSP1Device(map, isPalRegion() ? 21281370u : 21477272u);
        }
        device_->reset();
        break;
    }
    case CartridgeHardware::DSP2: {
        const DSP1Map map = DSP1Map::DSP2LoROM;
        NecDspFirmware fw;
        if (loadNecFirmware(rom_path_, {"dsp2"}, 0x1800, 0x0800, fw))
            device_ = createNecDspDevice(NecDspType::DSP2, map, std::move(fw), isPalRegion() ? 21281370u : 21477272u);
        if (!device_) device_ = createDSP2Device(map);
        device_->reset();
        break;
    }
    case CartridgeHardware::DSP3: {
        NecDspFirmware fw;
        if (loadNecFirmware(rom_path_, {"dsp3"}, 0x1800, 0x0800, fw))
            device_ = createNecDspDevice(NecDspType::DSP3, DSP1Map::LoROMSmall, std::move(fw), isPalRegion() ? 21281370u : 21477272u);
        if (!device_) device_ = createDSP3Device();
        device_->reset();
        break;
    }
    case CartridgeHardware::DSP4: {
        NecDspFirmware fw;
        if (loadNecFirmware(rom_path_, {"dsp4"}, 0x1800, 0x0800, fw))
            device_ = createNecDspDevice(NecDspType::DSP4, DSP1Map::LoROMSmall, std::move(fw), isPalRegion() ? 21281370u : 21477272u);
        if (!device_) device_ = createDSP4Device();
        device_->reset();
        break;
    }
    case CartridgeHardware::SDD1:
        device_ = createSDD1Device(rom_data_, sram_data_, sram_dirty_);
        device_->reset();
        break;
    case CartridgeHardware::CX4: {
        std::vector<uint8_t> cx4DataRom;
        loadCX4DataRom(rom_path_, cx4DataRom);
        device_ = createCX4Device(rom_data_, isPalRegion() ? 21281370u : 21477272u, std::move(cx4DataRom));
        device_->reset();
        break;
    }
    case CartridgeHardware::SPC7110:
    case CartridgeHardware::SPC7110RTC:
        if (sram_data_.empty()) sram_data_.resize(0x2000, 0xFF);
        device_ = createSPC7110Device(rom_data_, sram_data_, sram_dirty_, rtc_data_, rtc_dirty_, hardware_type_ == CartridgeHardware::SPC7110RTC, isPalRegion() ? 21281370u : 21477272u);
        device_->reset();
        break;
    case CartridgeHardware::SRTC:
        if (rtc_data_.size() < 20) rtc_data_.resize(20, 0);
        device_ = createSRTCDevice(rtc_data_, rtc_dirty_, isPalRegion() ? 21281370u : 21477272u);
        device_->reset();
        break;
    case CartridgeHardware::ST010: {
        if (sram_data_.size() < 0x1000) sram_data_.resize(0x1000, 0x00);
        NecDspFirmware fw;
        if (loadNecFirmware(rom_path_, {"st010"}, 0xC000, 0x1000, fw))
            device_ = createNecDspDevice(NecDspType::ST010, DSP1Map::LoROMSmall, std::move(fw), isPalRegion() ? 21281370u : 21477272u, &sram_data_, &sram_dirty_);
        if (!device_) device_ = createST010Device(sram_data_, sram_dirty_);
        device_->reset();
        break;
    }
    case CartridgeHardware::ST011: {
        if (sram_data_.size() < 0x1000) sram_data_.resize(0x1000, 0x00);
        NecDspFirmware fw;
        if (loadNecFirmware(rom_path_, {"st011"}, 0xC000, 0x1000, fw))
            device_ = createNecDspDevice(NecDspType::ST011, DSP1Map::LoROMSmall, std::move(fw), isPalRegion() ? 21281370u : 21477272u, &sram_data_, &sram_dirty_);
        if (!device_) device_ = createST011Device(sram_data_, sram_dirty_);
        device_->reset();
        break;
    }
    case CartridgeHardware::ST018: {
        if (sram_data_.size() < 0x2000) sram_data_.resize(0x2000, 0x00);
        ST018Firmware fw;
        if (loadST018Firmware(rom_path_, fw))
            device_ = createST018FirmwareDevice(sram_data_, sram_dirty_, std::move(fw), isPalRegion() ? 21281370u : 21477272u);
        if (!device_) device_ = createST018Device(sram_data_, sram_dirty_);
        device_->reset();
        break;
    }
    case CartridgeHardware::SuperFX:
    case CartridgeHardware::SuperFX2: {
        std::size_t header = mapper_type_ == MapperType::HiROM ? kHiROMHeader : kLoROMHeader;
        const uint8_t exp = header >= 3 && header - 3 < rom_data_.size() ? rom_data_[header - 3] : 0;
        std::size_t bytes = exp < 16 ? (std::size_t(1024) << exp) : 0;
        const bool mc1 = isMC1Image(checksum_);
        if (mc1) bytes = 32 * 1024;
        if (hardware_type_ == CartridgeHardware::SuperFX2) {
            device_ = createSuperFX2Device(rom_data_, bytes, &sram_data_, &sram_dirty_);
        } else {
            GSURevision revision = GSURevision::GSU1;
            if (mc1) {
                revision = GSURevision::MC1;
            }
            device_ = createGSUDevice(rom_data_, bytes, GSUTimingProfile{revision}, &sram_data_, &sram_dirty_);
        }
        device_->reset();
        break;
    }
    default:
        device_ = std::make_unique<UnsupportedCartridgeDevice>();
        break;
    }
    hardware_needs_clock_ = device_ != nullptr && device_->needsClock();
}
void Cartridge::detect_hardware(std::size_t header) {
    hardware_type_ = CartridgeHardware::None;
    const uint16_t identifier = static_cast<uint16_t>((static_cast<uint16_t>(rom_type_) << 8) | map_mode_);
    const uint8_t company = (header + 0x1A < rom_data_.size()) ? rom_data_[header + 0x1A] : 0;
    const uint8_t romSizeExp = (header + 0x17 < rom_data_.size()) ? rom_data_[header + 0x17] : 0;
    if (rom_type_ == 0x03) {
        hardware_type_ = (map_mode_ == 0x30) ? CartridgeHardware::DSP4 : CartridgeHardware::DSP1;
    } else if (rom_type_ == 0x05) {
        if (map_mode_ == 0x20) hardware_type_ = CartridgeHardware::DSP2;
        else if (map_mode_ == 0x30 && company == 0xB2) hardware_type_ = CartridgeHardware::DSP3;
        else hardware_type_ = CartridgeHardware::DSP1;
    }
    switch (identifier) {
    case 0x5535: hardware_type_ = CartridgeHardware::SRTC; break;
    case 0xF93A: hardware_type_ = CartridgeHardware::SPC7110RTC; break;
    case 0xF53A: hardware_type_ = CartridgeHardware::SPC7110; break;
    case 0x2530: hardware_type_ = CartridgeHardware::OBC1; break;
    case 0x3423:
    case 0x3523: hardware_type_ = CartridgeHardware::SA1; break;
    case 0x1320:
    case 0x1420:
    case 0x1520:
    case 0x1A20:
    case 0x1330:
    case 0x1430:
    case 0x1530:
    case 0x1A30: hardware_type_ = CartridgeHardware::SuperFX; break;
    case 0x4332:
    case 0x4532: hardware_type_ = CartridgeHardware::SDD1; break;
    case 0xF530: hardware_type_ = CartridgeHardware::ST018; break;
    case 0xF630: hardware_type_ = (romSizeExp == 0x09) ? CartridgeHardware::ST011 : CartridgeHardware::ST010; break;
    case 0xF320: hardware_type_ = CartridgeHardware::CX4; break;
    default: break;
    }
    if (hardware_type_ == CartridgeHardware::SuperFX && isGSU2Header(rom_type_, romSizeExp)) {
        hardware_type_ = CartridgeHardware::SuperFX2;
    }
    const uint8_t contents = static_cast<uint8_t>(rom_type_ & 0x0F);
    if (hardware_type_ == CartridgeHardware::None && contents >= 0x03 && contents <= 0x06) {
        hardware_type_ = CartridgeHardware::UnknownCoprocessor;
    }
}
Cartridge::HeaderCandidate Cartridge::score_header(std::size_t offset, MapperType mapper) const {
    HeaderCandidate result{offset, mapper, -1000};
    if (offset + 0x40 > rom_data_.size()) {
        return result;
    }
    int score = 0;
    const uint8_t map_mode = rom_data_[offset + 0x15];
    const uint8_t map_low = map_mode & 0x0F;
    switch (mapper) {
    case MapperType::LoROM:
        score += (map_low == 0x00 || map_low == 0x02 || map_low == 0x03) ? 4 : -6;
        break;
    case MapperType::ExLoROM:
        score += (map_low == 0x00 || map_low == 0x02 || map_low == 0x03) ? 5 : -7;
        score += rom_data_.size() > 0x400000 ? 3 : -8;
        break;
    case MapperType::HiROM:
        score += (map_low == 0x01 || map_low == 0x0A) ? 4 : -6;
        break;
    case MapperType::ExHiROM:
        score += (map_low == 0x05) ? 6 : -8;
        if (rom_data_.size() > 0x400000) score += 2;
        break;
    }
    const uint16_t complement = read16le(rom_data_, offset + 0x1C);
    const uint16_t checksum = read16le(rom_data_, offset + 0x1E);
    if (checksum != 0 && complement != 0 &&
        static_cast<uint16_t>(checksum + complement) == 0xFFFF) {
        score += 8;
    }
    const uint16_t reset_vector = read16le(rom_data_, offset + 0x3C);
    if (reset_vector >= 0x8000) {
        score += 4;
    } else if (reset_vector != 0) {
        score -= 4;
    }
    int printable = 0;
    for (std::size_t i = 0; i < kTitleLength; ++i) {
        const uint8_t ch = rom_data_[offset + i];
        if (ch == 0 || ch == 0xFF || std::isprint(static_cast<unsigned char>(ch))) {
            ++printable;
        }
    }
    score += printable >= 18 ? 2 : -2;
    if (rom_data_[offset + 0x17] <= 0x0E) {
        ++score;
    }
    if (rom_data_[offset + 0x18] <= 0x0D) {
        ++score;
    }
    result.score = score;
    return result;
}
std::size_t Cartridge::decode_ram_size(uint8_t exponent) {
    if (exponent == 0 || exponent > 0x0D) {
        return 0;
    }
    return static_cast<std::size_t>(1024) << exponent;
}
void Cartridge::parse_header() {
    HeaderCandidate best = score_header(kLoROMHeader, MapperType::LoROM);
    const HeaderCandidate hi = score_header(kHiROMHeader, MapperType::HiROM);
    if (hi.score > best.score) {
        best = hi;
    }
    const HeaderCandidate exlo = score_header(kExLoROMHeader, MapperType::ExLoROM);
    if (exlo.score > best.score) {
        best = exlo;
    }
    const HeaderCandidate exhi = score_header(kExHiROMHeader, MapperType::ExHiROM);
    if (exhi.score > best.score) {
        best = exhi;
    }
    if (best.score <= -1000) {
        title_ = "Unknown / headerless ROM";
        return;
    }
    mapper_type_ = best.mapper;
    const std::size_t header = best.offset;
    title_.assign(reinterpret_cast<const char*>(&rom_data_[header]), kTitleLength);
    while (!title_.empty() &&
           (title_.back() == ' ' || title_.back() == '\0' || title_.back() == '\xFF')) {
        title_.pop_back();
    }
    if (title_.empty()) {
        title_ = "Untitled SNES ROM";
    }
    map_mode_ = rom_data_[header + 0x15];
    rom_type_ = rom_data_[header + 0x16];
    region_ = rom_data_[header + 0x19];
    checksum_complement_ = read16le(rom_data_, header + 0x1C);
    checksum_ = read16le(rom_data_, header + 0x1E);
    detect_hardware(header);
    const std::size_t sram_size = decode_ram_size(rom_data_[header + 0x18]);
    sram_data_.assign(sram_size, 0);
    const char* mapper_name = "LoROM";
    if (mapper_type_ == MapperType::HiROM) mapper_name = "HiROM";
    if (mapper_type_ == MapperType::ExLoROM) mapper_name = "ExLoROM";
    if (mapper_type_ == MapperType::ExHiROM) mapper_name = "ExHiROM";
}
std::size_t Cartridge::mirror_rom_address(std::size_t address, std::size_t size) {
    if (size == 0) return 0;
    std::size_t base = 0;
    std::size_t mask = 1;
    while (mask <= address && mask <= (static_cast<std::size_t>(-1) >> 1)) {
        mask <<= 1;
    }
    mask >>= 1;
    while (address >= size) {
        while (mask != 0 && (address & mask) == 0) {
            mask >>= 1;
        }
        if (mask == 0) return base;
        address -= mask;
        if (size > mask) {
            size -= mask;
            base += mask;
        }
        mask >>= 1;
    }
    return base + address;
}
uint8_t Cartridge::read_rom(uint32_t address) const {
    const std::size_t size = rom_data_.size();
    if (size == 0) {
        return 0xFF;
    }
    if ((size & (size - 1)) == 0) {
        return rom_data_[static_cast<std::size_t>(address) & (size - 1)];
    }
    return rom_data_[mirror_rom_address(address, size)];
}
uint8_t Cartridge::read_sram(uint32_t address) const {
    if (sram_data_.empty()) {
        return 0xFF;
    }
    return sram_data_[address % sram_data_.size()];
}
void Cartridge::write_sram(uint32_t address, uint8_t value) {
    if (!sram_data_.empty()) {
        const std::size_t index = address % sram_data_.size();
        if (sram_data_[index] != value) {
            sram_data_[index] = value;
            sram_dirty_ = true;
        }
    }
}

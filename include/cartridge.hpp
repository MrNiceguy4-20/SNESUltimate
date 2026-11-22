#ifndef CARTRIDGE_HPP
#define CARTRIDGE_HPP
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include "cartridge_device.hpp"
enum class MapperType {
    LoROM,
    HiROM,
    ExLoROM,
    ExHiROM
};
enum class CartridgeHardware {
    None,
    DSP1,
    DSP2,
    DSP3,
    DSP4,
    SuperFX,
    SuperFX2,
    SA1,
    SDD1,
    SPC7110,
    SPC7110RTC,
    OBC1,
    SRTC,
    CX4,
    ST010,
    ST011,
    ST018,
    UnknownCoprocessor
};
class Cartridge {
public:
    ~Cartridge();
    bool load(const std::string& filepath);
    bool loadData(std::vector<uint8_t> data, const std::string& sourcePath, const std::string& displayName = {});
    void reset();
    bool isLoaded() const { return !rom_data_.empty(); }
    uint8_t read_rom(uint32_t address) const;
    uint8_t read_sram(uint32_t address) const;
    void write_sram(uint32_t address, uint8_t value);
    MapperType mapperType() const { return mapper_type_; }
    const std::string& title() const { return title_; }
    std::size_t sramSize() const { return sram_data_.size(); }
    uint8_t mapMode() const { return map_mode_; }
    uint8_t romType() const { return rom_type_; }
    uint8_t region() const { return region_; }
    bool isPalRegion() const {
        return (region_ >= 0x02 && region_ <= 0x0C) || region_ == 0x11;
    }
    CartridgeHardware hardwareType() const { return hardware_type_; }
    const char* hardwareName() const;
    bool hasEnhancementHardware() const { return hardware_type_ != CartridgeHardware::None; }
    bool hardwareImplemented() const;
    CartridgeSupportLevel hardwareSupportLevel() const;
    const char* hardwareSupportName() const;
    uint32_t hardwareStateTag() const { return device_ ? device_->stateTag() : 0; }
    std::vector<uint8_t> saveHardwareState() const { return device_ ? device_->saveState() : std::vector<uint8_t>{}; }
    bool loadHardwareState(uint32_t tag, const std::vector<uint8_t>& state) { return device_ && tag == device_->stateTag() && device_->loadState(state); }
    bool readHardware(uint32_t address, uint8_t& value);
    bool readHardware(uint32_t address, uint8_t openBus, uint8_t& value);
    bool writeHardware(uint32_t address, uint8_t value);
    bool clockHardware(uint64_t masterClocks);
    void notifyHardwareDMARegisterWrite(int channel, int offset, uint8_t value);
    void beginHardwareDMA(int channel, uint32_t aAddress, uint32_t length, uint8_t control);
    bool readHardwareDMA(int channel, uint32_t address, uint8_t& value);
    void endHardwareDMA(int channel);
    bool hardwareNeedsClock() const { return hardware_needs_clock_; }
    bool hardwareIrqPending() const { return device_ != nullptr && device_->hostIrqPending(); }
    bool hardwareHostIrqMayChangeWithoutHostAccess() const { return device_ != nullptr && device_->hostIrqMayChangeWithoutHostAccess(); }
    bool hardwareHostAccessRequiresClock(uint32_t address, bool write) const { return device_ != nullptr && device_->hostAccessRequiresClock(address & 0xFFFFFFu, write); }
    void beginHardwareHostAccess(uint32_t address, bool write, uint8_t masterClocks) {
        if (device_ != nullptr) device_->hostAccessBegin(address & 0xFFFFFFu, write, masterClocks);
    }
    uint16_t checksum() const { return checksum_; }
    bool isLoROMFamily() const { return mapper_type_ == MapperType::LoROM || mapper_type_ == MapperType::ExLoROM; }
    bool isHiROMFamily() const { return mapper_type_ == MapperType::HiROM || mapper_type_ == MapperType::ExHiROM; }
    bool saveSRAM();
    bool saveRTC();
    void setSRAMPersistence(bool enabled) { sram_persistence_enabled_ = enabled; }
    const std::string& sourcePath() const { return rom_path_; }
    friend class SNES;
private:
    struct HeaderCandidate {
        std::size_t offset = 0;
        MapperType mapper = MapperType::LoROM;
        int score = -1000;
    };
    HeaderCandidate score_header(std::size_t offset, MapperType mapper) const;
    void parse_header();
    void detect_hardware(std::size_t header);
    void create_hardware_device();
    static std::size_t decode_ram_size(uint8_t exponent);
    static std::size_t mirror_rom_address(std::size_t address, std::size_t size);
    void loadSRAM();
    void loadRTC();
    static std::string makeSavePath(const std::string& romPath);
    void clearMetadata();
    std::vector<uint8_t> rom_data_;
    std::vector<uint8_t> sram_data_;
    std::vector<uint8_t> rtc_data_;
    std::string title_;
    MapperType mapper_type_ = MapperType::LoROM;
    uint8_t map_mode_ = 0;
    uint8_t rom_type_ = 0;
    uint8_t region_ = 0;
    CartridgeHardware hardware_type_ = CartridgeHardware::None;
    std::unique_ptr<CartridgeDevice> device_;
    bool hardware_needs_clock_ = false;
    uint16_t checksum_ = 0;
    uint16_t checksum_complement_ = 0;
    std::string rom_path_;
    std::string save_path_;
    std::string rtc_path_;
    std::string source_name_;
    bool sram_dirty_ = false;
    bool rtc_dirty_ = false;
    bool sram_persistence_enabled_ = true;
};
#endif

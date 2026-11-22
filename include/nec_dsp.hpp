#pragma once
#include "cartridge_device.hpp"
#include "dsp1.hpp"
#include <cstdint>
#include <memory>
#include <vector>
enum class NecDspType : uint8_t {
    DSP1,
    DSP2,
    DSP3,
    DSP4,
    ST010,
    ST011,
};
struct NecDspFirmware {
    std::vector<uint8_t> program;
    std::vector<uint8_t> data;
};
std::unique_ptr<CartridgeDevice> createNecDspDevice(
    NecDspType type,
    DSP1Map map,
    NecDspFirmware firmware,
    uint32_t masterClockHz,
    std::vector<uint8_t>* externalRam = nullptr,
    bool* externalRamDirty = nullptr);

#pragma once
#include "cartridge_device.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
enum class GSURevision : uint8_t {
    GSU1  = 0x01,
    GSU2  = 0x04,
    MC1   = 0x81,
    GSU1A = 0x82,
};
struct GSUTimingProfile {
    GSURevision revision = GSURevision::GSU1;
};
std::unique_ptr<CartridgeDevice> createGSUDevice(
    const std::vector<uint8_t>& rom,
    std::size_t expansionRamBytes,
    const GSUTimingProfile& profile,
    std::vector<uint8_t>* cartridgeRam = nullptr,
    bool* cartridgeRamDirty = nullptr);
std::unique_ptr<CartridgeDevice> createSuperFXDevice(
    const std::vector<uint8_t>& rom,
    std::size_t expansionRamBytes);

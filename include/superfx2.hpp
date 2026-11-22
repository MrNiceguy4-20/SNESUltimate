#pragma once
#include "cartridge_device.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
std::unique_ptr<CartridgeDevice> createSuperFX2Device(
    const std::vector<uint8_t>& rom,
    std::size_t expansionRamBytes,
    std::vector<uint8_t>* cartridgeRam = nullptr,
    bool* cartridgeRamDirty = nullptr);

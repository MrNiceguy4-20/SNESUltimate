#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include "cartridge_device.hpp"
std::unique_ptr<CartridgeDevice> createSA1Device(
    const std::vector<uint8_t>& rom,
    std::vector<uint8_t>& bwram,
    bool& bwramDirty,
    bool palRegion = false);

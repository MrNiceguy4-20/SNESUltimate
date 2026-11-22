#pragma once
#include "cartridge_device.hpp"
#include <memory>
#include <vector>
std::unique_ptr<CartridgeDevice> createSDD1Device(const std::vector<uint8_t>& rom, std::vector<uint8_t>& ram, bool& dirty);

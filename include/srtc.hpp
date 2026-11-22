#pragma once
#include "cartridge_device.hpp"
#include <memory>
#include <vector>
std::unique_ptr<CartridgeDevice> createSRTCDevice(std::vector<uint8_t>& rtcStorage, bool& dirty, uint32_t masterFrequency);

#pragma once
#include "cartridge_device.hpp"
#include <memory>
#include <vector>
std::unique_ptr<CartridgeDevice> createSPC7110Device(const std::vector<uint8_t>& rom, std::vector<uint8_t>& ram, bool& dirty, std::vector<uint8_t>& rtc, bool& rtcDirty, bool withRtc, uint32_t masterFrequency);

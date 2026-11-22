#pragma once
#include "cartridge_device.hpp"
#include <memory>
#include <vector>
std::unique_ptr<CartridgeDevice> createCX4Device(const std::vector<uint8_t>& rom, uint32_t masterClockHz = 21477272u, std::vector<uint8_t> dataRom = {});

#pragma once
#include "cartridge_device.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
enum class DSP1Map : unsigned char {
    LoROMSmall,
    LoROMLarge,
    HiROM,
    DSP2LoROM,
};
std::unique_ptr<CartridgeDevice> createDSP1Device(DSP1Map map, uint32_t masterClockHz = 21477272u);

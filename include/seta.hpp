#pragma once
#include "cartridge_device.hpp"
#include <memory>
#include <vector>
struct ST018Firmware {
    std::vector<uint8_t> program;
    std::vector<uint8_t> data;
    bool valid() const { return program.size()==0x20000 && data.size()==0x8000; }
};
std::unique_ptr<CartridgeDevice> createST010Device(std::vector<uint8_t>& ram, bool& dirty);
std::unique_ptr<CartridgeDevice> createST011Device(std::vector<uint8_t>& ram, bool& dirty);
std::unique_ptr<CartridgeDevice> createST018Device(std::vector<uint8_t>& ram, bool& dirty);
std::unique_ptr<CartridgeDevice> createST018FirmwareDevice(std::vector<uint8_t>& ram, bool& dirty, ST018Firmware firmware, uint32_t masterClockHz);

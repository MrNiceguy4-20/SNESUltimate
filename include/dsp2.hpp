#pragma once
#include "cartridge_device.hpp"
#include "dsp1.hpp"
#include <memory>
std::unique_ptr<CartridgeDevice> createDSP2Device(DSP1Map map);

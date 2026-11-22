#include "superfx2.hpp"
#include "superfx.hpp"
std::unique_ptr<CartridgeDevice> createSuperFX2Device(
    const std::vector<uint8_t>& rom,
    std::size_t expansionRamBytes,
    std::vector<uint8_t>* cartridgeRam,
    bool* cartridgeRamDirty) {
    static constexpr GSUTimingProfile kGSU2Profile{GSURevision::GSU2};
    return createGSUDevice(rom, expansionRamBytes, kGSU2Profile, cartridgeRam, cartridgeRamDirty);
}

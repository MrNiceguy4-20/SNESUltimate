#pragma once
#include <cstddef>
#include <cstdint>
struct EmbeddedBlob {
    const char* name;
    const uint8_t* data;
    std::size_t size;
};
const EmbeddedBlob* findEmbeddedFirmware(const char* name);

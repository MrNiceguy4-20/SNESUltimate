#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <vector>
class SPC7110Decompressor {
public:
    using Reader = std::function<uint8_t(uint32_t)>;
    explicit SPC7110Decompressor(Reader reader) : reader_(std::move(reader)) { reset(); }
    void reset();
    void initialize(uint32_t mode, uint32_t origin);
    void decode();
    uint32_t bpp() const { return bpp_; }
    uint32_t result() const { return result_; }
    std::vector<uint8_t> saveState() const;
    bool loadState(const std::vector<uint8_t>& state);
private:
    struct Context { uint8_t prediction = 0; uint8_t swap = 0; };
    Reader reader_;
    std::array<std::array<Context,15>,5> context_{};
    uint32_t bpp_ = 1, offset_ = 0, bits_ = 8;
    uint16_t range_ = 256, input_ = 0;
    uint8_t output_ = 0;
    uint64_t pixels_ = 0, colormap_ = 0;
    uint32_t result_ = 0;
    uint8_t read() { return reader_(offset_++); }
    static uint32_t deinterleave(uint64_t data, uint32_t bits);
    static uint64_t moveToFront(uint64_t list, uint32_t nibble);
};

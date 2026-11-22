#pragma once
#include "bus.hpp"
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>
struct CheatEntry {
    std::string description;
    std::vector<std::pair<uint32_t, uint8_t>> patches;
    bool enabled = false;
};
class CheatManager {
public:
    bool loadDatabase(const std::filesystem::path& database, const std::string& romTitle);
    void clear();
    void apply(Bus& bus);
    std::vector<CheatEntry>& entries() { return entries_; }
    const std::vector<CheatEntry>& entries() const { return entries_; }
private:
    std::vector<CheatEntry> entries_;
};

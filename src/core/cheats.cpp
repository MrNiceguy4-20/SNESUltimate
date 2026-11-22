#include "cheats.hpp"
#include <cstring>
#include <fstream>
#include <iterator>
#include <cctype>
#include <cstdio>
static std::string normalizedGameName(std::string value) {
    std::string out;
    bool inTag = false;
    for (char c : value) {
        if (c == '(' || c == '[') { inTag = true; continue; }
        if (c == ')' || c == ']') { inTag = false; continue; }
        if (inTag) continue;
        if (std::isalnum(static_cast<unsigned char>(c))) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}
static std::string baseGameName(const std::string& value) {
    const std::size_t cut = value.find_first_of("([");
    return normalizedGameName(cut == std::string::npos ? value : value.substr(0, cut));
}
static bool hasTokenInsensitive(std::string value, const std::string& token) {
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value.find(token) != std::string::npos;
}
namespace {
struct Cursor {
    const uint8_t* p;
    const uint8_t* end;
    bool take(std::size_t n) const { return static_cast<std::size_t>(end - p) >= n; }
    bool u8(uint8_t& v) { if (!take(1)) return false; v = *p++; return true; }
    bool u16(uint16_t& v) { if (!take(2)) return false; v = static_cast<uint16_t>(p[0] | (p[1] << 8)); p += 2; return true; }
    bool u32(uint32_t& v) { if (!take(4)) return false; v = p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); p += 4; return true; }
    bool str(std::string& v) { uint16_t n = 0; if (!u16(n) || !take(n)) return false; v.assign(reinterpret_cast<const char*>(p), n); p += n; return true; }
};
bool regionMatches(const std::string& romTitle, const std::string& fileName) {
    const bool usaRom = hasTokenInsensitive(romTitle, "(u)");
    const bool europeRom = hasTokenInsensitive(romTitle, "(e)");
    const bool japanRom = hasTokenInsensitive(romTitle, "(j)");
    const bool usaCheat = hasTokenInsensitive(fileName, "usa");
    const bool europeCheat = hasTokenInsensitive(fileName, "europe") || hasTokenInsensitive(fileName, "world");
    const bool japanCheat = hasTokenInsensitive(fileName, "japan") || hasTokenInsensitive(fileName, "(j)");
    const bool multiRegion = usaCheat && europeCheat;
    return usaRom ? (usaCheat || multiRegion) : europeRom ? (europeCheat || multiRegion) : japanRom ? japanCheat : true;
}
bool readGame(Cursor c, std::vector<CheatEntry>& out) {
    uint16_t count = 0;
    if (!c.u16(count)) return false;
    for (uint16_t i = 0; i < count; ++i) {
        CheatEntry entry;
        uint8_t parts = 0;
        if (!c.str(entry.description) || !c.u8(parts)) return false;
        for (uint8_t j = 0; j < parts; ++j) {
            uint32_t address = 0;
            uint8_t value = 0;
            if (!c.u32(address) || !c.u8(value)) return false;
            entry.patches.emplace_back(address & 0xFFFFFFu, value);
        }
        out.push_back(std::move(entry));
    }
    return true;
}
}
bool CheatManager::loadDatabase(const std::filesystem::path& database, const std::string& romTitle) {
    clear();
    if (romTitle.empty()) return false;
    std::ifstream in(database.wstring(), std::ios::in | std::ios::binary);
    if (!in) return false;
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (data.size() < 12 || std::memcmp(data.data(), "SNCH", 4) != 0) return false;
    Cursor header{data.data() + 4, data.data() + data.size()};
    uint32_t version = 0, games = 0;
    if (!header.u32(version) || version != 1 || !header.u32(games)) return false;
    const std::string wanted = baseGameName(romTitle);
    if (wanted.empty()) return false;
    struct Record { Cursor body; std::string name; };
    std::vector<Record> exact, loose;
    Cursor walk = header;
    for (uint32_t g = 0; g < games; ++g) {
        uint32_t size = 0;
        if (!walk.u32(size) || !walk.take(size)) break;
        Cursor body{walk.p, walk.p + size};
        walk.p += size;
        std::string name;
        if (!body.str(name)) continue;
        const std::string candidate = baseGameName(name);
        const std::string fullCandidate = normalizedGameName(name);
        if ((candidate == wanted || fullCandidate.rfind(wanted, 0) == 0) && regionMatches(romTitle, name)) exact.push_back({body, name});
        else if (!candidate.empty() && (candidate.find(wanted) != std::string::npos || wanted.find(candidate) != std::string::npos)) loose.push_back({body, name});
    }
    for (const Record& r : exact.empty() ? loose : exact) readGame(r.body, entries_);
    std::vector<CheatEntry> unique;
    for (auto& entry : entries_) {
        bool duplicate = false;
        for (const auto& existing : unique) {
            if (existing.description == entry.description) { duplicate = true; break; }
        }
        if (!duplicate) unique.push_back(std::move(entry));
    }
    entries_.swap(unique);
    return !entries_.empty();
}
void CheatManager::clear() { entries_.clear(); }
void CheatManager::apply(Bus& bus) {
    for (const auto& cheat : entries_) {
        if (!cheat.enabled) continue;
        for (const auto& patch : cheat.patches) bus.write(patch.first, patch.second);
    }
}

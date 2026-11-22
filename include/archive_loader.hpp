#pragma once
#include <cstdint>
#include <string>
#include <vector>
struct ArchivedRom {
    std::string entryName;
    std::vector<uint8_t> data;
};
class ArchiveLoader {
public:
    static bool isArchivePath(const std::string& path);
    static bool isRomPath(const std::string& path);
    static bool loadFirstRom(const std::string& archivePath, ArchivedRom& outRom, std::string& error);
private:
    static std::string extensionLower(const std::string& path);
};

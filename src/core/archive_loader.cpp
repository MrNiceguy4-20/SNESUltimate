#include "archive_loader.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>
#define MINIZ_HEADER_FILE_ONLY
#include "../../third_party/miniz/miniz.c"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
namespace {
constexpr std::size_t kMaxArchiveRomBytes = 64u * 1024u * 1024u;
constexpr std::size_t kMaxArchiveListingBytes = 16u * 1024u * 1024u;
std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}
bool hasRomExtension(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
        return false;
    }
    const std::string ext = toLower(path.substr(dot));
    static const std::array<const char*, 10> kExtensions = {
        ".sfc", ".smc", ".fig", ".swc", ".bin",
        ".rom", ".gd3", ".gd7", ".dx2", ".bs"
    };
    return std::find(kExtensions.begin(), kExtensions.end(), ext) != kExtensions.end();
}
int romPriority(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return 100;
    const std::string ext = toLower(path.substr(dot));
    if (ext == ".sfc") return 0;
    if (ext == ".smc") return 1;
    if (ext == ".fig") return 2;
    if (ext == ".swc") return 3;
    if (ext == ".bin") return 4;
    if (ext == ".rom") return 5;
    return 10;
}
bool loadZipWithMiniz(const std::string& archivePath, ArchivedRom& outRom, std::string& error) {
    mz_zip_archive zip = {};
    if (!mz_zip_reader_init_file(&zip, archivePath.c_str(), 0)) {
        error = "ZIP could not be opened.";
        return false;
    }
    struct Candidate {
        mz_uint index = 0;
        std::string name;
        int priority = 100;
    };
    std::vector<Candidate> candidates;
    bool skippedOversizedCandidate = false;
    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat = {};
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) continue;
        if (mz_zip_reader_is_file_a_directory(&zip, i)) continue;
        const std::string name = stat.m_filename;
        if (hasRomExtension(name) && stat.m_uncomp_size <= kMaxArchiveRomBytes) {
            candidates.push_back(Candidate{i, name, romPriority(name)});
        } else if (hasRomExtension(name)) {
            skippedOversizedCandidate = true;
        }
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.priority < b.priority;
    });
    if (candidates.empty()) {
        mz_zip_reader_end(&zip);
        error = skippedOversizedCandidate
            ? "ZIP contains recognized SNES ROM data larger than the 64 MiB safety limit."
            : "ZIP contains no recognized SNES ROM files.";
        return false;
    }
    for (const Candidate& candidate : candidates) {
        size_t size = 0;
        void* data = mz_zip_reader_extract_to_heap(&zip, candidate.index, &size, 0);
        if (data != nullptr && size != 0) {
            const auto* bytes = static_cast<const uint8_t*>(data);
            outRom.entryName = candidate.name;
            outRom.data.assign(bytes, bytes + size);
            mz_free(data);
            mz_zip_reader_end(&zip);
            return true;
        }
        if (data != nullptr) mz_free(data);
    }
    mz_zip_reader_end(&zip);
    error = "A ROM entry was found in the ZIP, but extraction failed.";
    return false;
}
#ifdef _WIN32
std::string quoteWindowsArg(const std::string& arg) {
    if (arg.empty()) return "\"\"";
    if (arg.find_first_of(" \t\n\v\"") == std::string::npos) return arg;
    std::string result;
    result.push_back('"');
    std::size_t backslashes = 0;
    for (char ch : arg) {
        if (ch == '\\') {
            ++backslashes;
            continue;
        }
        if (ch == '"') {
            result.append(backslashes * 2 + 1, '\\');
            result.push_back('"');
            backslashes = 0;
            continue;
        }
        result.append(backslashes, '\\');
        backslashes = 0;
        result.push_back(ch);
    }
    result.append(backslashes * 2, '\\');
    result.push_back('"');
    return result;
}
bool runTarCapture(const std::vector<std::string>& args, std::vector<uint8_t>& output,
                   DWORD& exitCode, std::size_t maxOutputBytes, bool& outputTooLarge) {
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) return false;
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writePipe;
    HANDLE nullHandle = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    si.hStdError = (nullHandle != INVALID_HANDLE_VALUE) ? nullHandle : GetStdHandle(STD_ERROR_HANDLE);
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi = {};
    std::string command = "tar.exe";
    for (const auto& arg : args) {
        command.push_back(' ');
        command += quoteWindowsArg(arg);
    }
    std::vector<char> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back('\0');
    const BOOL created = CreateProcessA(
        nullptr, mutableCommand.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(writePipe);
    if (nullHandle != INVALID_HANDLE_VALUE) CloseHandle(nullHandle);
    if (!created) {
        CloseHandle(readPipe);
        return false;
    }
    output.clear();
    outputTooLarge = false;
    std::array<uint8_t, 16384> buffer = {};
    for (;;) {
        DWORD bytesRead = 0;
        if (!ReadFile(readPipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr) || bytesRead == 0) {
            break;
        }
        if (bytesRead > maxOutputBytes - output.size()) {
            outputTooLarge = true;
            continue;
        }
        output.insert(output.end(), buffer.begin(), buffer.begin() + bytesRead);
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(readPipe);
    return true;
}
#else
bool runTarCapture(const std::vector<std::string>&, std::vector<uint8_t>&, unsigned long& exitCode,
                   std::size_t, bool& outputTooLarge) {
    exitCode = 1;
    outputTooLarge = false;
    return false;
}
#endif
std::vector<std::string> splitLines(const std::vector<uint8_t>& bytes) {
    std::vector<std::string> lines;
    std::string current;
    for (uint8_t byte : bytes) {
        const char ch = static_cast<char>(byte);
        if (ch == '\r') continue;
        if (ch == '\n') {
            if (!current.empty()) lines.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) lines.push_back(current);
    return lines;
}
bool loadWithTar(const std::string& archivePath, ArchivedRom& outRom, std::string& error) {
    std::vector<uint8_t> listingBytes;
#ifdef _WIN32
    DWORD listExit = 1;
#else
    unsigned long listExit = 1;
#endif
    bool listingTooLarge = false;
    if (!runTarCapture({"-tf", archivePath}, listingBytes, listExit, kMaxArchiveListingBytes, listingTooLarge)) {
        error = "Could not start the system archive tool.";
        return false;
    }
    if (listingTooLarge) {
        error = "Archive listing exceeds the 16 MiB safety limit.";
        return false;
    }
    if (listExit != 0) {
        error = "The archive could not be opened by the system archive tool.";
        return false;
    }
    std::vector<std::string> candidates;
    for (const auto& entry : splitLines(listingBytes)) {
        if (!entry.empty() && entry.back() != '/' && hasRomExtension(entry)) {
            candidates.push_back(entry);
        }
    }
    if (candidates.empty()) {
        error = "Archive contains no recognized SNES ROM files.";
        return false;
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const std::string& a, const std::string& b) {
        return romPriority(a) < romPriority(b);
    });
    bool skippedOversizedCandidate = false;
    for (const auto& entry : candidates) {
        std::vector<uint8_t> romBytes;
#ifdef _WIN32
        DWORD extractExit = 1;
#else
        unsigned long extractExit = 1;
#endif
        bool romTooLarge = false;
        if (!runTarCapture({"-xOf", archivePath, entry}, romBytes, extractExit,
                           kMaxArchiveRomBytes, romTooLarge)) continue;
        if (romTooLarge) {
            skippedOversizedCandidate = true;
            continue;
        }
        if (extractExit == 0 && !romBytes.empty()) {
            outRom.entryName = entry;
            outRom.data = std::move(romBytes);
            return true;
        }
    }
    error = skippedOversizedCandidate
        ? "Archive contains recognized SNES ROM data larger than the 64 MiB safety limit."
        : "A ROM entry was found, but it could not be extracted from the archive.";
    return false;
}
}
std::string ArchiveLoader::extensionLower(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
        return {};
    }
    return toLower(path.substr(dot));
}
bool ArchiveLoader::isArchivePath(const std::string& path) {
    const std::string ext = extensionLower(path);
    return ext == ".zip" || ext == ".rar" || ext == ".7z";
}
bool ArchiveLoader::isRomPath(const std::string& path) {
    return hasRomExtension(path);
}
bool ArchiveLoader::loadFirstRom(const std::string& archivePath, ArchivedRom& outRom, std::string& error) {
    outRom = {};
    error.clear();
    if (!isArchivePath(archivePath)) {
        error = "Not a supported archive type.";
        return false;
    }
    const std::string ext = extensionLower(archivePath);
    if (ext == ".zip") {
        if (loadZipWithMiniz(archivePath, outRom, error)) return true;
        std::string fallbackError;
        if (loadWithTar(archivePath, outRom, fallbackError)) return true;
        if (!fallbackError.empty()) error += " Fallback: " + fallbackError;
        return false;
    }
    return loadWithTar(archivePath, outRom, error);
}

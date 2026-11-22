#include "snes.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
namespace fs = std::filesystem;
namespace {
struct Options {
    fs::path rom;
    fs::path batch_dir;
    uint64_t frames = 60;
    uint64_t cycles = 0;
    bool use_cycles = false;
    bool json = false;
    bool quiet = false;
    bool persist_sram = false;
    bool fast_ppu = false;
    bool jit_ppu = false;
    fs::path dump_frame;
    fs::path dump_wram;
    fs::path dump_audio;
    std::string expected_hash;
    std::string expected_audio_hash;
    bool oracle_wram = false;
    uint64_t oracle_wram_offset = 0;
    uint8_t oracle_pass = 1;
    uint8_t oracle_fail = 0xFF;
    bool oracle_wram_range = false;
    uint64_t oracle_wram_range_offset = 0;
    uint64_t oracle_wram_range_length = 0;
    uint8_t oracle_range_pass = 0xFF;
    uint8_t oracle_range_fail = 0x00;
    bool oracle_spc_port = false;
    uint8_t oracle_spc_port_index = 0;
    uint8_t oracle_spc_pass = 1;
    std::vector<uint8_t> oracle_spc_fail_values;
    bool oracle_spc_after_ipl = false;
    uint64_t oracle_spc_required_passes = 1;
    bool stop_on_oracle = false;
    bool require_pass = false;
    std::vector<uint64_t> watch_wram_u16;
};
class CoutSilencer {
public:
    explicit CoutSilencer(bool enabled) : enabled_(enabled) {
        if (enabled_) old_ = std::cout.rdbuf(sink_.rdbuf());
    }
    ~CoutSilencer() {
        if (enabled_) std::cout.rdbuf(old_);
    }
    CoutSilencer(const CoutSilencer&) = delete;
    CoutSilencer& operator=(const CoutSilencer&) = delete;
private:
    bool enabled_ = false;
    std::streambuf* old_ = nullptr;
    std::ostringstream sink_;
};
struct RunResult {
    bool loaded = false;
    bool expectation_met = true;
    std::string path;
    std::string title;
    std::string mapper;
    std::string hardware;
    bool hardware_implemented = true;
    std::string hardware_support = "implemented";
    uint64_t frames = 0;
    uint64_t master_clocks = 0;
    uint64_t cpu_cycles = 0;
    uint64_t hash = 0;
    uint64_t audio_hash = 14695981039346656037ULL;
    uint64_t audio_frames = 0;
    int width = 0;
    int height = 0;
    CPU::State cpu{};
    std::string error;
    std::string oracle = "none";
    std::string status = "unclassified";
    int oracle_value = -1;
    uint64_t oracle_pass_count = 0;
    uint64_t oracle_fail_count = 0;
    uint64_t oracle_pending_count = 0;
    uint64_t oracle_spc_pass_events = 0;
    uint64_t oracle_spc_fail_events = 0;
    uint64_t oracle_spc_observed_writes = 0;
    uint64_t oracle_spc_last_event_cycle = 0;
    bool spc_ipl_enabled = false;
    std::array<uint8_t, 4> spc_ports{};
    uint64_t visible_nonblack_pixels = 0;
    bool forced_blank = true;
    uint8_t brightness = 0;
    uint8_t bg_mode = 0;
    uint8_t main_screen_mask = 0;
    uint8_t sub_screen_mask = 0;
    uint8_t nmitimen = 0;
    uint8_t irq_mode = 0;
    int ppu_scanline = 0;
    int ppu_dot = 0;
    struct WramWatch { uint64_t offset = 0; uint16_t value = 0; uint64_t changes = 0; };
    std::vector<WramWatch> wram_watches;
};
void usage(const char* argv0) {
    std::cout
        << "SNESUltimate headless regression runner\n\n"
        << "Usage:\n"
        << "  " << argv0 << " [options] ROM\n"
        << "  " << argv0 << " [options] --batch DIRECTORY\n\n"
        << "Execution:\n"
        << "  --frames N          Run N video frames (default: 60)\n"
        << "  --cycles N          Run N master clocks instead of frames\n"
        << "  --expect-hash HEX   Exit nonzero if visible framebuffer hash differs\n"
        << "  --expect-audio-hash HEX\n"
        << "                      Exit nonzero if deterministic PCM hash differs\n"
        << "  --oracle-wram O:P:F Classify WRAM byte O as pass value P / fail value F\n"
        << "  --oracle-wram-range O:L:P:F\n"
        << "                      Classify WRAM range: all P = pass; resolved F = fail\n"
        << "  --oracle-spc-port P:PASS:FAIL[,FAIL...]\n"
        << "                      Classify SPC->S-CPU port writes (event-driven)\n"
        << "  --oracle-spc-after-ipl\n"
        << "                      Ignore SPC status values while the IPL ROM is mapped\n"
        << "  --oracle-spc-pass-count N\n"
        << "                      Require N distinct SPC pass events (default: 1)\n"
        << "  --stop-on-oracle    Stop frame execution as soon as an oracle resolves\n"
        << "  --require-pass      Exit nonzero unless an oracle resolves to pass\n"
        << "  --watch-wram-u16 O  Report little-endian WRAM u16 at offset O; repeatable\n\n"
        << "Output:\n"
        << "  --json              Print one JSON object per ROM\n"
        << "  --quiet             Suppress normal human-readable result output\n"
        << "  --dump-frame PATH   Write final visible framebuffer as binary PPM (single ROM)\n"
        << "  --dump-wram PATH    Write final 128 KiB WRAM image (single ROM)\n"
        << "  --dump-audio PATH   Write all generated PCM as little-endian s16 stereo\n"
        << "  --persist-sram      Enable normal .srm loading/saving (off by default)\n"
        << "  --fast-ppu          Use the experimental fast scanline renderer\n"
        << "  --jit-ppu           Use the experimental AsmJit line compositor\n"
        << "  --help              Show this help\n\n"
        << "Framebuffer hash is 64-bit FNV-1a over width, height, then visible RGBA32 pixels.\n"
        << "Audio hash is 64-bit FNV-1a over every generated little-endian s16 stereo sample.\n";
}
bool parseU64(const std::string& text, uint64_t& out) {
    try {
        std::size_t used = 0;
        const unsigned long long value = std::stoull(text, &used, 0);
        if (used != text.size()) return false;
        out = static_cast<uint64_t>(value);
        return true;
    } catch (...) {
        return false;
    }
}
bool parseOracleWram(const std::string& text, Options& opt) {
    const auto a = text.find(':');
    const auto b = a == std::string::npos ? std::string::npos : text.find(':', a + 1);
    if (a == std::string::npos || b == std::string::npos) return false;
    uint64_t off = 0, pass = 0, fail = 0;
    if (!parseU64(text.substr(0, a), off) || !parseU64(text.substr(a + 1, b - a - 1), pass) || !parseU64(text.substr(b + 1), fail)) return false;
    if (off >= 128 * 1024 || pass > 0xFF || fail > 0xFF || pass == fail) return false;
    opt.oracle_wram = true;
    opt.oracle_wram_offset = off;
    opt.oracle_pass = static_cast<uint8_t>(pass);
    opt.oracle_fail = static_cast<uint8_t>(fail);
    return true;
}
bool parseOracleWramRange(const std::string& text, Options& opt) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        const auto pos = text.find(':', start);
        parts.push_back(text.substr(start, pos == std::string::npos ? pos : pos - start));
        if (pos == std::string::npos) break;
        start = pos + 1;
    }
    if (parts.size() != 4) return false;
    uint64_t off = 0, len = 0, pass = 0, fail = 0;
    if (!parseU64(parts[0], off) || !parseU64(parts[1], len) || !parseU64(parts[2], pass) || !parseU64(parts[3], fail)) return false;
    if (len == 0 || off >= 128 * 1024 || len > 128 * 1024 || off + len > 128 * 1024 || pass > 0xFF || fail > 0xFF || pass == fail) return false;
    opt.oracle_wram_range = true;
    opt.oracle_wram_range_offset = off;
    opt.oracle_wram_range_length = len;
    opt.oracle_range_pass = static_cast<uint8_t>(pass);
    opt.oracle_range_fail = static_cast<uint8_t>(fail);
    return true;
}
bool parseOracleSpcPort(const std::string& text, Options& opt) {
    const auto a = text.find(':');
    const auto b = a == std::string::npos ? std::string::npos : text.find(':', a + 1);
    if (a == std::string::npos || b == std::string::npos) return false;
    uint64_t port = 0, pass = 0;
    if (!parseU64(text.substr(0, a), port) || !parseU64(text.substr(a + 1, b - a - 1), pass)) return false;
    if (port > 3 || pass > 0xFF) return false;
    std::vector<uint8_t> fails;
    std::size_t start = b + 1;
    while (start <= text.size()) {
        const auto comma = text.find(',', start);
        const std::string token = text.substr(start, comma == std::string::npos ? comma : comma - start);
        uint64_t value = 0;
        if (token.empty() || !parseU64(token, value) || value > 0xFF || value == pass) return false;
        const uint8_t byte = static_cast<uint8_t>(value);
        if (std::find(fails.begin(), fails.end(), byte) == fails.end()) fails.push_back(byte);
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    if (fails.empty()) return false;
    opt.oracle_spc_port = true;
    opt.oracle_spc_port_index = static_cast<uint8_t>(port);
    opt.oracle_spc_pass = static_cast<uint8_t>(pass);
    opt.oracle_spc_fail_values = std::move(fails);
    return true;
}
bool oracleConfigured(const Options& opt) {
    return opt.oracle_wram || opt.oracle_wram_range || opt.oracle_spc_port;
}
bool parseArgs(int argc, char** argv, Options& opt) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << name << " requires a value.\n";
                return nullptr;
            }
            return argv[++i];
        };
        if (arg == "--help" || arg == "-h") {
            usage(argv[0]);
            std::exit(0);
        } else if (arg == "--frames") {
            const char* v = value("--frames");
            if (!v || !parseU64(v, opt.frames)) return false;
            opt.use_cycles = false;
        } else if (arg == "--cycles") {
            const char* v = value("--cycles");
            if (!v || !parseU64(v, opt.cycles)) return false;
            opt.use_cycles = true;
        } else if (arg == "--expect-hash") {
            const char* v = value("--expect-hash");
            if (!v) return false;
            opt.expected_hash = v;
            std::transform(opt.expected_hash.begin(), opt.expected_hash.end(), opt.expected_hash.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (opt.expected_hash.rfind("0x", 0) == 0) opt.expected_hash.erase(0, 2);
        } else if (arg == "--expect-audio-hash") {
            const char* v = value("--expect-audio-hash");
            if (!v) return false;
            opt.expected_audio_hash = v;
            std::transform(opt.expected_audio_hash.begin(), opt.expected_audio_hash.end(), opt.expected_audio_hash.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (opt.expected_audio_hash.rfind("0x", 0) == 0) opt.expected_audio_hash.erase(0, 2);
        } else if (arg == "--oracle-wram") {
            const char* v = value("--oracle-wram");
            if (!v || !parseOracleWram(v, opt)) {
                std::cerr << "--oracle-wram expects OFFSET:PASS:FAIL (numbers accept 0x prefix).\n";
                return false;
            }
        } else if (arg == "--oracle-wram-range") {
            const char* v = value("--oracle-wram-range");
            if (!v || !parseOracleWramRange(v, opt)) {
                std::cerr << "--oracle-wram-range expects OFFSET:LENGTH:PASS:FAIL.\n";
                return false;
            }
        } else if (arg == "--oracle-spc-port") {
            const char* v = value("--oracle-spc-port");
            if (!v || !parseOracleSpcPort(v, opt)) {
                std::cerr << "--oracle-spc-port expects PORT:PASS:FAIL[,FAIL...] with PORT 0..3.\n";
                return false;
            }
        } else if (arg == "--oracle-spc-after-ipl") {
            opt.oracle_spc_after_ipl = true;
        } else if (arg == "--oracle-spc-pass-count") {
            const char* v = value("--oracle-spc-pass-count");
            if (!v || !parseU64(v, opt.oracle_spc_required_passes) || opt.oracle_spc_required_passes == 0) {
                std::cerr << "--oracle-spc-pass-count expects a positive integer.\n";
                return false;
            }
        } else if (arg == "--watch-wram-u16") {
            const char* v = value("--watch-wram-u16");
            uint64_t off = 0;
            if (!v || !parseU64(v, off) || off + 1 >= 128 * 1024) {
                std::cerr << "--watch-wram-u16 expects a WRAM byte offset with room for two bytes.\n";
                return false;
            }
            opt.watch_wram_u16.push_back(off);
        } else if (arg == "--stop-on-oracle") {
            opt.stop_on_oracle = true;
        } else if (arg == "--require-pass") {
            opt.require_pass = true;
        } else if (arg == "--dump-frame") {
            const char* v = value("--dump-frame");
            if (!v) return false;
            opt.dump_frame = v;
        } else if (arg == "--dump-wram") {
            const char* v = value("--dump-wram");
            if (!v) return false;
            opt.dump_wram = v;
        } else if (arg == "--dump-audio") {
            const char* v = value("--dump-audio");
            if (!v) return false;
            opt.dump_audio = v;
        } else if (arg == "--batch") {
            const char* v = value("--batch");
            if (!v) return false;
            opt.batch_dir = v;
        } else if (arg == "--json") {
            opt.json = true;
        } else if (arg == "--quiet") {
            opt.quiet = true;
        } else if (arg == "--persist-sram") {
            opt.persist_sram = true;
        } else if (arg == "--fast-ppu") {
            opt.fast_ppu = true;
        } else if (arg == "--jit-ppu") {
            opt.jit_ppu = true;
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "Unknown option: " << arg << '\n';
            return false;
        } else if (opt.rom.empty()) {
            opt.rom = arg;
        } else {
            std::cerr << "Only one ROM path may be supplied.\n";
            return false;
        }
    }
    if (opt.rom.empty() == opt.batch_dir.empty()) {
        std::cerr << "Supply exactly one ROM or --batch DIRECTORY.\n";
        return false;
    }
    const unsigned oracle_count = static_cast<unsigned>(opt.oracle_wram) + static_cast<unsigned>(opt.oracle_wram_range) + static_cast<unsigned>(opt.oracle_spc_port);
    if (oracle_count > 1) {
        std::cerr << "Configure only one oracle per run.\n";
        return false;
    }
    if ((opt.oracle_spc_after_ipl || opt.oracle_spc_required_passes != 1) && !opt.oracle_spc_port) {
        std::cerr << "SPC oracle modifiers require --oracle-spc-port.\n";
        return false;
    }
    if (opt.require_pass && !oracleConfigured(opt)) {
        std::cerr << "--require-pass requires a configured oracle.\n";
        return false;
    }
    if (!opt.batch_dir.empty() && (!opt.dump_frame.empty() || !opt.dump_wram.empty() || !opt.dump_audio.empty() || !opt.expected_hash.empty() || !opt.expected_audio_hash.empty() || oracleConfigured(opt) || opt.require_pass)) {
        std::cerr << "Dump and expected-hash options are single-ROM options.\n";
        return false;
    }
    return true;
}
uint64_t fnvByte(uint64_t hash, uint8_t value) {
    constexpr uint64_t prime = 1099511628211ULL;
    return (hash ^ value) * prime;
}
uint64_t hashAudioSamples(uint64_t hash, const std::vector<int16_t>& samples) {
    for (int16_t sample : samples) {
        const uint16_t bits = static_cast<uint16_t>(sample);
        hash = fnvByte(hash, static_cast<uint8_t>(bits));
        hash = fnvByte(hash, static_cast<uint8_t>(bits >> 8));
    }
    return hash;
}
uint64_t framebufferHash(const PPU& ppu) {
    uint64_t hash = 14695981039346656037ULL;
    const uint32_t width = static_cast<uint32_t>(ppu.width());
    const uint32_t height = static_cast<uint32_t>(ppu.height());
    for (int shift = 0; shift < 32; shift += 8) hash = fnvByte(hash, static_cast<uint8_t>(width >> shift));
    for (int shift = 0; shift < 32; shift += 8) hash = fnvByte(hash, static_cast<uint8_t>(height >> shift));
    const uint32_t* pixels = ppu.framebuffer();
    const int stride = ppu.framebufferStride();
    for (int y = 0; y < ppu.height(); ++y) {
        for (int x = 0; x < ppu.width(); ++x) {
            const uint32_t px = pixels[static_cast<std::size_t>(y) * stride + x];
            hash = fnvByte(hash, static_cast<uint8_t>(px));
            hash = fnvByte(hash, static_cast<uint8_t>(px >> 8));
            hash = fnvByte(hash, static_cast<uint8_t>(px >> 16));
            hash = fnvByte(hash, static_cast<uint8_t>(px >> 24));
        }
    }
    return hash;
}
uint64_t nonBlackPixelCount(const PPU& ppu) {
    uint64_t count = 0;
    const uint32_t* pixels = ppu.framebuffer();
    const int stride = ppu.framebufferStride();
    for (int y = 0; y < ppu.height(); ++y) {
        for (int x = 0; x < ppu.width(); ++x) {
            if (pixels[static_cast<std::size_t>(y) * stride + x] != 0xFF000000u) ++count;
        }
    }
    return count;
}
std::string hashString(uint64_t hash) {
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << hash;
    return out.str();
}
std::string mapperName(MapperType mapper) {
    switch (mapper) {
    case MapperType::LoROM: return "LoROM";
    case MapperType::ExLoROM: return "ExLoROM";
    case MapperType::HiROM: return "HiROM";
    case MapperType::ExHiROM: return "ExHiROM";
    }
    return "Unknown";
}
std::string jsonEscape(const std::string& s) {
    std::ostringstream out;
    for (unsigned char c : s) {
        switch (c) {
        case '\\': out << "\\\\"; break;
        case '"': out << "\\\""; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (c < 0x20 || c >= 0x80)
                out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec;
            else
                out << static_cast<char>(c);
        }
    }
    return out.str();
}
bool writeFramePPM(const fs::path& path, const PPU& ppu, std::string& error) {
    std::ofstream out(path, std::ios::binary);
    if (!out) { error = "could not open framebuffer output"; return false; }
    out << "P6\n" << ppu.width() << ' ' << ppu.height() << "\n255\n";
    const uint32_t* pixels = ppu.framebuffer();
    const int stride = ppu.framebufferStride();
    for (int y = 0; y < ppu.height(); ++y) {
        for (int x = 0; x < ppu.width(); ++x) {
            const uint32_t px = pixels[static_cast<std::size_t>(y) * stride + x];
            const char rgb[3] = {
                static_cast<char>(px & 0xFF),
                static_cast<char>((px >> 8) & 0xFF),
                static_cast<char>((px >> 16) & 0xFF)
            };
            out.write(rgb, 3);
        }
    }
    if (!out.good()) { error = "failed while writing framebuffer"; return false; }
    return true;
}
bool writeAudioPCM(const fs::path& path, const std::vector<int16_t>& samples, std::string& error) {
    std::ofstream out(path, std::ios::binary);
    if (!out) { error = "could not open audio output"; return false; }
    for (int16_t sample : samples) {
        const uint16_t bits = static_cast<uint16_t>(sample);
        const char bytes[2] = {static_cast<char>(bits & 0xFF), static_cast<char>(bits >> 8)};
        out.write(bytes, 2);
    }
    if (!out.good()) { error = "failed while writing audio output"; return false; }
    return true;
}
bool writeBinary(const fs::path& path, const std::vector<uint8_t>& data, std::string& error) {
    std::ofstream out(path, std::ios::binary);
    if (!out) { error = "could not open binary output"; return false; }
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!out.good()) { error = "failed while writing binary output"; return false; }
    return true;
}
RunResult runOne(const fs::path& rom, const Options& opt, bool allow_dumps) {
    RunResult r;
    CoutSilencer silence_core_output(opt.json || opt.quiet);
    r.path = rom.string();
    auto snesStorage = std::make_unique<SNES>();
    SNES& snes = *snesStorage;
    snes.cartridge().setSRAMPersistence(opt.persist_sram);
    snes.ppu().setFastScanline(opt.fast_ppu);
    snes.ppu().setJitScanline(opt.jit_ppu);
    if (!snes.loadROM(r.path)) {
        r.error = "ROM load failed";
        return r;
    }
    r.loaded = true;
    std::vector<int16_t> captured_audio;
    auto collectAudio = [&]() {
        auto samples = snes.apu().takeAudioSamples();
        r.audio_hash = hashAudioSamples(r.audio_hash, samples);
        r.audio_frames += samples.size() / 2;
        if (!opt.dump_audio.empty()) captured_audio.insert(captured_audio.end(), samples.begin(), samples.end());
    };
    if (opt.oracle_spc_port) {
        snes.apu().setPortWriteObserver([&](uint8_t port, uint8_t value, bool ipl_enabled, uint64_t spc_cycle) {
            if (port != opt.oracle_spc_port_index) return;
            ++r.oracle_spc_observed_writes;
            r.oracle_value = value;
            r.oracle_spc_last_event_cycle = spc_cycle;
            const bool eligible = !opt.oracle_spc_after_ipl || !ipl_enabled;
            if (!eligible) return;
            if (value == opt.oracle_spc_pass) {
                ++r.oracle_spc_pass_events;
                if (r.oracle_spc_pass_events >= opt.oracle_spc_required_passes) r.status = "pass";
                else if (r.status != "fail") r.status = "running";
                return;
            }
            if (std::find(opt.oracle_spc_fail_values.begin(), opt.oracle_spc_fail_values.end(), value) != opt.oracle_spc_fail_values.end()) {
                ++r.oracle_spc_fail_events;
                r.status = "fail";
                for (uint8_t i = 0; i < 4; ++i) r.spc_ports[i] = snes.apu().readPort(i);
            }
        });
    }
    for (uint64_t off : opt.watch_wram_u16) {
        RunResult::WramWatch watch;
        watch.offset = off;
        r.wram_watches.push_back(watch);
    }
    auto updateWramWatches = [&]() {
        const auto& wram = snes.bus().wram();
        for (auto& watch : r.wram_watches) {
            const uint16_t value = static_cast<uint16_t>(wram[static_cast<std::size_t>(watch.offset)]) |
                                   (static_cast<uint16_t>(wram[static_cast<std::size_t>(watch.offset + 1)]) << 8);
            if (value != watch.value) {
                ++watch.changes;
                watch.value = value;
            }
        }
    };
    auto updateOracle = [&](bool final_sample) {
        if (opt.oracle_wram) {
            r.oracle = "wram-byte";
            const uint8_t value = snes.bus().wram()[static_cast<std::size_t>(opt.oracle_wram_offset)];
            r.oracle_value = value;
            if (value == opt.oracle_pass) r.status = "pass";
            else if (value == opt.oracle_fail) r.status = "fail";
            else r.status = "running";
            return;
        }
        if (opt.oracle_spc_port) {
            r.oracle = "spc-port-events";
            r.oracle_value = snes.apu().readPort(opt.oracle_spc_port_index);
            if (r.status != "fail" && r.oracle_spc_pass_events >= opt.oracle_spc_required_passes) r.status = "pass";
            else if (r.status != "fail" && r.status != "pass") r.status = "running";
            return;
        }
        if (opt.oracle_wram_range) {
            r.oracle = "wram-range";
            r.oracle_pass_count = 0;
            r.oracle_fail_count = 0;
            r.oracle_pending_count = 0;
            const auto& wram = snes.bus().wram();
            for (uint64_t i = 0; i < opt.oracle_wram_range_length; ++i) {
                const uint8_t value = wram[static_cast<std::size_t>(opt.oracle_wram_range_offset + i)];
                if (value == opt.oracle_range_pass) ++r.oracle_pass_count;
                else if (value == opt.oracle_range_fail) ++r.oracle_fail_count;
                else ++r.oracle_pending_count;
            }
            if (r.oracle_pass_count == opt.oracle_wram_range_length) r.status = "pass";
            else if (final_sample && r.oracle_pending_count == 0 && r.oracle_pass_count != 0 && r.oracle_fail_count != 0) r.status = "fail";
            else r.status = "running";
        }
    };
    if (opt.use_cycles) {
        snes.runCycles(opt.cycles);
        collectAudio();
        updateWramWatches();
        updateOracle(true);
    } else {
        for (uint64_t i = 0; i < opt.frames; ++i) {
            snes.runFrame();
            collectAudio();
            updateWramWatches();
            updateOracle(false);
            if (opt.stop_on_oracle && (r.status == "pass" || r.status == "fail")) break;
        }
        updateWramWatches();
        updateOracle(true);
    }
    r.frames = snes.frameCount();
    r.master_clocks = snes.scheduler().now();
    r.cpu_cycles = snes.cpu().cycles();
    r.hash = framebufferHash(snes.ppu());
    r.width = snes.ppu().width();
    r.height = snes.ppu().height();
    r.cpu = snes.cpu().state();
    r.visible_nonblack_pixels = nonBlackPixelCount(snes.ppu());
    r.forced_blank = snes.ppu().forcedBlank();
    r.brightness = snes.ppu().brightness();
    r.bg_mode = snes.ppu().bgMode();
    r.main_screen_mask = snes.ppu().mainScreenMask();
    r.sub_screen_mask = snes.ppu().subScreenMask();
    r.nmitimen = snes.bus().nmitimen();
    r.irq_mode = snes.bus().irqMode();
    r.ppu_scanline = snes.ppu().scanline();
    r.ppu_dot = snes.ppu().dot();
    if (r.oracle_spc_fail_events == 0) {
        for (uint8_t port = 0; port < 4; ++port) r.spc_ports[port] = snes.apu().readPort(port);
    }
    r.spc_ipl_enabled = snes.apu().iplEnabled();
    r.title = snes.cartridge().title();
    r.mapper = mapperName(snes.cartridge().mapperType());
    r.hardware = snes.cartridge().hardwareName();
    r.hardware_implemented = snes.cartridge().hardwareImplemented();
    r.hardware_support = snes.cartridge().hardwareSupportName();
    if (!opt.expected_hash.empty()) r.expectation_met = hashString(r.hash) == opt.expected_hash;
    if (!opt.expected_audio_hash.empty()) r.expectation_met = r.expectation_met && hashString(r.audio_hash) == opt.expected_audio_hash;
    if (oracleConfigured(opt) && r.status == "fail") r.expectation_met = false;
    if (opt.require_pass && r.status != "pass") r.expectation_met = false;
    if (allow_dumps) {
        std::string error;
        if (!opt.dump_frame.empty() && !writeFramePPM(opt.dump_frame, snes.ppu(), error)) r.error += (r.error.empty() ? "" : "; ") + error;
        if (!opt.dump_wram.empty() && !writeBinary(opt.dump_wram, snes.bus().wram(), error)) r.error += (r.error.empty() ? "" : "; ") + error;
        if (!opt.dump_audio.empty() && !writeAudioPCM(opt.dump_audio, captured_audio, error)) r.error += (r.error.empty() ? "" : "; ") + error;
    }
    return r;
}
void printJson(const RunResult& r) {
    std::cout << '{'
        << "\"path\":\"" << jsonEscape(r.path) << "\","
        << "\"loaded\":" << (r.loaded ? "true" : "false") << ','
        << "\"title\":\"" << jsonEscape(r.title) << "\","
        << "\"mapper\":\"" << r.mapper << "\","
        << "\"hardware\":\"" << jsonEscape(r.hardware) << "\","
        << "\"hardware_implemented\":" << (r.hardware_implemented ? "true" : "false") << ','
        << "\"hardware_support\":\"" << jsonEscape(r.hardware_support) << "\","
        << "\"frames\":" << r.frames << ','
        << "\"master_clocks\":" << r.master_clocks << ','
        << "\"cpu_cycles\":" << r.cpu_cycles << ','
        << "\"width\":" << r.width << ','
        << "\"height\":" << r.height << ','
        << "\"framebuffer_hash\":\"" << hashString(r.hash) << "\","
        << "\"audio_hash\":\"" << hashString(r.audio_hash) << "\","
        << "\"audio_frames\":" << r.audio_frames << ','
        << "\"pc\":" << r.cpu.pc << ','
        << "\"pb\":" << static_cast<unsigned>(r.cpu.pb) << ','
        << "\"expectation_met\":" << (r.expectation_met ? "true" : "false") << ','
        << "\"oracle\":\"" << r.oracle << "\","
        << "\"status\":\"" << r.status << "\","
        << "\"oracle_value\":" << r.oracle_value << ','
        << "\"oracle_pass_count\":" << r.oracle_pass_count << ','
        << "\"oracle_fail_count\":" << r.oracle_fail_count << ','
        << "\"oracle_pending_count\":" << r.oracle_pending_count << ','
        << "\"oracle_spc_pass_events\":" << r.oracle_spc_pass_events << ','
        << "\"oracle_spc_fail_events\":" << r.oracle_spc_fail_events << ','
        << "\"oracle_spc_observed_writes\":" << r.oracle_spc_observed_writes << ','
        << "\"oracle_spc_last_event_cycle\":" << r.oracle_spc_last_event_cycle << ','
        << "\"spc_ipl_enabled\":" << (r.spc_ipl_enabled ? "true" : "false") << ','
        << "\"spc_ports\":[" << static_cast<unsigned>(r.spc_ports[0]) << ','
        << static_cast<unsigned>(r.spc_ports[1]) << ',' << static_cast<unsigned>(r.spc_ports[2]) << ','
        << static_cast<unsigned>(r.spc_ports[3]) << "],";
    std::cout << "\"wram_u16_watches\":[";
    for (std::size_t i = 0; i < r.wram_watches.size(); ++i) {
        if (i) std::cout << ',';
        const auto& watch = r.wram_watches[i];
        std::cout << "{\"offset\":" << watch.offset << ",\"value\":" << watch.value
                  << ",\"changes\":" << watch.changes << '}';
    }
    std::cout << "],"
        << "\"visible_nonblack_pixels\":" << r.visible_nonblack_pixels << ','
        << "\"forced_blank\":" << (r.forced_blank ? "true" : "false") << ','
        << "\"brightness\":" << static_cast<unsigned>(r.brightness) << ','
        << "\"bg_mode\":" << static_cast<unsigned>(r.bg_mode) << ','
        << "\"main_screen_mask\":" << static_cast<unsigned>(r.main_screen_mask) << ','
        << "\"sub_screen_mask\":" << static_cast<unsigned>(r.sub_screen_mask) << ','
        << "\"nmitimen\":" << static_cast<unsigned>(r.nmitimen) << ','
        << "\"irq_mode\":" << static_cast<unsigned>(r.irq_mode) << ','
        << "\"ppu_scanline\":" << r.ppu_scanline << ','
        << "\"ppu_dot\":" << r.ppu_dot << ','
        << "\"error\":\"" << jsonEscape(r.error) << "\""
        << "}\n";
}
void printHuman(const RunResult& r) {
    if (!r.loaded) {
        std::cout << "FAIL  " << r.path << "  " << r.error << '\n';
        return;
    }
    std::cout << (r.expectation_met && r.error.empty() ? "OK    " : "FAIL  ") << r.path
              << "\n      title=\"" << r.title << "\" mapper=" << r.mapper
              << " hardware=\"" << r.hardware << "\""
              << " support=\"" << r.hardware_support << "\""
              << (r.hardware_implemented ? "" : " [NOT IMPLEMENTED]")
              << " frames=" << r.frames << " master_clocks=" << r.master_clocks
              << " cpu_cycles=" << r.cpu_cycles << "\n      framebuffer=" << r.width << 'x' << r.height
              << " hash=" << hashString(r.hash)
              << " audio_frames=" << r.audio_frames << " audio_hash=" << hashString(r.audio_hash)
              << " cpu=" << std::hex << std::setfill('0')
              << " PB:PC=" << std::setw(2) << static_cast<unsigned>(r.cpu.pb) << ':' << std::setw(4) << r.cpu.pc
              << " A=" << std::setw(4) << r.cpu.a << " X=" << std::setw(4) << r.cpu.x << " Y=" << std::setw(4) << r.cpu.y
              << std::dec
              << "\n      video: nonblack=" << r.visible_nonblack_pixels
              << " forced_blank=" << (r.forced_blank ? "yes" : "no")
              << " brightness=" << static_cast<unsigned>(r.brightness)
              << " mode=" << static_cast<unsigned>(r.bg_mode)
              << " TM=0x" << std::hex << static_cast<unsigned>(r.main_screen_mask)
              << " TS=0x" << static_cast<unsigned>(r.sub_screen_mask)
              << " NMITIMEN=0x" << static_cast<unsigned>(r.nmitimen) << std::dec;
    if (r.oracle != "none") {
        std::cout << " oracle=" << r.oracle << " status=" << r.status;
        if (r.oracle_value >= 0) std::cout << " value=" << r.oracle_value;
        if (r.oracle == "wram-range") std::cout << " pass=" << r.oracle_pass_count << " fail=" << r.oracle_fail_count << " pending=" << r.oracle_pending_count;
        if (r.oracle == "spc-port-events") std::cout << " pass_events=" << r.oracle_spc_pass_events << " fail_events=" << r.oracle_spc_fail_events << " writes=" << r.oracle_spc_observed_writes << " ipl=" << (r.spc_ipl_enabled ? "on" : "off") << " ports=[" << static_cast<unsigned>(r.spc_ports[0]) << ',' << static_cast<unsigned>(r.spc_ports[1]) << ',' << static_cast<unsigned>(r.spc_ports[2]) << ',' << static_cast<unsigned>(r.spc_ports[3]) << ']';
    }
    if (!r.error.empty()) std::cout << " error=\"" << r.error << '"';
    std::cout << '\n';
}
bool romExtension(const fs::path& p) {
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".sfc" || ext == ".smc" || ext == ".fig" || ext == ".swc" || ext == ".bin" || ext == ".rom" ||
           ext == ".zip" || ext == ".rar" || ext == ".7z";
}
}
int main(int argc, char** argv) {
    Options opt;
    if (!parseArgs(argc, argv, opt)) {
        usage(argv[0]);
        return 2;
    }
    int failures = 0;
    if (!opt.batch_dir.empty()) {
        if (!fs::exists(opt.batch_dir) || !fs::is_directory(opt.batch_dir)) {
            std::cerr << "Batch path is not a directory: " << opt.batch_dir << '\n';
            return 2;
        }
        std::vector<fs::path> roms;
        for (const auto& entry : fs::recursive_directory_iterator(opt.batch_dir, fs::directory_options::skip_permission_denied)) {
            if (entry.is_regular_file() && romExtension(entry.path())) roms.push_back(entry.path());
        }
        std::sort(roms.begin(), roms.end());
        if (roms.empty()) {
            std::cerr << "No recognized ROM files found in " << opt.batch_dir << '\n';
            return 2;
        }
        for (const auto& rom : roms) {
            const RunResult r = runOne(rom, opt, false);
            if (opt.json) printJson(r);
            else if (!opt.quiet) printHuman(r);
            if (!r.loaded || !r.expectation_met || !r.error.empty()) ++failures;
        }
        if (!opt.json && !opt.quiet) std::cout << "Batch complete: " << roms.size() << " ROM(s), " << failures << " runner failure(s).\n";
    } else {
        const RunResult r = runOne(opt.rom, opt, true);
        if (opt.json) printJson(r);
        else if (!opt.quiet) printHuman(r);
        if (!r.loaded || !r.expectation_met || !r.error.empty()) failures = 1;
    }
    return failures == 0 ? 0 : 1;
}

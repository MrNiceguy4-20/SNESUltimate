#include "ppu.hpp"
#include "bus.hpp"
#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <asmjit/x86.h>
namespace {
constexpr uint8_t kTransparent = 0xFF;
constexpr std::array<uint64_t, 256> makeBitplaneExpandTable() {
    std::array<uint64_t, 256> table{};
    for (unsigned value = 0; value < 256; ++value) {
        uint64_t packed = 0;
        for (unsigned x = 0; x < 8; ++x) {
            const uint64_t bit = (value >> (7u - x)) & 1u;
            packed |= bit << (x * 8u);
        }
        table[value] = packed;
    }
    return table;
}
constexpr auto kBitplaneExpand = makeBitplaneExpandTable();
constexpr std::array<uint8_t, 16 * 32> makeBrightnessComponentTable() {
    std::array<uint8_t, 16 * 32> table{};
    for (unsigned brightness = 0; brightness < 16; ++brightness) {
        for (unsigned component = 0; component < 32; ++component) {
            const unsigned expanded = (component * 255u + 15u) / 31u;
            table[brightness * 32u + component] =
                static_cast<uint8_t>((expanded * brightness) / 15u);
        }
    }
    return table;
}
constexpr auto kBrightnessComponent = makeBrightnessComponentTable();
std::array<uint32_t, 16 * 32768> makeBgr555RgbaTable() {
    std::array<uint32_t, 16 * 32768> table{};
    for (unsigned brightness = 0; brightness < 16; ++brightness) {
        const size_t componentBase = brightness * 32u;
        for (unsigned color = 0; color < 32768; ++color) {
            const uint8_t r = kBrightnessComponent[componentBase + (color & 0x1Fu)];
            const uint8_t g = kBrightnessComponent[componentBase + ((color >> 5) & 0x1Fu)];
            const uint8_t b = kBrightnessComponent[componentBase + ((color >> 10) & 0x1Fu)];
            table[brightness * 32768u + color] = static_cast<uint32_t>(r) |
                (static_cast<uint32_t>(g) << 8) | (static_cast<uint32_t>(b) << 16) |
                0xFF000000u;
        }
    }
    return table;
}
const auto kBgr555Rgba = makeBgr555RgbaTable();
int backgroundBpp(uint8_t mode, int bg) {
    switch (mode & 7) {
    case 0: return 2;
    case 1: return bg < 2 ? 4 : (bg == 2 ? 2 : 0);
    case 2: return bg < 2 ? 4 : 0;
    case 3: return bg == 0 ? 8 : (bg == 1 ? 4 : 0);
    case 4: return bg == 0 ? 8 : (bg == 1 ? 2 : 0);
    case 5: return bg == 0 ? 4 : (bg == 1 ? 2 : 0);
    case 6: return bg == 0 ? 4 : 0;
    default: return 0;
    }
}
int priorityFor(uint8_t mode, int bg, bool high, bool bg3Priority) {
    switch (mode & 7) {
    case 0:
        if (bg == 0) return high ? 12 : 9;
        if (bg == 1) return high ? 11 : 8;
        if (bg == 2) return high ? 6 : 3;
        if (bg == 3) return high ? 5 : 2;
        return 0;
    case 1:
        if (bg == 0) return high ? 12 : 9;
        if (bg == 1) return high ? 11 : 8;
        if (bg == 2) return high ? (bg3Priority ? 14 : 6) : 3;
        return 0;
    case 2: case 3: case 4: case 5:
        if (bg == 0) return high ? 12 : 6;
        if (bg == 1) return high ? 9 : 3;
        return 0;
    case 6:
        return bg == 0 ? (high ? 12 : 6) : 0;
    default:
        return 0;
    }
}
}
struct PPU::JitState {
    asmjit::JitRuntime runtime;
    std::unordered_map<uint32_t, void*> kernels;
    std::array<std::array<uint8_t, 256>, 4> index;
    std::array<std::array<uint8_t, 256>, 4> priority;
    std::array<std::array<uint8_t, 256>, 4> palette;
};
PPU::PPU() { power(); }
PPU::~PPU() = default;
void PPU::power() {
    vram_.fill(0);
    cgram_.fill(0);
    oam_.fill(0);
    reset();
}
void PPU::reset() {
    framebuffer_.fill(0xFF000000u);
    for (auto& level : cgram_rgba_cache_) level.fill(0xFF000000u);
    registers_.fill(0);
    bgsc_.fill(0);
    bg_hofs_.fill(0);
    bg_vofs_.fill(0);
    mode1_row_generation_ = 1;
    for (auto& cache : mode1_row_cache_) cache = Mode1RowCache{};
    for (auto& cache : bg_row_cache_) cache = BgRowCache{};
    scroll_latch_ = 0;
    bghofs_latch_ = 0;
    inidisp_ = 0x80;
    objsel_ = 0;
    bgmode_ = 0;
    mosaic_ = 0;
    mosaic_vcounter_ = 0;
    bg12nba_ = 0;
    bg34nba_ = 0;
    m7sel_ = 0;
    m7a_ = m7b_ = m7c_ = m7d_ = m7x_ = m7y_ = 0;
    m7_hofs_ = m7_vofs_ = 0;
    m7_latch_ = 0;
    w12sel_ = w34sel_ = wobjsel_ = 0;
    wh0_ = wh1_ = wh2_ = wh3_ = 0;
    wbglog_ = wobjlog_ = 0;
    tm_ = ts_ = tmw_ = tsw_ = 0;
    cgwsel_ = cgadsub_ = 0;
    fixed_color_ = 0;
    setini_ = 0;
    pal_region_ = false;
    ppu1_open_bus_ = 0;
    ppu2_open_bus_ = 0;
    inidisp_early_pending_ = false;
    inidisp_early_value_ = 0;
    setini_early_pending_ = false;
    setini_early_value_ = 0;
    vmain_ = 0;
    vmadd_ = 0;
    vram_read_latch_ = 0;
    oam_word_address_ = 0;
    oam_reload_address_ = 0;
    oam_high_next_ = false;
    oam_write_latch_ = 0;
    obj_priority_rotation_ = false;
    obj_range_over_ = false;
    obj_time_over_ = false;
    obj_eval_target_line_ = -1;
    obj_eval_generation_ = 1;
    obj_eval_cache_generation_ = 0;
    obj_eval_cache_line_ = -1;
    obj_eval_cache_count_ = 0;
    obj_eval_sliver_mask_.fill(0);
    obj_eval_fetched_.fill({});
    obj_ready_line_ = -1;
    obj_ready_count_ = 0;
    obj_ready_sliver_mask_.fill(0);
    obj_ready_fetched_.fill({});
    obj_eval_active_ = false;
    obj_eval_scan_index_ = 0;
    obj_eval_oam_index_ = 0;
    obj_eval_selected_count_ = 0;
    obj_sliver_eval_active_ = false;
    obj_sliver_eval_index_ = -1;
    obj_sliver_eval_col_ = 0;
    obj_slivers_fetched_ = 0;
    cgadd_ = 0;
    cgram_high_next_ = false;
    cgram_write_latch_ = 0;
    cgram_read_high_next_ = false;
    pipeline_cgram_line_ = -1;
    pipeline_main_cgram_.fill(0);
    pipeline_sub_cgram_.fill(0);
    hires_dac_line_ = -1;
    hires_prev_main_color_ = 0;
    hires_prev_main_color_enable_ = false;
    hires_prev_math_enable_ = false;
    hires_prev_blend_mode_ = false;
    hires_prev_color_halve_ = false;
    line_obj_line_ = -1;
    for (auto& p : line_obj_main_) p = Pixel{};
    for (auto& p : line_obj_sub_) p = Pixel{};
    scanline_ = 0;
    dot_ = 0;
    dot_start_master_clock_ = bus_ != nullptr ? bus_->masterClock() : 0;
    frame_counter_ = 0;
    stat78_field_ = false;
    frame_hires_output_ = false;
    frame_overscan_output_ = false;
    present_valid_ = false;
    frame_present_ = false;
    fast_line_ = -1;
    jit_line_ = -1;
    latched_hcounter_ = 0;
    latched_vcounter_ = 0;
    counters_latched_ = false;
    hcounter_high_next_ = false;
    vcounter_high_next_ = false;
}
int PPU::dotsThisLine() const {
    const bool longLine = pal_region_ && interlaceEnabled() &&
        ((frame_counter_ & 1u) != 0) && scanline_ == 311;
    return longLine ? LONG_DOTS_PER_LINE : DOTS_PER_LINE;
}
int PPU::masterClocksThisDot() const {
    const bool shortLine = !pal_region_ && !interlaceEnabled() && ((frame_counter_ & 1u) != 0) && scanline_ == 240;
    if (!shortLine && (dot_ == 323 || dot_ == 327)) {
        return 6;
    }
    return 4;
}
int PPU::masterClocksForLine(int line, bool oddField) const {
    const bool shortLine = !pal_region_ && !interlaceEnabled() && oddField && line == 240;
    if (shortLine) return 1360;
    const bool longLine = pal_region_ && interlaceEnabled() && oddField && line == 311;
    return longLine ? 1368 : 1364;
}
int PPU::masterClockPhaseInDot() const {
    if (bus_ == nullptr) return 0;
    const uint64_t now = bus_->masterClock();
    if (now <= dot_start_master_clock_) return 0;
    const uint64_t elapsed = now - dot_start_master_clock_;
    const uint64_t clocks = static_cast<uint64_t>(masterClocksThisDot());
    return static_cast<int>(std::min<uint64_t>(elapsed, clocks - 1));
}
bool PPU::cpuCanAccessVram() const {
    return forcedBlank() || vblank();
}
bool PPU::cpuCanAccessCgram() const {
    return forcedBlank() || vblank() || hblank();
}
bool PPU::cpuCanAccessOam() const {
    return forcedBlank() || vblank();
}
void PPU::mosaicScanline() {
    const int size = ((mosaic_ >> 4) & 0x0F) + 1;
    const bool enabled = (mosaic_ & 0x0F) != 0;
    if (scanline_ == 1) mosaic_vcounter_ = enabled ? size + 1 : 0;
    if (mosaic_vcounter_ != 0 && --mosaic_vcounter_ == 0) mosaic_vcounter_ = enabled ? size : 0;
}
void PPU::tick() {
    if (frame_present_ && dot_ == 0 && scanline_ == vblankStartLine()) composePresentation();
    if (dot_ == 0) mosaicScanline();
    if (scanline_ == vblankStartLine() && dot_ == OAM_RESET_DOT && !forcedBlank()) {
        oam_word_address_ = oam_reload_address_;
        oam_high_next_ = false;
    }
    if (scanline_ >= 1 && scanline_ <= nativeHeight()) {
        const int screenX = screenXFromDot(dot_);
        if (screenX >= 0)
            renderPixel(screenX, scanline_);
    }
    const int visibleHeight = nativeHeight();
    const int vblankStart = visibleHeight + 1;
    if (scanline_ < vblankStart && (inidisp_ & 0x80) == 0) {
        const int h = dot_;
        if (h < OBJ_RANGE_END_DOT) {
            if (h == 0) {
                const int target = scanline_ + 1;
                if (target <= visibleHeight) beginObjectEvaluation(target);
            } else if ((h & 1) != 0 && obj_eval_active_) {
                stepObjectRangeEvaluation();
            }
        } else if (h == OBJ_RANGE_END_DOT) {
            if (obj_eval_active_) {
                obj_eval_active_ = false;
                obj_eval_cache_count_ = obj_eval_selected_count_;
                obj_eval_sliver_mask_.fill(0);
                obj_eval_fetched_.fill({});
                obj_sliver_eval_active_ = obj_eval_selected_count_ > 0;
                obj_sliver_eval_index_ = obj_eval_selected_count_ - 1;
                obj_sliver_eval_col_ = 0;
                obj_slivers_fetched_ = 0;
                if (!obj_sliver_eval_active_) publishObjectEvaluation();
            }
        } else if (h >= OBJ_FETCH_START_DOT && (h & 1) != 0 && obj_sliver_eval_active_) {
            stepObjectSliverEvaluation();
        }
    }
    ++dot_;
    if (dot_ >= DOTS_PER_LINE) {
        const bool palLongLine = dot_ == DOTS_PER_LINE && pal_region_ && interlaceEnabled() &&
            ((frame_counter_ & 1u) != 0) && scanline_ == 311;
        if (!palLongLine) {
            dot_ = 0;
            ++scanline_;
            if (scanline_ >= linesThisFrame()) {
                scanline_ = 0;
                frame_overscan_output_ = (setini_ & 0x04) != 0;
                ++frame_counter_;
                obj_range_over_ = false;
                obj_time_over_ = false;
                obj_eval_target_line_ = -1;
                obj_eval_active_ = false;
                obj_sliver_eval_active_ = false;
                obj_ready_line_ = -1;
                obj_ready_count_ = 0;
                line_obj_line_ = -1;
                pipeline_cgram_line_ = -1;
            }
        }
    }
    if (scanline_ == 0 && dot_ == 1) {
        stat78_field_ = (frame_counter_ & 1u) != 0;
    }
    dot_start_master_clock_ = bus_ != nullptr ? bus_->masterClock() : dot_start_master_clock_;
}
uint16_t PPU::vramIncrement() const {
    switch (vmain_ & 0x03) {
    case 0: return 1;
    case 1: return 32;
    default: return 128;
    }
}
uint16_t PPU::remapVramAddress(uint16_t address) const {
    switch ((vmain_ >> 2) & 0x03) {
    case 0: return address;
    case 1: return static_cast<uint16_t>((address & 0xFF00) | ((address & 0x001F) << 3) | ((address >> 5) & 0x07));
    case 2: return static_cast<uint16_t>((address & 0xFE00) | ((address & 0x003F) << 3) | ((address >> 6) & 0x07));
    case 3: return static_cast<uint16_t>((address & 0xFC00) | ((address & 0x007F) << 3) | ((address >> 7) & 0x07));
    default: return address;
    }
}
uint16_t PPU::readVramWord(uint16_t wordAddress) const {
    const uint16_t mapped = remapVramAddress(wordAddress);
    const uint32_t byteAddress = static_cast<uint32_t>(mapped) << 1;
    return static_cast<uint16_t>(vram_[byteAddress & 0xFFFF] |
        (static_cast<uint16_t>(vram_[(byteAddress + 1) & 0xFFFF]) << 8));
}
void PPU::incrementVramAddress(bool highPort) {
    const bool incrementOnHigh = (vmain_ & 0x80) != 0;
    if (highPort == incrementOnHigh) vmadd_ = static_cast<uint16_t>(vmadd_ + vramIncrement());
}
void PPU::latchCounters() {
    latched_hcounter_ = static_cast<uint16_t>(dot_ & 0x01FF);
    latched_vcounter_ = static_cast<uint16_t>(scanline_ & 0x01FF);
    counters_latched_ = true;
}
bool PPU::activeCgramPreferSub() const {
    const int clocks = masterClocksThisDot();
    return masterClockPhaseInDot() >= (clocks / 2);
}
uint8_t PPU::activeCgramAddress() const {
    if (forcedBlank() || vblank() || hblank()) return cgadd_;
    const int screenX = screenXFromDot(dot_);
    if (screenX < 0) return cgadd_;
    const int y = std::clamp(scanline_, 1, nativeHeight());
    const bool preferSub = activeCgramPreferSub();
    if (pipeline_cgram_line_ == y) {
        return preferSub ? pipeline_sub_cgram_[static_cast<size_t>(screenX)]
                         : pipeline_main_cgram_[static_cast<size_t>(screenX)];
    }
    const Pixel px = screenPixel(screenX, y, preferSub);
    return px.transparent ? 0 : px.cgramIndex;
}
uint8_t PPU::readRegister(uint16_t address) {
    auto drivePpu1 = [&](uint8_t value, uint8_t drivenMask = 0xFF) -> uint8_t {
        const uint8_t result = static_cast<uint8_t>((value & drivenMask) | (ppu1_open_bus_ & ~drivenMask));
        ppu1_open_bus_ = result;
        return result;
    };
    auto drivePpu2 = [&](uint8_t value, uint8_t drivenMask = 0xFF) -> uint8_t {
        const uint8_t result = static_cast<uint8_t>((value & drivenMask) | (ppu2_open_bus_ & ~drivenMask));
        ppu2_open_bus_ = result;
        return result;
    };
    switch (address) {
    case 0x2134:
    case 0x2135:
    case 0x2136: {
        const int32_t lhs = static_cast<int16_t>(m7a_);
        const int32_t rhs = static_cast<int8_t>(m7b_ >> 8);
        const uint32_t product = static_cast<uint32_t>(lhs * rhs) & 0x00FFFFFFu;
        const unsigned shift = static_cast<unsigned>(address - 0x2134) * 8u;
        return drivePpu1(static_cast<uint8_t>((product >> shift) & 0xFFu));
    }
    case 0x2137:
        if (bus_ != nullptr && (bus_->ioPortOutput() & 0x80) != 0) latchCounters();
        return bus_ != nullptr ? bus_->openBus() : 0xFF;
    case 0x2138: {
        const uint16_t rawAddress = cpuCanAccessOam() ? internalOamByteAddress() : activeOamByteAddress();
        const uint16_t byteAddress = rawAddress < 0x0200
            ? rawAddress
            : static_cast<uint16_t>(0x0200 | (rawAddress & 0x001F));
        const uint8_t value = oam_[byteAddress];
        incrementInternalOamAddress();
        return drivePpu1(value);
    }
    case 0x2139: {
        const bool accessible = cpuCanAccessVram();
        const uint8_t value = accessible ? static_cast<uint8_t>(vram_read_latch_ & 0xFF) : 0x00;
        if (accessible && (vmain_ & 0x80) == 0) vram_read_latch_ = readVramWord(vmadd_);
        incrementVramAddress(false);
        return drivePpu1(value);
    }
    case 0x213A: {
        const bool accessible = cpuCanAccessVram();
        const uint8_t value = accessible ? static_cast<uint8_t>(vram_read_latch_ >> 8) : 0x00;
        if (accessible && (vmain_ & 0x80) != 0) vram_read_latch_ = readVramWord(vmadd_);
        incrementVramAddress(true);
        return drivePpu1(value);
    }
    case 0x213B: {
        const uint8_t readAddress = cpuCanAccessCgram() ? cgadd_ : activeCgramAddress();
        const uint16_t color = cgram_[readAddress] & 0x7FFF;
        const bool high = cgram_read_high_next_;
        const uint8_t value = high ? static_cast<uint8_t>((color >> 8) & 0x7F) : static_cast<uint8_t>(color & 0xFF);
        if (high) ++cgadd_;
        cgram_read_high_next_ = !cgram_read_high_next_;
        return drivePpu2(value, high ? 0x7F : 0xFF);
    }
    case 0x213C: {
        const bool high = hcounter_high_next_;
        const uint8_t value = high ? static_cast<uint8_t>((latched_hcounter_ >> 8) & 0x01) : static_cast<uint8_t>(latched_hcounter_ & 0xFF);
        hcounter_high_next_ = !hcounter_high_next_;
        return drivePpu2(value, high ? 0x01 : 0xFF);
    }
    case 0x213D: {
        const bool high = vcounter_high_next_;
        const uint8_t value = high ? static_cast<uint8_t>((latched_vcounter_ >> 8) & 0x01) : static_cast<uint8_t>(latched_vcounter_ & 0xFF);
        vcounter_high_next_ = !vcounter_high_next_;
        return drivePpu2(value, high ? 0x01 : 0xFF);
    }
    case 0x213E: {
        const uint8_t value = static_cast<uint8_t>(ppu1Version() |
            (obj_range_over_ ? 0x40 : 0x00) | (obj_time_over_ ? 0x80 : 0x00));
        return drivePpu1(value, 0xEF);
    }
    case 0x213F: {
        const uint8_t field = stat78_field_ ? 0x80u : 0x00u;
        const uint8_t latch = counters_latched_ ? 0x40 : 0x00;
        const uint8_t region = pal_region_ ? 0x10 : 0x00;
        const uint8_t value = static_cast<uint8_t>(field | latch | region | ppu2Version());
        const uint8_t result = drivePpu2(value, 0xDF);
        counters_latched_ = false;
        hcounter_high_next_ = false;
        vcounter_high_next_ = false;
        return result;
    }
    default:
        return bus_ != nullptr ? bus_->openBus() : 0xFF;
    }
}
void PPU::writeMode7Word(uint16_t& target, uint8_t value) {
    target = static_cast<uint16_t>(m7_latch_ | (static_cast<uint16_t>(value) << 8));
    m7_latch_ = value;
}
void PPU::writeRegister(uint16_t address, uint8_t value, int previousCpuBus) {
    if (address < 0x2100 || address > 0x213F) return;
    registers_[address - 0x2100] = value;
    ++reg_generation_;
    switch (address) {
    case 0x2100: {
        const uint8_t oldInidisp = inidisp_;
        const bool wasForcedBlank = (inidisp_ & 0x80) != 0;
        const bool activePicture = !vblank() && screenXFromDot(dot_) >= 0;
        if (previousCpuBus >= 0 && activePicture) {
            inidisp_early_pending_ = true;
            inidisp_early_value_ = static_cast<uint8_t>(previousCpuBus);
        }
        inidisp_ = value;
        if (wasForcedBlank && (inidisp_ & 0x80) == 0) {
            oam_word_address_ = oam_reload_address_;
            oam_high_next_ = false;
        }
        break;
    }
    case 0x2101: objsel_ = value; invalidateObjectEvaluation(); break;
    case 0x2102:
        oam_reload_address_ = static_cast<uint16_t>((oam_reload_address_ & 0x100) | value);
        oam_word_address_ = oam_reload_address_;
        oam_high_next_ = false;
        invalidateObjectEvaluation();
        break;
    case 0x2103:
        oam_reload_address_ = static_cast<uint16_t>((oam_reload_address_ & 0x0FF) | ((value & 0x01) << 8));
        obj_priority_rotation_ = (value & 0x80) != 0;
        oam_word_address_ = oam_reload_address_;
        oam_high_next_ = false;
        invalidateObjectEvaluation();
        break;
    case 0x2104: {
        uint16_t oamAddress = cpuCanAccessOam() ? internalOamByteAddress() : activeOamByteAddress();
        if (oamAddress < 0x0200) {
            if ((oamAddress & 1u) == 0) {
                oam_write_latch_ = value;
            } else {
                oam_[oamAddress - 1] = oam_write_latch_;
                oam_[oamAddress] = value;
            }
            if (!cpuCanAccessOam())
                oamAddress = static_cast<uint16_t>(0x0200 | ((oamAddress & 0x01F0) >> 4));
        }
        if (oamAddress >= 0x0200) {
            const uint16_t byteAddress = static_cast<uint16_t>(0x0200 | (oamAddress & 0x001F));
            if ((oamAddress & 1u) == 0) oam_write_latch_ = value;
            oam_[byteAddress] = value;
        }
        incrementInternalOamAddress();
        invalidateObjectEvaluation();
        break;
    }
    case 0x2105: bgmode_ = value; invalidateMode1Rows(); break;
    case 0x2106: {
        const bool mosaicWasEnabled = (mosaic_ & 0x0F) != 0;
        mosaic_ = value;
        if (!mosaicWasEnabled && (value & 0x0F) != 0) mosaic_vcounter_ = ((value >> 4) & 0x0F) + 2;
        invalidateMode1Rows();
        break;
    }
    case 0x2107: bgsc_[0] = value; invalidateMode1Rows(); break;
    case 0x2108: bgsc_[1] = value; invalidateMode1Rows(); break;
    case 0x2109: bgsc_[2] = value; invalidateMode1Rows(); break;
    case 0x210A: bgsc_[3] = value; invalidateMode1Rows(); break;
    case 0x210B: bg12nba_ = value; invalidateMode1Rows(); break;
    case 0x210C: bg34nba_ = value; invalidateMode1Rows(); break;
    case 0x210D: case 0x210E: case 0x210F: case 0x2110:
    case 0x2111: case 0x2112: case 0x2113: case 0x2114: {
        const int index = address - 0x210D;
        const int bg = index / 2;
        const bool vertical = (index & 1) != 0;
        uint16_t& target = vertical ? bg_vofs_[bg] : bg_hofs_[bg];
        if (vertical) {
            target = static_cast<uint16_t>((static_cast<uint16_t>(value) << 8) | scroll_latch_);
        } else {
            target = static_cast<uint16_t>((static_cast<uint16_t>(value) << 8)
                | (scroll_latch_ & 0xF8) | (bghofs_latch_ & 0x07));
            bghofs_latch_ = value;
        }
        scroll_latch_ = value;
        if (address == 0x210D || address == 0x210E) {
            uint16_t& m7target = (address == 0x210D) ? m7_hofs_ : m7_vofs_;
            m7target = static_cast<uint16_t>((static_cast<uint16_t>(value) << 8) | m7_latch_);
            m7_latch_ = value;
        }
        invalidateMode1Rows();
        break;
    }
    case 0x2115: vmain_ = value; break;
    case 0x2116:
        vmadd_ = static_cast<uint16_t>((vmadd_ & 0xFF00) | value);
        if (cpuCanAccessVram()) vram_read_latch_ = readVramWord(vmadd_);
        break;
    case 0x2117:
        vmadd_ = static_cast<uint16_t>((vmadd_ & 0x00FF) | (static_cast<uint16_t>(value) << 8));
        if (cpuCanAccessVram()) vram_read_latch_ = readVramWord(vmadd_);
        break;
    case 0x2118: {
        if (cpuCanAccessVram()) {
            const uint16_t mapped = remapVramAddress(vmadd_);
            const uint16_t byteAddress = static_cast<uint16_t>((static_cast<uint32_t>(mapped) << 1) & 0xFFFF);
            vram_[byteAddress] = value;
            invalidateMode1Rows();
        }
        incrementVramAddress(false);
        break;
    }
    case 0x2119: {
        if (cpuCanAccessVram()) {
            const uint16_t mapped = remapVramAddress(vmadd_);
            const uint16_t byteAddress = static_cast<uint16_t>(((static_cast<uint32_t>(mapped) << 1) + 1) & 0xFFFF);
            vram_[byteAddress] = value;
            invalidateMode1Rows();
        }
        incrementVramAddress(true);
        break;
    }
    case 0x211A: m7sel_ = value; break;
    case 0x211B: writeMode7Word(m7a_, value); break;
    case 0x211C: writeMode7Word(m7b_, value); break;
    case 0x211D: writeMode7Word(m7c_, value); break;
    case 0x211E: writeMode7Word(m7d_, value); break;
    case 0x211F: writeMode7Word(m7x_, value); break;
    case 0x2120: writeMode7Word(m7y_, value); break;
    case 0x2121: cgadd_ = value; cgram_high_next_ = false; cgram_read_high_next_ = false; break;
    case 0x2122: {
        const bool useExternalAddress = cpuCanAccessCgram();
        const uint8_t writeAddress = useExternalAddress ? cgadd_ : activeCgramAddress();
        if (!cgram_high_next_) cgram_write_latch_ = value;
        else {
            cgram_[writeAddress] = static_cast<uint16_t>(cgram_write_latch_ | ((value & 0x7F) << 8));
            const uint16_t cachedColor = cgram_[writeAddress];
            for (uint8_t b = 0; b < 16; ++b)
                cgram_rgba_cache_[b][writeAddress] = rgbaFromBgr555(cachedColor, b);
            ++cgadd_;
        }
        cgram_high_next_ = !cgram_high_next_;
        break;
    }
    case 0x2123: w12sel_ = value; break;
    case 0x2124: w34sel_ = value; break;
    case 0x2125: wobjsel_ = value; break;
    case 0x2126: wh0_ = value; break;
    case 0x2127: wh1_ = value; break;
    case 0x2128: wh2_ = value; break;
    case 0x2129: wh3_ = value; break;
    case 0x212A: wbglog_ = value; break;
    case 0x212B: wobjlog_ = value; break;
    case 0x212C: tm_ = value; break;
    case 0x212D: ts_ = value; break;
    case 0x212E: tmw_ = value; break;
    case 0x212F: tsw_ = value; break;
    case 0x2130: cgwsel_ = value; break;
    case 0x2131: cgadsub_ = value; break;
    case 0x2132: {
        const uint16_t c = value & 0x1F;
        if (value & 0x20) fixed_color_ = static_cast<uint16_t>((fixed_color_ & ~0x001F) | c);
        if (value & 0x40) fixed_color_ = static_cast<uint16_t>((fixed_color_ & ~0x03E0) | (c << 5));
        if (value & 0x80) fixed_color_ = static_cast<uint16_t>((fixed_color_ & ~0x7C00) | (c << 10));
        break;
    }
    case 0x2133: {
        const bool activePicture = !vblank() && screenXFromDot(dot_) >= 0;
        if (previousCpuBus >= 0 && activePicture) {
            setini_early_pending_ = true;
            setini_early_value_ = static_cast<uint8_t>(previousCpuBus);
        }
        setini_ = value;
        invalidateObjectEvaluation();
        break;
    }
    default: break;
    }
}
uint8_t PPU::tilePixel(uint32_t tileBase, uint16_t tileNumber, int bpp, int x, int y) const {
    if (bpp != 2 && bpp != 4 && bpp != 8) return 0;
    const uint32_t base = (tileBase + static_cast<uint32_t>(tileNumber) * static_cast<uint32_t>(bpp * 8)) & 0xFFFF;
    const unsigned bit = static_cast<unsigned>(7 - (x & 7));
    const uint32_t row = static_cast<uint32_t>(y & 7) << 1;
    const uint32_t a0 = (base + row) & 0xFFFF;
    uint8_t pixel = static_cast<uint8_t>(((vram_[a0] >> bit) & 1u) |
                                         (((vram_[(a0 + 1) & 0xFFFF] >> bit) & 1u) << 1));
    if (bpp == 2) return pixel;
    const uint32_t a2 = (a0 + 16) & 0xFFFF;
    pixel |= static_cast<uint8_t>(((vram_[a2] >> bit) & 1u) << 2);
    pixel |= static_cast<uint8_t>(((vram_[(a2 + 1) & 0xFFFF] >> bit) & 1u) << 3);
    if (bpp == 4) return pixel;
    const uint32_t a4 = (a0 + 32) & 0xFFFF;
    const uint32_t a6 = (a0 + 48) & 0xFFFF;
    pixel |= static_cast<uint8_t>(((vram_[a4] >> bit) & 1u) << 4);
    pixel |= static_cast<uint8_t>(((vram_[(a4 + 1) & 0xFFFF] >> bit) & 1u) << 5);
    pixel |= static_cast<uint8_t>(((vram_[a6] >> bit) & 1u) << 6);
    pixel |= static_cast<uint8_t>(((vram_[(a6 + 1) & 0xFFFF] >> bit) & 1u) << 7);
    return pixel;
}
void PPU::decodeTileRow(uint32_t tileBase, uint16_t tileNumber, int bpp, int y, std::array<uint8_t, 8>& out) const {
    const uint32_t base = (tileBase + static_cast<uint32_t>(tileNumber) * static_cast<uint32_t>(bpp * 8)) & 0xFFFF;
    const uint32_t row = static_cast<uint32_t>(y & 7) << 1;
    const uint32_t a0 = (base + row) & 0xFFFF;
    const uint8_t p0 = vram_[a0];
    const uint8_t p1 = vram_[(a0 + 1) & 0xFFFF];
    uint8_t p2 = 0, p3 = 0, p4 = 0, p5 = 0, p6 = 0, p7 = 0;
    if (bpp >= 4) {
        const uint32_t a2 = (a0 + 16) & 0xFFFF;
        p2 = vram_[a2]; p3 = vram_[(a2 + 1) & 0xFFFF];
    }
    if (bpp == 8) {
        const uint32_t a4 = (a0 + 32) & 0xFFFF;
        const uint32_t a6 = (a0 + 48) & 0xFFFF;
        p4 = vram_[a4]; p5 = vram_[(a4 + 1) & 0xFFFF];
        p6 = vram_[a6]; p7 = vram_[(a6 + 1) & 0xFFFF];
    }
    uint64_t packed = kBitplaneExpand[p0] | (kBitplaneExpand[p1] << 1);
    if (bpp >= 4)
        packed |= (kBitplaneExpand[p2] << 2) | (kBitplaneExpand[p3] << 3);
    if (bpp == 8)
        packed |= (kBitplaneExpand[p4] << 4) | (kBitplaneExpand[p5] << 5) |
                  (kBitplaneExpand[p6] << 6) | (kBitplaneExpand[p7] << 7);
    if constexpr (std::endian::native == std::endian::little) {
        std::memcpy(out.data(), &packed, sizeof(packed));
    } else {
        for (unsigned x = 0; x < 8; ++x)
            out[x] = static_cast<uint8_t>(packed >> (x * 8u));
    }
}
uint16_t PPU::tilemapEntry(int bg, int tileX, int tileY) const {
    const uint8_t sc = bgsc_[bg];
    const int size = sc & 0x03;
    const int widthTiles = (size & 0x01) ? 64 : 32;
    const int heightTiles = (size & 0x02) ? 64 : 32;
    tileX = static_cast<int>(static_cast<uint32_t>(tileX) & static_cast<uint32_t>(widthTiles - 1));
    tileY = static_cast<int>(static_cast<uint32_t>(tileY) & static_cast<uint32_t>(heightTiles - 1));
    int screen = 0;
    if (tileX >= 32) screen += 1;
    if (tileY >= 32) screen += (size == 3 ? 2 : 1);
    const uint32_t mapBase = static_cast<uint32_t>(sc & 0xFC) << 9;
    const uint32_t offset = static_cast<uint32_t>(screen) * 0x800 + static_cast<uint32_t>((tileY & 31) * 32 + (tileX & 31)) * 2;
    const uint32_t address = (mapBase + offset) & 0xFFFF;
    return static_cast<uint16_t>(vram_[address] | (static_cast<uint16_t>(vram_[(address + 1) & 0xFFFF]) << 8));
}
uint16_t PPU::directColor(uint8_t palette, uint8_t pixel) const {
    const uint16_t r = static_cast<uint16_t>(((pixel & 0x07) << 2) | ((palette & 0x01) << 1));
    const uint16_t g = static_cast<uint16_t>(((pixel & 0x38) >> 1) | (palette & 0x02));
    const uint16_t b = static_cast<uint16_t>(((pixel & 0xC0) >> 3) | (palette & 0x04));
    return static_cast<uint16_t>((b << 10) | (g << 5) | r);
}
uint8_t PPU::backgroundPixelMode1(int bg, int x, int y, uint8_t& priority) const {
    if (bg < 0 || bg > 2) return kTransparent;
    const uint8_t bgBit = static_cast<uint8_t>(1u << bg);
    const bool tile16 = (bgmode_ & (0x10u << bg)) != 0;
    if ((mosaic_ & bgBit) == 0 && !tile16) {
        const int sx = (x + static_cast<int>(bg_hofs_[bg] & 0x3FF)) & 0x3FF;
        const int sy = (y + static_cast<int>(bg_vofs_[bg] & 0x3FF)) & 0x3FF;
        const int tileX = sx >> 3;
        const int tileY = sy >> 3;
        const int rawX = sx & 7;
        const int rawY = sy & 7;
        const uint32_t rowKey = static_cast<uint32_t>(tileX) |
            (static_cast<uint32_t>(tileY) << 7) |
            (static_cast<uint32_t>(rawY) << 14);
        Mode1RowCache& cache = mode1_row_cache_[static_cast<size_t>(bg)];
        if (cache.generation != mode1_row_generation_ || cache.rowKey != rowKey) {
            const uint16_t entry = tilemapEntry(bg, tileX, tileY);
            const bool hflip = (entry & 0x4000) != 0;
            const bool vflip = (entry & 0x8000) != 0;
            const int sourceY = vflip ? 7 - rawY : rawY;
            const uint16_t tile = static_cast<uint16_t>(entry & 0x03FF);
            uint32_t tileBase;
            if (bg == 0) tileBase = static_cast<uint32_t>(bg12nba_ & 0x0F) * 0x2000u;
            else if (bg == 1) tileBase = static_cast<uint32_t>((bg12nba_ >> 4) & 0x0F) * 0x2000u;
            else tileBase = static_cast<uint32_t>(bg34nba_ & 0x0F) * 0x2000u;
            const int bpp = bg < 2 ? 4 : 2;
            std::array<uint8_t, 8> decoded{};
            decodeTileRow(tileBase, tile, bpp, sourceY, decoded);
            const uint8_t paletteBase = static_cast<uint8_t>(((entry >> 10) & 7) * (bg == 2 ? 4 : 16));
            for (int localX = 0; localX < 8; ++localX) {
                const uint8_t raw = decoded[static_cast<size_t>(hflip ? 7 - localX : localX)];
                cache.pixels[static_cast<size_t>(localX)] = raw == 0 ? 0 : static_cast<uint8_t>(paletteBase + raw);
            }
            cache.generation = mode1_row_generation_;
            cache.rowKey = rowKey;
            cache.palette = static_cast<uint8_t>((entry >> 10) & 7);
            const bool high = (entry & 0x2000) != 0;
            cache.priority = static_cast<uint8_t>(priorityFor(1, bg, high, (bgmode_ & 0x08) != 0));
        }
        const uint8_t index = cache.pixels[static_cast<size_t>(rawX)];
        if (index == 0) return kTransparent;
        priority = cache.priority;
        return index;
    }
    int sampleX = x;
    int sampleY = y;
    if (mosaic_ & bgBit) {
        const int size = ((mosaic_ >> 4) & 0x0F) + 1;
        sampleX -= sampleX % size;
        sampleY = y - mosaicVOffset();
    }
    const int tileShift = tile16 ? 4 : 3;
    const int tileSize = 1 << tileShift;
    const int tileMask = tileSize - 1;
    const int sx = (sampleX + static_cast<int>(bg_hofs_[bg] & 0x3FF)) & 0x3FF;
    const int sy = (sampleY + static_cast<int>(bg_vofs_[bg] & 0x3FF)) & 0x3FF;
    const int tileX = sx >> tileShift;
    const int tileY = sy >> tileShift;
    const int rawX = sx & tileMask;
    const int rawY = sy & tileMask;
    Mode1RowCache& cache = mode1_row_cache_[static_cast<size_t>(bg)];
    const uint32_t rowKey = static_cast<uint32_t>(tileX) |
        (static_cast<uint32_t>(tileY) << 7) |
        (static_cast<uint32_t>(rawY) << 14) |
        (tile16 ? (1u << 18) : 0u);
    if (cache.generation != mode1_row_generation_ || cache.rowKey != rowKey) {
        const uint16_t entry = tilemapEntry(bg, tileX, tileY);
        const bool hflip = (entry & 0x4000) != 0;
        const bool vflip = (entry & 0x8000) != 0;
        const int sourceY = vflip ? tileSize - 1 - rawY : rawY;
        const uint16_t baseTile = static_cast<uint16_t>(entry & 0x03FF);
        uint32_t tileBase;
        if (bg == 0) tileBase = static_cast<uint32_t>(bg12nba_ & 0x0F) * 0x2000u;
        else if (bg == 1) tileBase = static_cast<uint32_t>((bg12nba_ >> 4) & 0x0F) * 0x2000u;
        else tileBase = static_cast<uint32_t>(bg34nba_ & 0x0F) * 0x2000u;
        const int bpp = bg < 2 ? 4 : 2;
        std::array<uint8_t, 8> left{};
        std::array<uint8_t, 8> right{};
        const uint16_t verticalTile = static_cast<uint16_t>(baseTile +
            (tile16 ? (((sourceY >> 3) & 1) << 4) : 0));
        decodeTileRow(tileBase, verticalTile, bpp, sourceY & 7, left);
        if (tile16)
            decodeTileRow(tileBase, static_cast<uint16_t>(verticalTile + 1), bpp, sourceY & 7, right);
        const uint8_t paletteBase = static_cast<uint8_t>(((entry >> 10) & 7) * (bg == 2 ? 4 : 16));
        for (int localX = 0; localX < tileSize; ++localX) {
            const int sourceX = hflip ? tileSize - 1 - localX : localX;
            const uint8_t raw = sourceX < 8 ? left[static_cast<size_t>(sourceX)]
                                            : right[static_cast<size_t>(sourceX - 8)];
            cache.pixels[static_cast<size_t>(localX)] = raw == 0 ? 0 : static_cast<uint8_t>(paletteBase + raw);
        }
        cache.generation = mode1_row_generation_;
        cache.rowKey = rowKey;
        cache.palette = static_cast<uint8_t>((entry >> 10) & 7);
        const bool high = (entry & 0x2000) != 0;
        cache.priority = static_cast<uint8_t>(priorityFor(1, bg, high, (bgmode_ & 0x08) != 0));
    }
    const uint8_t index = cache.pixels[static_cast<size_t>(rawX)];
    if (index == 0) return kTransparent;
    priority = cache.priority;
    return index;
}
uint8_t PPU::backgroundPixel(int bg, int x, int y, uint8_t& priority, uint8_t& palette, bool subScreen) const {
    const uint8_t mode = bgmode_ & 7;
    if (mode == 1) return backgroundPixelMode1(bg, x, y, priority);
    const int bpp = backgroundBpp(mode, bg);
    if (bpp == 0) return kTransparent;
    int sampleX = x;
    int sampleY = y;
    if (mosaic_ & (1u << bg)) {
        const int size = ((mosaic_ >> 4) & 0x0F) + 1;
        sampleX -= sampleX % size;
        sampleY = y - mosaicVOffset();
    }
    const bool tile16 = (bgmode_ & (0x10u << bg)) != 0;
    const bool mode56 = (mode == 5 || mode == 6);
    const int tileShiftX = (mode56 || tile16) ? 4 : 3;
    const int tileShiftY = tile16 ? 4 : 3;
    const int tileSizeX = 1 << tileShiftX;
    const int tileSizeY = 1 << tileShiftY;
    const int tileMaskX = tileSizeX - 1;
    const int tileMaskY = tileSizeY - 1;
    int hofs = static_cast<int>(bg_hofs_[bg] & 0x3FF);
    int vofs = static_cast<int>(bg_vofs_[bg] & 0x3FF);
    int sx;
    if (mode56) {
        sx = (sampleX * 2 + (subScreen ? 0 : 1) + (hofs << 1)) & 0x3FF;
    } else {
        sx = (sampleX + hofs) & 0x3FF;
    }
    int sy = (sampleY + vofs) & 0x3FF;
    const bool optMode = (mode == 2 || mode == 4 || mode == 6);
    if (optMode && (bg == 0 || bg == 1)) {
        const uint16_t validBit = static_cast<uint16_t>(0x2000u << bg);
        const int bg3H = static_cast<int>(bg_hofs_[2] & 0x3FF);
        const int bg3V = static_cast<int>(bg_vofs_[2] & 0x3FF);
        const bool bg3Tile16 = (bgmode_ & 0x40u) != 0;
        const int bg3TileShiftY = bg3Tile16 ? 4 : 3;
        if (!mode56) {
            const int fineH = hofs & 0x07;
            const int optColumnX = sampleX + fineH;
            if (optColumnX >= 8) {
                const int lookupX = (optColumnX - 8) + (bg3H & ~0x07);
                const int lookupTileX = lookupX >> bg3TileShiftY;
                if (opt_cache_.generation != mode1_row_generation_ || opt_cache_.tileX != lookupTileX ||
                    (mode != 4 && !opt_cache_.hasV)) {
                    opt_cache_.generation = mode1_row_generation_;
                    opt_cache_.tileX = lookupTileX;
                    opt_cache_.h = tilemapEntry(2, lookupTileX, bg3V >> bg3TileShiftY);
                    opt_cache_.hasV = mode != 4;
                    if (opt_cache_.hasV) opt_cache_.v = tilemapEntry(2, lookupTileX, (bg3V + 8) >> bg3TileShiftY);
                }
                const uint16_t hlookup = opt_cache_.h;
                if (mode == 4) {
                    if (hlookup & validBit) {
                        if ((hlookup & 0x8000) == 0) {
                            const int optH = fineH | (hlookup & 0x03F8);
                            sx = (sampleX + optH) & 0x3FF;
                        } else {
                            sy = (sampleY + (hlookup & 0x03FF)) & 0x3FF;
                        }
                    }
                } else {
                    const uint16_t vlookup = opt_cache_.v;
                    if (hlookup & validBit) {
                        const int optH = fineH | (hlookup & 0x03F8);
                        sx = (sampleX + optH) & 0x3FF;
                    }
                    if (vlookup & validBit)
                        sy = (sampleY + (vlookup & 0x03FF)) & 0x3FF;
                }
            }
        } else {
            const int nativeSampleX = sampleX * 2 + (subScreen ? 0 : 1);
            const int hscroll = hofs << 1;
            const int offsetX = nativeSampleX + (hscroll & 0x07);
            if (offsetX >= tileSizeX) {
                const int bg3TileShiftX = 4;
                const int lookupX = offsetX - tileSizeX + ((bg3H & ~0x07) << 1);
                const auto optEntry = [&](int pixelX, int pixelY) -> uint16_t {
                    return tilemapEntry(2, pixelX >> bg3TileShiftX, pixelY >> bg3TileShiftY);
                };
                const uint16_t hlookup = optEntry(lookupX, bg3V);
                const uint16_t vlookup = optEntry(lookupX, bg3V + 8);
                if (hlookup & validBit)
                    sx = (offsetX + (hlookup & 0x03F8)) & 0x3FF;
                if (vlookup & validBit)
                    sy = (sampleY + (vlookup & 0x03FF)) & 0x3FF;
            }
        }
    }
    const int tileX = sx >> tileShiftX;
    const int tileY = sy >> tileShiftY;
    const int rawX = sx & tileMaskX;
    const int rawY = sy & tileMaskY;
    BgRowCache& cache = bg_row_cache_[static_cast<size_t>(bg)];
    if (cache.generation != mode1_row_generation_ || cache.tileX != tileX ||
        cache.tileY != tileY || cache.localY != rawY ||
        cache.tileSizeX != tileSizeX || cache.tileSizeY != tileSizeY) {
        const uint16_t entry = tilemapEntry(bg, tileX, tileY);
        const bool hflip = (entry & 0x4000) != 0;
        const bool vflip = (entry & 0x8000) != 0;
        const int sourceY = vflip ? tileSizeY - 1 - rawY : rawY;
        const uint16_t baseTile = static_cast<uint16_t>(entry & 0x03FF);
        uint32_t tileBase = 0;
        if (bg == 0) tileBase = static_cast<uint32_t>(bg12nba_ & 0x0F) * 0x2000u;
        else if (bg == 1) tileBase = static_cast<uint32_t>((bg12nba_ >> 4) & 0x0F) * 0x2000u;
        else if (bg == 2) tileBase = static_cast<uint32_t>(bg34nba_ & 0x0F) * 0x2000u;
        else tileBase = static_cast<uint32_t>((bg34nba_ >> 4) & 0x0F) * 0x2000u;
        const uint16_t verticalTile = static_cast<uint16_t>(baseTile +
            (tileSizeY == 16 ? (((sourceY >> 3) & 1) << 4) : 0));
        std::array<uint8_t, 8> left{};
        std::array<uint8_t, 8> right{};
        decodeTileRow(tileBase, verticalTile, bpp, sourceY & 7, left);
        if (tileSizeX == 16)
            decodeTileRow(tileBase, static_cast<uint16_t>(verticalTile + 1), bpp, sourceY & 7, right);
        for (int localX = 0; localX < tileSizeX; ++localX) {
            const int sourceX = hflip ? tileSizeX - 1 - localX : localX;
            cache.pixels[static_cast<size_t>(localX)] = sourceX < 8
                ? left[static_cast<size_t>(sourceX)]
                : right[static_cast<size_t>(sourceX - 8)];
        }
        cache.generation = mode1_row_generation_;
        cache.tileX = tileX;
        cache.tileY = tileY;
        cache.localY = rawY;
        cache.tileSizeX = tileSizeX;
        cache.tileSizeY = tileSizeY;
        cache.palette = static_cast<uint8_t>((entry >> 10) & 7);
        const bool highPriority = (entry & 0x2000) != 0;
        cache.priority = static_cast<uint8_t>(priorityFor(mode, bg, highPriority, (bgmode_ & 0x08) != 0));
    }
    const uint8_t pixel = cache.pixels[static_cast<size_t>(rawX)];
    if (pixel == 0) return kTransparent;
    palette = cache.palette;
    priority = cache.priority;
    if (mode == 0) return static_cast<uint8_t>(bg * 32 + palette * 4 + pixel);
    if (bpp == 2) return static_cast<uint8_t>(palette * 4 + pixel);
    if (bpp == 4) return static_cast<uint8_t>(palette * 16 + pixel);
    return pixel;
}
bool PPU::windowResultFromGeometry(int layer, bool win1, bool win2) const {
    uint8_t sel = 0;
    int logic = 0;
    if (layer < 2) { sel = static_cast<uint8_t>((w12sel_ >> (layer * 4)) & 0x0F); logic = (wbglog_ >> (layer * 2)) & 3; }
    else if (layer < 4) { sel = static_cast<uint8_t>((w34sel_ >> ((layer - 2) * 4)) & 0x0F); logic = (wbglog_ >> (layer * 2)) & 3; }
    else { sel = static_cast<uint8_t>(wobjsel_ & 0x0F); logic = wobjlog_ & 3; }
    const bool e1 = (sel & 0x02) != 0;
    const bool e2 = (sel & 0x08) != 0;
    bool a = win1;
    bool b = win2;
    if (sel & 0x01) a = !a;
    if (sel & 0x04) b = !b;
    if (!e1 && !e2) return false;
    if (e1 && !e2) return a;
    if (!e1 && e2) return b;
    switch (logic) { case 0: return a || b; case 1: return a && b; case 2: return a != b; default: return a == b; }
}
bool PPU::colorWindowResultFromGeometry(bool win1, bool win2) const {
    const uint8_t sel = static_cast<uint8_t>((wobjsel_ >> 4) & 0x0F);
    const bool e1 = (sel & 0x02) != 0;
    const bool e2 = (sel & 0x08) != 0;
    if (!e1 && !e2) return false;
    const int logic = (wobjlog_ >> 2) & 3;
    bool a = win1;
    bool b = win2;
    if (sel & 0x01) a = !a;
    if (sel & 0x04) b = !b;
    if (e1 && !e2) return a;
    if (!e1 && e2) return b;
    switch (logic) { case 0: return a || b; case 1: return a && b; case 2: return a != b; default: return a == b; }
}
bool PPU::windowResult(int layer, int x) const {
    const bool win1 = (wh0_ <= wh1_) && (x >= static_cast<int>(wh0_)) && (x <= static_cast<int>(wh1_));
    const bool win2 = (wh2_ <= wh3_) && (x >= static_cast<int>(wh2_)) && (x <= static_cast<int>(wh3_));
    return windowResultFromGeometry(layer, win1, win2);
}
bool PPU::colorWindowResult(int x) const {
    const bool win1 = (wh0_ <= wh1_) && (x >= static_cast<int>(wh0_)) && (x <= static_cast<int>(wh1_));
    const bool win2 = (wh2_ <= wh3_) && (x >= static_cast<int>(wh2_)) && (x <= static_cast<int>(wh3_));
    return colorWindowResultFromGeometry(win1, win2);
}
bool PPU::layerWindowMasked(int layer, int x, bool subScreen) const {
    const uint8_t mask = subScreen ? tsw_ : tmw_;
    return (mask & (1u << layer)) != 0 && windowResult(layer, x);
}
uint16_t PPU::internalOamByteAddress() const {
    return static_cast<uint16_t>((((oam_word_address_ & 0x1FF) << 1) |
        (oam_high_next_ ? 1u : 0u)) & 0x03FF);
}
void PPU::incrementInternalOamAddress() {
    const uint16_t next = static_cast<uint16_t>((internalOamByteAddress() + 1) & 0x03FF);
    oam_word_address_ = static_cast<uint16_t>((next >> 1) & 0x01FF);
    oam_high_next_ = (next & 1u) != 0;
}
uint16_t PPU::activeOamByteAddress() const {
    if (cpuCanAccessOam()) return internalOamByteAddress();
    if (dot_ < OBJ_RANGE_END_DOT && obj_eval_active_)
        return static_cast<uint16_t>((obj_eval_oam_index_ & 0x7F) << 2);
    if (obj_sliver_eval_active_ && obj_sliver_eval_index_ >= 0 &&
        obj_sliver_eval_index_ < obj_eval_selected_count_) {
        return static_cast<uint16_t>((obj_eval_cache_[static_cast<size_t>(obj_sliver_eval_index_)].sprite & 0x7F) << 2);
    }
    return static_cast<uint16_t>((obj_eval_oam_index_ & 0x7F) << 2);
}
uint8_t PPU::firstSpriteIndex() const {
    if (!obj_priority_rotation_) return 0;
    return static_cast<uint8_t>((internalOamByteAddress() >> 2) & 0x7F);
}
PPU::ObjectDimensions PPU::objectDimensions(bool large) const {
    static constexpr ObjectDimensions sizes[8][2] = {
        {{8,8},   {16,16}},
        {{8,8},   {32,32}},
        {{8,8},   {64,64}},
        {{16,16}, {32,32}},
        {{16,16}, {64,64}},
        {{32,32}, {64,64}},
        {{16,32}, {32,64}},
        {{16,32}, {32,32}},
    };
    return sizes[(objsel_ >> 5) & 7][large ? 1 : 0];
}
bool PPU::objectOnScanline(int sprite, int line, int& x, int& localY, ObjectDimensions& dims) const {
    const int base = sprite * 4;
    x = oam_[base];
    const int y = oam_[base + 1];
    const uint8_t high = oam_[512 + sprite / 4];
    const int shift = (sprite & 3) * 2;
    if ((high >> shift) & 1) x |= 0x100;
    if (x >= 256) x -= 512;
    const bool large = ((high >> (shift + 1)) & 1) != 0;
    dims = objectDimensions(large);
    const bool objInterlace = (setini_ & 0x02) != 0;
    const int rangeH = objInterlace ? dims.height * 2 : dims.height;
    localY = (line - 1 - y) & 0xFF;
    return localY < rangeH;
}
void PPU::beginObjectEvaluation(int line) {
    obj_eval_target_line_ = line;
    obj_eval_active_ = true;
    obj_eval_scan_index_ = 0;
    obj_eval_oam_index_ = firstSpriteIndex();
    obj_eval_selected_count_ = 0;
    obj_sliver_eval_active_ = false;
    obj_sliver_eval_index_ = -1;
    obj_sliver_eval_col_ = 0;
    obj_slivers_fetched_ = 0;
    obj_eval_sliver_mask_.fill(0);
    obj_eval_fetched_.fill({});
    line_obj_line_ = -1;
}
void PPU::stepObjectRangeEvaluation() {
    if (!obj_eval_active_ || obj_eval_scan_index_ >= 128) return;
    const int sprite = obj_eval_oam_index_;
    obj_eval_oam_index_ = static_cast<uint8_t>((obj_eval_oam_index_ + 1) & 127);
    ++obj_eval_scan_index_;
    int x = 0, localY = 0;
    ObjectDimensions dims{};
    if (!objectOnScanline(sprite, obj_eval_target_line_, x, localY, dims)) return;
    if (!((x == -256) || (x > -dims.width && x < 256))) return;
    if (obj_eval_selected_count_ < 32) {
        obj_eval_cache_[static_cast<size_t>(obj_eval_selected_count_++)] = {sprite, x, localY, dims};
    } else {
        obj_range_over_ = true;
    }
}
void PPU::publishObjectEvaluation() {
    obj_ready_line_ = obj_eval_target_line_;
    obj_ready_count_ = obj_eval_selected_count_;
    obj_ready_cache_ = obj_eval_cache_;
    obj_ready_sliver_mask_ = obj_eval_sliver_mask_;
    obj_ready_fetched_ = obj_eval_fetched_;
    obj_eval_cache_line_ = obj_eval_target_line_;
    obj_eval_cache_count_ = obj_eval_selected_count_;
    obj_eval_cache_generation_ = obj_eval_generation_;
}
void PPU::stepObjectSliverEvaluation() {
    if (!obj_sliver_eval_active_) return;
    while (obj_sliver_eval_index_ >= 0) {
        const size_t objIndex = static_cast<size_t>(obj_sliver_eval_index_);
        const EvaluatedObject& obj = obj_eval_cache_[objIndex];
        const int evalX = (obj.x == -256) ? 0 : obj.x;
        const int cols = (obj.dims.width + 7) / 8;
        if (obj_sliver_eval_col_ >= cols) {
            --obj_sliver_eval_index_;
            obj_sliver_eval_col_ = 0;
            continue;
        }
        const int col = obj_sliver_eval_col_++;
        const int tileXScreen = evalX + col * 8;
        if (tileXScreen <= -8 || tileXScreen >= 256) continue;
        if (obj_slivers_fetched_ >= 34) {
            obj_time_over_ = true;
            obj_sliver_eval_active_ = false;
            publishObjectEvaluation();
            return;
        }
        const int base = obj.sprite * 4;
        const uint8_t tile = oam_[base + 2];
        const uint8_t attr = oam_[base + 3];
        const bool objInterlace = (setini_ & 0x02) != 0;
        const bool oddField = (frame_counter_ & 1u) != 0;
        int localY;
        if (objInterlace) {
            const int rangeH = obj.dims.height * 2;
            const int flipped = (attr & 0x80) ? (rangeH - 1 - obj.localY) : obj.localY;
            if (((flipped & 1) != 0) != oddField) localY = -1;
            else localY = flipped >> 1;
        } else {
            localY = (attr & 0x80) ? (obj.dims.height - 1 - obj.localY) : obj.localY;
        }
        FetchedObjectSliver& fetched = obj_eval_fetched_[objIndex][static_cast<size_t>(col)];
        fetched = {};
        fetched.valid = true;
        fetched.palette = static_cast<uint8_t>((attr >> 1) & 7);
        fetched.priority = static_cast<uint8_t>(4 + (((attr >> 4) & 3) * 3));
        if (localY >= 0) {
            const uint32_t charBase = (static_cast<uint32_t>(objsel_ & 0x07) << 14) & 0xFFFF;
            const uint32_t nameOffset = static_cast<uint32_t>(((objsel_ >> 3) & 0x03) + 1) << 13;
            const uint32_t selectedBase = (charBase + ((attr & 0x01) ? nameOffset : 0)) & 0xFFFF;
            const bool hflip = (attr & 0x40) != 0;
            const int sourceTileX = hflip ? (cols - 1 - col) : col;
            const int sourceTileY = localY >> 3;
            const uint8_t tileNumber = static_cast<uint8_t>(
                (((tile >> 4) + sourceTileY) & 0x0F) << 4 |
                (((tile & 0x0F) + sourceTileX) & 0x0F));
            std::array<uint8_t, 8> rowPixels{};
            decodeTileRow(selectedBase, tileNumber, 4, localY & 7, rowPixels);
            if (hflip) {
                for (int px = 0; px < 8; ++px)
                    fetched.pixels[static_cast<size_t>(px)] = rowPixels[static_cast<size_t>(7 - px)];
            } else {
                fetched.pixels = rowPixels;
            }
        }
        obj_eval_sliver_mask_[objIndex] |= static_cast<uint8_t>(1u << col);
        ++obj_slivers_fetched_;
        if (obj_slivers_fetched_ == 34) {
            int idx = obj_sliver_eval_index_;
            int nextCol = obj_sliver_eval_col_;
            bool more = false;
            while (idx >= 0 && !more) {
                const EvaluatedObject& remain = obj_eval_cache_[static_cast<size_t>(idx)];
                const int rx = (remain.x == -256) ? 0 : remain.x;
                const int rcols = (remain.dims.width + 7) / 8;
                while (nextCol < rcols) {
                    const int xx = rx + nextCol * 8;
                    ++nextCol;
                    if (xx > -8 && xx < 256) { more = true; break; }
                }
                if (!more) { --idx; nextCol = 0; }
            }
            if (more) obj_time_over_ = true;
            obj_sliver_eval_active_ = false;
            publishObjectEvaluation();
        }
        return;
    }
    obj_sliver_eval_active_ = false;
    publishObjectEvaluation();
}
void PPU::evaluateObjectLimits(int line) {
    const uint8_t first = firstSpriteIndex();
    int inRangeCount = 0;
    int selectedCount = 0;
    for (int n = 0; n < 128; ++n) {
        const int sprite = (first + n) & 127;
        int x = 0, localY = 0;
        ObjectDimensions dims{};
        if (!objectOnScanline(sprite, line, x, localY, dims)) continue;
        const bool countsInRange = (x == -256) || (x > -dims.width && x < 256);
        if (!countsInRange) continue;
        ++inRangeCount;
        if (selectedCount < 32)
            obj_eval_cache_[static_cast<size_t>(selectedCount++)] = {sprite, x, localY, dims};
        else
            obj_range_over_ = true;
    }
    obj_eval_sliver_mask_.fill(0);
    int slivers = 0;
    for (int i = selectedCount - 1; i >= 0; --i) {
        const EvaluatedObject& obj = obj_eval_cache_[static_cast<size_t>(i)];
        const int evalX = (obj.x == -256) ? 0 : obj.x;
        const int cols = (obj.dims.width + 7) / 8;
        for (int col = 0; col < cols; ++col) {
            const int tileX = evalX + col * 8;
            if (tileX <= -8 || tileX >= 256) continue;
            if (slivers >= 34) {
                obj_time_over_ = true;
            } else {
                obj_eval_sliver_mask_[static_cast<size_t>(i)] |= static_cast<uint8_t>(1u << col);
                ++slivers;
            }
        }
    }
    obj_eval_cache_line_ = line;
    obj_eval_cache_count_ = selectedCount;
    obj_eval_cache_generation_ = obj_eval_generation_;
}
void PPU::renderObjects(int line, bool subScreen, std::array<Pixel, 256>& pixels) const {
    const uint8_t enable = subScreen ? ts_ : tm_;
    if ((enable & 0x10) == 0) return;
    const uint32_t charBase = (static_cast<uint32_t>(objsel_ & 0x07) << 14) & 0xFFFF;
    const uint32_t nameOffset = static_cast<uint32_t>(((objsel_ >> 3) & 0x03) + 1) << 13;
    const uint8_t first = firstSpriteIndex();
    int selected[32] = {};
    int selectedCount = 0;
    for (int n = 0; n < 128 && selectedCount < 32; ++n) {
        const int sprite = (first + n) & 127;
        int x = 0, localY = 0;
        ObjectDimensions dims{};
        if (!objectOnScanline(sprite, line, x, localY, dims)) continue;
        if (!((x == -256) || (x > -dims.width && x < 256))) continue;
        selected[selectedCount++] = sprite;
    }
    std::array<uint8_t, 32> sliverMask{};
    int slivers = 0;
    for (int i = selectedCount - 1; i >= 0; --i) {
        int x = 0, localY = 0;
        ObjectDimensions dims{};
        (void)objectOnScanline(selected[i], line, x, localY, dims);
        const int evalX = (x == -256) ? 0 : x;
        const int cols = (dims.width + 7) / 8;
        for (int col = 0; col < cols; ++col) {
            const int tileX = evalX + col * 8;
            if (tileX <= -8 || tileX >= 256) continue;
            if (slivers < 34) {
                sliverMask[i] |= static_cast<uint8_t>(1u << col);
                ++slivers;
            }
        }
    }
    std::array<Pixel, 256> objPixels{};
    for (int i = 0; i < selectedCount; ++i) {
        const int sprite = selected[i];
        const int base = sprite * 4;
        int x = 0, localY0 = 0;
        ObjectDimensions dims{};
        (void)objectOnScanline(sprite, line, x, localY0, dims);
        const uint8_t tile = oam_[base + 2];
        const uint8_t attr = oam_[base + 3];
        const bool objInterlace = (setini_ & 0x02) != 0;
        int localY;
        if (objInterlace) {
            const int rangeH = dims.height * 2;
            const int flipped = (attr & 0x80) ? (rangeH - 1 - localY0) : localY0;
            const bool oddField = (frame_counter_ & 1u) != 0;
            if (((flipped & 1) != 0) != oddField) continue;
            localY = flipped >> 1;
        } else {
            localY = (attr & 0x80) ? (dims.height - 1 - localY0) : localY0;
        }
        for (int sx = 0; sx < dims.width; ++sx) {
            const int screenX = x + sx;
            if (screenX < 0 || screenX >= 256 || layerWindowMasked(4, screenX, subScreen)) continue;
            const int screenTileCol = sx >> 3;
            if ((sliverMask[i] & (1u << screenTileCol)) == 0) continue;
            if (!objPixels[screenX].transparent) continue;
            const int localX = (attr & 0x40) ? (dims.width - 1 - sx) : sx;
            const int tileX = localX >> 3;
            const int tileY = localY >> 3;
            const uint16_t tileNumber = static_cast<uint16_t>(tile + tileX + tileY * 16);
            const uint32_t selectedBase = (charBase + ((attr & 0x01) ? nameOffset : 0)) & 0xFFFF;
            const uint8_t p = tilePixel(selectedBase, tileNumber, 4, localX & 7, localY & 7);
            if (p == 0) continue;
            Pixel obj{};
            obj.priority = static_cast<uint8_t>(4 + (((attr >> 4) & 3) * 3));
            obj.cgramIndex = static_cast<uint8_t>(128 + (((attr >> 1) & 7) * 16) + p);
            obj.color = cgram_[obj.cgramIndex];
            obj.layer = 4;
            obj.objPalette = static_cast<uint8_t>((attr >> 1) & 7);
            obj.transparent = false;
            objPixels[screenX] = obj;
        }
    }
    for (int x = 0; x < 256; ++x) {
        if (!objPixels[x].transparent && objPixels[x].priority >= pixels[x].priority)
            pixels[x] = objPixels[x];
    }
}
void PPU::renderObjectPair(int line, std::array<Pixel, 256>& mainPixels,
                           std::array<Pixel, 256>& subPixels) {
    const bool mainEnabled = (tm_ & 0x10) != 0;
    const bool subEnabled = (ts_ & 0x10) != 0;
    if (!mainEnabled && !subEnabled) return;
    const uint32_t charBase = (static_cast<uint32_t>(objsel_ & 0x07) << 14) & 0xFFFF;
    const uint32_t nameOffset = static_cast<uint32_t>(((objsel_ >> 3) & 0x03) + 1) << 13;
    const uint8_t first = firstSpriteIndex();
    const bool objInterlace = (setini_ & 0x02) != 0;
    const bool oddField = (frame_counter_ & 1u) != 0;
    const bool unlimited = sprite_limit_removed_;
    const int maxSelected = unlimited ? 128 : 32;
    const int maxSlivers = unlimited ? 1024 : 34;
    std::array<EvaluatedObject, 128> localSelected{};
    std::array<uint8_t, 128> localSliverMask{};
    const EvaluatedObject* selected = nullptr;
    const uint8_t* sliverMask = nullptr;
    int selectedCount = 0;
    const bool useFetchedPipeline = !unlimited && obj_ready_line_ == line;
    if (useFetchedPipeline) {
        selected = obj_ready_cache_.data();
        sliverMask = obj_ready_sliver_mask_.data();
        selectedCount = obj_ready_count_;
    } else {
        for (int n = 0; n < 128 && selectedCount < maxSelected; ++n) {
            const int sprite = (first + n) & 127;
            int x = 0, localY = 0;
            ObjectDimensions dims{};
            if (!objectOnScanline(sprite, line, x, localY, dims)) continue;
            if (!((x == -256) || (x > -dims.width && x < 256))) continue;
            localSelected[static_cast<size_t>(selectedCount++)] = {sprite, x, localY, dims};
        }
        int slivers = 0;
        for (int i = selectedCount - 1; i >= 0; --i) {
            const EvaluatedObject& obj = localSelected[static_cast<size_t>(i)];
            const int evalX = (obj.x == -256) ? 0 : obj.x;
            const int cols = (obj.dims.width + 7) / 8;
            for (int col = 0; col < cols; ++col) {
                const int tileX = evalX + col * 8;
                if (tileX <= -8 || tileX >= 256) continue;
                if (slivers < maxSlivers) {
                    localSliverMask[static_cast<size_t>(i)] |= static_cast<uint8_t>(1u << col);
                    ++slivers;
                }
            }
        }
        selected = localSelected.data();
        sliverMask = localSliverMask.data();
    }
    if (useFetchedPipeline) {
        const bool mainWindowed = (tmw_ & 0x10) != 0;
        const bool subWindowed = (tsw_ & 0x10) != 0;
        const bool anyWindowed = mainWindowed || subWindowed;
        for (int i = 0; i < selectedCount; ++i) {
            const size_t objIndex = static_cast<size_t>(i);
            uint8_t mask = sliverMask[objIndex];
            if (mask == 0) continue;
            const int spriteX = selected[objIndex].x;
            for (int col = 0; col < 8; ++col) {
                if ((mask & (1u << col)) == 0) continue;
                const FetchedObjectSliver& fetched = obj_ready_fetched_[objIndex][static_cast<size_t>(col)];
                if (!fetched.valid) continue;
                const int sliverX = spriteX + col * 8;
                for (int px = 0; px < 8; ++px) {
                    const uint8_t pixel = fetched.pixels[static_cast<size_t>(px)];
                    if (pixel == 0) continue;
                    const int screenX = sliverX + px;
                    if (screenX < 0 || screenX >= 256) continue;
                    const size_t screenIndex = static_cast<size_t>(screenX);
                    bool writeMain = mainEnabled && mainPixels[screenIndex].transparent;
                    bool writeSub = subEnabled && subPixels[screenIndex].transparent;
                    if (!writeMain && !writeSub) continue;
                    if (anyWindowed) {
                        const bool masked = windowResult(4, screenX);
                        if (mainWindowed && masked) writeMain = false;
                        if (subWindowed && masked) writeSub = false;
                        if (!writeMain && !writeSub) continue;
                    }
                    Pixel obj{};
                    obj.priority = fetched.priority;
                    obj.cgramIndex = static_cast<uint8_t>(128 + fetched.palette * 16 + pixel);
                    obj.color = cgram_[obj.cgramIndex];
                    obj.layer = 4;
                    obj.objPalette = fetched.palette;
                    obj.transparent = false;
                    if (writeMain) mainPixels[screenIndex] = obj;
                    if (writeSub) subPixels[screenIndex] = obj;
                }
            }
        }
        return;
    }
    for (int i = 0; i < selectedCount; ++i) {
        const EvaluatedObject& selectedObj = selected[static_cast<size_t>(i)];
        const int sprite = selectedObj.sprite;
        const int base = sprite * 4;
        const int x = selectedObj.x;
        const int localY0 = selectedObj.localY;
        const ObjectDimensions dims = selectedObj.dims;
        uint8_t tile = 0;
        uint8_t attr = 0;
        int localY = 0;
        uint32_t selectedBase = 0;
        uint8_t priority = 0;
        uint8_t palette = 0;
        if (!useFetchedPipeline) {
            tile = oam_[base + 2];
            attr = oam_[base + 3];
            if (objInterlace) {
                const int rangeH = dims.height * 2;
                const int flipped = (attr & 0x80) ? (rangeH - 1 - localY0) : localY0;
                if (((flipped & 1) != 0) != oddField) continue;
                localY = flipped >> 1;
            } else {
                localY = (attr & 0x80) ? (dims.height - 1 - localY0) : localY0;
            }
            selectedBase = (charBase + ((attr & 0x01) ? nameOffset : 0)) & 0xFFFF;
            priority = static_cast<uint8_t>(4 + (((attr >> 4) & 3) * 3));
            palette = static_cast<uint8_t>((attr >> 1) & 7);
        }
        for (int sx = 0; sx < dims.width; ++sx) {
            const int screenX = x + sx;
            if (screenX < 0 || screenX >= 256) continue;
            const int screenTileCol = sx >> 3;
            if ((sliverMask[static_cast<size_t>(i)] & (1u << screenTileCol)) == 0) continue;
            const bool writeMain = mainEnabled && mainPixels[static_cast<size_t>(screenX)].transparent &&
                !layerWindowMasked(4, screenX, false);
            const bool writeSub = subEnabled && subPixels[static_cast<size_t>(screenX)].transparent &&
                !layerWindowMasked(4, screenX, true);
            if (!writeMain && !writeSub) continue;
            uint8_t pixel = 0;
            uint8_t pixelPalette = palette;
            uint8_t pixelPriority = priority;
            if (useFetchedPipeline) {
                const FetchedObjectSliver& fetched = obj_ready_fetched_[static_cast<size_t>(i)][static_cast<size_t>(screenTileCol)];
                if (!fetched.valid) continue;
                pixel = fetched.pixels[static_cast<size_t>(sx & 7)];
                pixelPalette = fetched.palette;
                pixelPriority = fetched.priority;
            } else {
                const int localX = (attr & 0x40) ? (dims.width - 1 - sx) : sx;
                const int tileX = localX >> 3;
                const int tileY = localY >> 3;
                const uint8_t tileNumber = static_cast<uint8_t>(
                    (((tile >> 4) + tileY) & 0x0F) << 4 |
                    (((tile & 0x0F) + tileX) & 0x0F));
                pixel = tilePixel(selectedBase, tileNumber, 4, localX & 7, localY & 7);
            }
            if (pixel == 0) continue;
            Pixel obj{};
            obj.priority = pixelPriority;
            obj.cgramIndex = static_cast<uint8_t>(128 + pixelPalette * 16 + pixel);
            obj.color = cgram_[obj.cgramIndex];
            obj.layer = 4;
            obj.objPalette = pixelPalette;
            obj.transparent = false;
            if (writeMain) mainPixels[static_cast<size_t>(screenX)] = obj;
            if (writeSub) subPixels[static_cast<size_t>(screenX)] = obj;
        }
    }
}
PPU::Pixel PPU::mode7Pixel(int x, int y, bool subScreen) const {
    Pixel result{};
    const uint8_t enable = subScreen ? ts_ : tm_;
    const bool extbg = (setini_ & 0x40) != 0;
    const bool bg1On = (enable & 0x01) != 0 && !layerWindowMasked(0, x, subScreen);
    const bool bg2On = extbg && (enable & 0x02) != 0 && !layerWindowMasked(1, x, subScreen);
    if (!bg1On && !bg2On) return result;
    const int16_t a = static_cast<int16_t>(m7a_);
    const int16_t b = static_cast<int16_t>(m7b_);
    const int16_t c = static_cast<int16_t>(m7c_);
    const int16_t d = static_cast<int16_t>(m7d_);
    auto sign13 = [](uint16_t value) -> int16_t {
        value &= 0x1FFF;
        return static_cast<int16_t>((value & 0x1000) ? (value | 0xE000) : value);
    };
    const int16_t cx = sign13(m7x_);
    const int16_t cy = sign13(m7y_);
    const int16_t ho = sign13(m7_hofs_);
    const int16_t vo = sign13(m7_vofs_);
    auto sampleRaw = [&](bool bg2) -> uint8_t {
        int sx = x;
        int sy = y;
        const int mosaicSize = ((mosaic_ >> 4) & 0x0F) + 1;
        if (bg2) {
            if (mosaic_ & 0x02) sx -= sx % mosaicSize;
            if (mosaic_ & 0x01) sy = y - mosaicVOffset();
        } else if (mosaic_ & 0x01) {
            sx -= sx % mosaicSize;
            sy = y - mosaicVOffset();
        }
        if (m7sel_ & 0x01) sx = 255 - sx;
        if (m7sel_ & 0x02) sy = 255 - sy;
        const auto clip = [](int32_t n) -> int32_t { return (n & 0x2000) ? (n | ~1023) : (n & 1023); };
        const int32_t hOffset = clip(static_cast<int32_t>(ho) - cx);
        const int32_t vOffset = clip(static_cast<int32_t>(vo) - cy);
        const int32_t originX = ((a * hOffset) & ~63) + ((b * vOffset) & ~63) + ((b * sy) & ~63) + cx * 256;
        const int32_t originY = ((c * hOffset) & ~63) + ((d * vOffset) & ~63) + ((d * sy) & ~63) + cy * 256;
        const int32_t pixelX = (originX + a * sx) >> 8;
        const int32_t pixelY = (originY + c * sx) >> 8;
        const int repeat = (m7sel_ >> 6) & 3;
        const bool outside = ((pixelX | pixelY) & ~1023) != 0;
        if (repeat == 2 && outside) return 0;
        uint8_t tile = 0;
        if (!(repeat == 3 && outside)) {
            const uint32_t mapWord = static_cast<uint32_t>(((pixelY >> 3) & 127) * 128 + ((pixelX >> 3) & 127));
            tile = vram_[(mapWord * 2) & 0xFFFF];
        }
        const uint32_t chr = static_cast<uint32_t>(tile) * 64 + static_cast<uint32_t>(pixelY & 7) * 8 + static_cast<uint32_t>(pixelX & 7);
        return vram_[(chr * 2 + 1) & 0xFFFF];
    };
    if (bg1On) {
        const uint8_t raw = sampleRaw(false);
        if (raw != 0) {
            result.priority = 5;
            result.layer = 0;
            result.cgramIndex = raw;
            result.transparent = false;
            result.color = (cgwsel_ & 0x01) ? directColor(0, raw) : cgram_[raw];
        }
    }
    if (bg2On) {
        const uint8_t raw = sampleRaw(true);
        const uint8_t bg2Index = static_cast<uint8_t>(raw & 0x7F);
        if (bg2Index != 0) {
            const uint8_t bg2Priority = (raw & 0x80) ? 9 : 3;
            if (result.transparent || bg2Priority >= result.priority) {
                result.priority = bg2Priority;
                result.layer = 1;
                result.cgramIndex = bg2Index;
                result.transparent = false;
                result.color = cgram_[bg2Index];
            }
        }
    }
    return result;
}
PPU::Pixel PPU::screenPixel(int x, int y, bool subScreen) const {
    if ((bgmode_ & 7) == 7) return mode7Pixel(x, y, subScreen);
    Pixel best{};
    const uint8_t enable = subScreen ? ts_ : tm_;
    for (int bg = 3; bg >= 0; --bg) {
        if ((enable & (1u << bg)) == 0 || layerWindowMasked(bg, x, subScreen)) continue;
        uint8_t pri = 0, pal = 0;
        const uint8_t index = backgroundPixel(bg, x, y, pri, pal, subScreen);
        if (index == kTransparent || pri < best.priority) continue;
        best.priority = pri;
        best.layer = static_cast<uint8_t>(bg);
        best.cgramIndex = index;
        best.transparent = false;
        if (bg == 0 && (((bgmode_ & 7) == 3) || ((bgmode_ & 7) == 4)) && (cgwsel_ & 0x01)) best.color = directColor(pal, index);
        else best.color = cgram_[index];
    }
    return best;
}
void PPU::screenPixelPairMode1(int x, int y, bool win1, bool win2, Pixel& main, Pixel& sub) const {
    const uint8_t anyEnable = static_cast<uint8_t>((tm_ | ts_) & 0x07);
    if (anyEnable == 0) return;
    for (int bg = 2; bg >= 0; --bg) {
        const uint8_t bit = static_cast<uint8_t>(1u << bg);
        if ((anyEnable & bit) == 0) continue;
        const bool onMain = (tm_ & bit) != 0 && ((tmw_ & bit) == 0 || !windowResultFromGeometry(bg, win1, win2));
        const bool onSub = (ts_ & bit) != 0 && ((tsw_ & bit) == 0 || !windowResultFromGeometry(bg, win1, win2));
        if (!onMain && !onSub) continue;
        uint8_t pri = 0;
        const uint8_t index = backgroundPixelMode1(bg, x, y, pri);
        if (index == kTransparent) continue;
        if (onMain && pri >= main.priority) {
            main.priority = pri;
            main.layer = static_cast<uint8_t>(bg);
            main.cgramIndex = index;
            main.transparent = false;
        }
        if (onSub && pri >= sub.priority) {
            sub.priority = pri;
            sub.layer = static_cast<uint8_t>(bg);
            sub.cgramIndex = index;
            sub.transparent = false;
        }
    }
}
void PPU::screenPixelPair(int x, int y, Pixel& main, Pixel& sub) const {
    const uint8_t mode = bgmode_ & 7;
    if (mode >= 5) {
        if ((tm_ & 0x0F) != 0) main = screenPixel(x, y, false);
        if ((ts_ & 0x0F) != 0) sub = screenPixel(x, y, true);
        return;
    }
    const uint8_t anyEnable = static_cast<uint8_t>((tm_ | ts_) & 0x0F);
    if (anyEnable == 0) return;
    for (int bg = 3; bg >= 0; --bg) {
        const uint8_t bit = static_cast<uint8_t>(1u << bg);
        if ((anyEnable & bit) == 0) continue;
        const bool onMain = (tm_ & bit) != 0 && !layerWindowMasked(bg, x, false);
        const bool onSub = (ts_ & bit) != 0 && !layerWindowMasked(bg, x, true);
        if (!onMain && !onSub) continue;
        uint8_t pri = 0, pal = 0;
        const uint8_t index = (mode == 1)
            ? backgroundPixelMode1(bg, x, y, pri)
            : backgroundPixel(bg, x, y, pri, pal, false);
        if (index == kTransparent) continue;
        const bool direct = bg == 0 && (mode == 3 || mode == 4) && (cgwsel_ & 0x01);
        const uint16_t color = direct ? directColor(pal, index) : cgram_[index];
        if (onMain && pri >= main.priority) {
            main.priority = pri;
            main.layer = static_cast<uint8_t>(bg);
            main.cgramIndex = index;
            main.transparent = false;
            main.color = color;
        }
        if (onSub && pri >= sub.priority) {
            sub.priority = pri;
            sub.layer = static_cast<uint8_t>(bg);
            sub.cgramIndex = index;
            sub.transparent = false;
            sub.color = color;
        }
    }
}
bool PPU::colorMathAllowed(const Pixel& main, bool insideColorWindow) const {
    static constexpr uint8_t kAllowedRegionMask[4] = { 0x3, 0x2, 0x1, 0x0 };
    const uint8_t mode = static_cast<uint8_t>((cgwsel_ >> 4) & 3);
    if (((kAllowedRegionMask[mode] >> static_cast<unsigned>(insideColorWindow)) & 1u) == 0)
        return false;
    if (main.layer == 4 && main.objPalette < 4) return false;
    const uint8_t bit = main.layer < 5 ? main.layer : 5;
    return (cgadsub_ & (1u << bit)) != 0;
}
uint16_t PPU::addColors(uint16_t a, uint16_t b, bool subtract, bool half) {
    const uint32_t x = a & 0x7FFFu;
    const uint32_t y = b & 0x7FFFu;
    if (!subtract) {
        if (half) {
            return static_cast<uint16_t>((x + y - ((x ^ y) & 0x0421u)) >> 1);
        }
        const uint32_t sum = x + y;
        const uint32_t carry = (sum - ((x ^ y) & 0x0421u)) & 0x8420u;
        return static_cast<uint16_t>(((sum - carry) | (carry - (carry >> 5))) & 0x7FFFu);
    }
    const uint32_t diff = x - y + 0x8420u;
    const uint32_t borrow = (diff - ((x ^ y) & 0x8420u)) & 0x8420u;
    uint32_t result = (diff - borrow) & (borrow - (borrow >> 5));
    if (half) result = (result & 0x7BDEu) >> 1;
    return static_cast<uint16_t>(result & 0x7FFFu);
}
uint32_t PPU::rgbaFromBgr555(uint16_t color, uint8_t brightness) {
    const size_t level = static_cast<size_t>(std::min<uint8_t>(brightness, 15));
    return kBgr555Rgba[level * 32768u + (color & 0x7FFFu)];
}
void PPU::ensureLineObjectLayers(int line) {
    if (line_obj_line_ == line) return;
    line_obj_line_ = line;
    for (auto& p : line_obj_main_) p = Pixel{};
    for (auto& p : line_obj_sub_) p = Pixel{};
    renderObjectPair(line, line_obj_main_, line_obj_sub_);
}
void PPU::renderPixel(int x, int line) {
    if (line < 1 || line > nativeHeight() || x < 0 || x >= 256) return;
    struct SetiniRestore {
        uint8_t& target;
        uint8_t saved;
        bool active;
        ~SetiniRestore() { if (active) target = saved; }
    } setiniRestore{setini_, setini_, setini_early_pending_};
    if (setiniRestore.active) {
        setini_ = setini_early_value_;
        setini_early_pending_ = false;
    }
    uint8_t renderInidisp = inidisp_;
    if (inidisp_early_pending_) {
        renderInidisp = inidisp_early_value_;
        inidisp_early_pending_ = false;
    }
    const size_t rowBase = static_cast<size_t>((line - 1) * 512);
    const bool hires = hiresOutputEnabled();
    if (line == 1 && x == 0)
        frame_hires_output_ = false;
    if (hires && !frame_hires_output_) {
        for (int priorLine = 1; priorLine < line; ++priorLine) {
            const size_t priorBase = static_cast<size_t>((priorLine - 1) * 512);
            for (int px = 255; px >= 0; --px) {
                const uint32_t c = framebuffer_[priorBase + static_cast<size_t>(px)];
                framebuffer_[priorBase + static_cast<size_t>(px * 2)] = c;
                framebuffer_[priorBase + static_cast<size_t>(px * 2 + 1)] = c;
            }
        }
        for (int px = x - 1; px >= 0; --px) {
            const uint32_t c = framebuffer_[rowBase + static_cast<size_t>(px)];
            framebuffer_[rowBase + static_cast<size_t>(px * 2)] = c;
            framebuffer_[rowBase + static_cast<size_t>(px * 2 + 1)] = c;
        }
        frame_hires_output_ = true;
    }
    const bool wideFrame = frame_hires_output_;
    if (line == 1 && x == 0) {
        frame_present_ = mode7_widescreen_ || mode7_hd_scale_ > 1;
        frame_pad_ = mode7_widescreen_ ? WIDESCREEN_PAD : 0;
        frame_scale_ = mode7_hd_scale_;
    }
    if (frame_present_) presentationLineHook(x, line, renderInidisp);
    if (pipeline_cgram_line_ != line) {
        pipeline_cgram_line_ = line;
        pipeline_main_cgram_.fill(0);
        pipeline_sub_cgram_.fill(0);
    }
    if (renderInidisp & 0x80) {
        if (wideFrame) {
            framebuffer_[rowBase + static_cast<size_t>(x * 2)] = 0xFF000000u;
            framebuffer_[rowBase + static_cast<size_t>(x * 2 + 1)] = 0xFF000000u;
        } else {
            framebuffer_[rowBase + static_cast<size_t>(x)] = 0xFF000000u;
        }
        pipeline_main_cgram_[static_cast<size_t>(x)] = 0;
        pipeline_sub_cgram_[static_cast<size_t>(x)] = 0;
        return;
    }
    const bool anyObjEnabled = ((tm_ | ts_) & 0x10) != 0;
    if (anyObjEnabled && line_obj_line_ != line) ensureLineObjectLayers(line);
    if (jit_scanline_enabled_ && !setiniRestore.active && renderInidisp == inidisp_) {
        if (x == 0) {
            jit_line_ = buildJitLine(line, static_cast<uint8_t>(renderInidisp & 0x0F), rowBase) ? line : -1;
            jit_gen_ = reg_generation_;
        }
        if (jit_line_ == line && jit_gen_ == reg_generation_) return;
    }
    if ((bgmode_ & 7) == 1 && !hires && ts_ == 0 && (tmw_ & 0x07) == 0 &&
        (cgwsel_ & 0xC0) == 0) {
        const uint8_t possibleMathSources = static_cast<uint8_t>((tm_ & 0x17) | 0x20);
        const bool mathGloballyPrevented = ((cgwsel_ >> 4) & 3) == 3;
        const bool noSelectedMathSource = (cgadsub_ & possibleMathSources) == 0;
        const bool colorWindowEnabled = (wobjsel_ & 0xA0) != 0;
        if (mathGloballyPrevented || noSelectedMathSource || !colorWindowEnabled) {
            Pixel main;
            const uint8_t enable = static_cast<uint8_t>(tm_ & 0x07);
            if (enable == 0x03) {
                uint8_t pri = 0;
                uint8_t index = backgroundPixelMode1(1, x, line, pri);
                if (index != kTransparent) {
                    main.priority = pri;
                    main.layer = 1;
                    main.cgramIndex = index;
                    main.transparent = false;
                }
                pri = 0;
                index = backgroundPixelMode1(0, x, line, pri);
                if (index != kTransparent && pri >= main.priority) {
                    main.priority = pri;
                    main.layer = 0;
                    main.cgramIndex = index;
                    main.transparent = false;
                }
            } else if (enable == 0x01) {
                uint8_t pri = 0;
                const uint8_t index = backgroundPixelMode1(0, x, line, pri);
                if (index != kTransparent) {
                    main.priority = pri;
                    main.layer = 0;
                    main.cgramIndex = index;
                    main.transparent = false;
                }
            } else if (enable == 0x02) {
                uint8_t pri = 0;
                const uint8_t index = backgroundPixelMode1(1, x, line, pri);
                if (index != kTransparent) {
                    main.priority = pri;
                    main.layer = 1;
                    main.cgramIndex = index;
                    main.transparent = false;
                }
            } else {
                for (int bg = 2; bg >= 0; --bg) {
                    const uint8_t bit = static_cast<uint8_t>(1u << bg);
                    if ((enable & bit) == 0) continue;
                    uint8_t pri = 0;
                    const uint8_t index = backgroundPixelMode1(bg, x, line, pri);
                    if (index == kTransparent || pri < main.priority) continue;
                    main.priority = pri;
                    main.layer = static_cast<uint8_t>(bg);
                    main.cgramIndex = index;
                    main.transparent = false;
                }
            }
            if (main.transparent) {
                main.cgramIndex = 0;
                main.layer = 5;
                main.transparent = false;
            }
            if ((tm_ & 0x10) != 0) {
                const Pixel& objMain = line_obj_main_[static_cast<size_t>(x)];
                if (!objMain.transparent && objMain.priority >= main.priority) main = objMain;
            }
            main.color = cgram_[main.cgramIndex & 0xFF];
            pipeline_main_cgram_[static_cast<size_t>(x)] = main.cgramIndex;
            pipeline_sub_cgram_[static_cast<size_t>(x)] = 0;
            uint16_t color = main.color;
            bool mathEnable = false;
            if (!mathGloballyPrevented && !noSelectedMathSource) {
                mathEnable = colorMathAllowed(main, false);
                if (mathEnable) {
                    const bool useSubscreen = (cgwsel_ & 0x02) != 0;
                    const bool half = (cgadsub_ & 0x40) != 0 && !useSubscreen;
                    color = addColors(color, fixed_color_, (cgadsub_ & 0x80) != 0, half);
                }
            }
            const uint8_t brightness = static_cast<uint8_t>(renderInidisp & 0x0F);
            const uint32_t rgba = mathEnable
                ? rgbaFromBgr555(color, brightness)
                : cgram_rgba_cache_[brightness][main.cgramIndex & 0xFF];
            if (wideFrame) {
                framebuffer_[rowBase + static_cast<size_t>(x * 2)] = rgba;
                framebuffer_[rowBase + static_cast<size_t>(x * 2 + 1)] = rgba;
            } else {
                framebuffer_[rowBase + static_cast<size_t>(x)] = rgba;
            }
            return;
        }
    }
    Pixel main;
    Pixel sub;
    bool win1 = false;
    bool win2 = false;
    const bool needWindowGeometry = ((tmw_ | tsw_) & 0x07) != 0 || (wobjsel_ & 0xA0) != 0;
    if (needWindowGeometry) {
        win1 = (wh0_ <= wh1_) && (x >= static_cast<int>(wh0_)) && (x <= static_cast<int>(wh1_));
        win2 = (wh2_ <= wh3_) && (x >= static_cast<int>(wh2_)) && (x <= static_cast<int>(wh3_));
    }
    bool useFast = false;
    if (fast_scanline_enabled_ && !setiniRestore.active) {
        if (x == 0) {
            fast_line_ = buildFastLine(line) ? line : -1;
            fast_gen_ = reg_generation_;
        }
        useFast = fast_line_ == line && fast_gen_ == reg_generation_;
    }
    if (useFast) {
        main = fast_main_[static_cast<size_t>(x)];
        sub = fast_sub_[static_cast<size_t>(x)];
    } else if ((bgmode_ & 7) == 1) screenPixelPairMode1(x, line, win1, win2, main, sub);
    else screenPixelPair(x, line, main, sub);
    if (main.transparent) {
        main.color = cgram_[0];
        main.cgramIndex = 0;
        main.layer = 5;
        main.transparent = false;
    }
    if (sub.transparent) {
        sub.color = cgram_[0];
        sub.cgramIndex = 0;
        sub.layer = 5;
    }
    if ((tm_ & 0x10) != 0) {
        const Pixel& objMain = line_obj_main_[static_cast<size_t>(x)];
        if (!objMain.transparent && objMain.priority >= main.priority) main = objMain;
    }
    if ((ts_ & 0x10) != 0) {
        const Pixel& objSub = line_obj_sub_[static_cast<size_t>(x)];
        if (!objSub.transparent && objSub.priority >= sub.priority) sub = objSub;
    }
    const bool subBackdrop = sub.transparent;
    if (sub.transparent) sub.transparent = false;
    const uint8_t activeMode = static_cast<uint8_t>(bgmode_ & 7);
    const bool mainDirect = (cgwsel_ & 0x01) != 0 && main.layer == 0 &&
        (activeMode == 3 || activeMode == 4 || activeMode == 7);
    const bool subDirect = (cgwsel_ & 0x01) != 0 && sub.layer == 0 &&
        (activeMode == 3 || activeMode == 4 || activeMode == 7);
    if (!mainDirect)
        main.color = cgram_[main.cgramIndex & 0xFF];
    if (!subDirect)
        sub.color = cgram_[sub.cgramIndex & 0xFF];
    pipeline_main_cgram_[static_cast<size_t>(x)] = main.cgramIndex;
    pipeline_sub_cgram_[static_cast<size_t>(x)] = sub.cgramIndex;
    const uint8_t clipMode = static_cast<uint8_t>((cgwsel_ >> 6) & 3);
    const bool insideColorWindow = colorWindowResultFromGeometry(win1, win2);
    static constexpr uint8_t kClipRegionMask[4] = { 0x0, 0x1, 0x2, 0x3 };
    const bool clipMainToBlack =
        ((kClipRegionMask[clipMode] >> static_cast<unsigned>(insideColorWindow)) & 1u) != 0;
    const bool mainColorEnable = !clipMainToBlack;
    const bool mathEnable = colorMathAllowed(main, insideColorWindow);
    const bool useSubscreen = (cgwsel_ & 0x02) != 0;
    uint16_t color = mainColorEnable ? main.color : 0;
    bool currentBlendMode = false;
    bool currentColorHalve = false;
    if (mathEnable) {
        if (useSubscreen && subBackdrop) {
            currentBlendMode = false;
            currentColorHalve = false;
        } else {
            currentBlendMode = useSubscreen;
            currentColorHalve = (cgadsub_ & 0x40) != 0 && mainColorEnable;
        }
        const uint16_t rhs = currentBlendMode ? sub.color : fixed_color_;
        color = addColors(color, rhs, (cgadsub_ & 0x80) != 0, currentColorHalve);
    }
    const uint8_t brightness = static_cast<uint8_t>(renderInidisp & 0x0F);
    const uint32_t rgba = rgbaFromBgr555(color, brightness);
    if (hires) {
        if (hires_dac_line_ != line || x == 0) {
            hires_dac_line_ = line;
            hires_prev_main_color_ = cgram_[0];
            hires_prev_main_color_enable_ = false;
            hires_prev_math_enable_ = false;
            hires_prev_blend_mode_ = false;
            hires_prev_color_halve_ = false;
        }
        uint16_t belowColor = hires_prev_main_color_enable_ ? sub.color : 0;
        if (hires_prev_math_enable_) {
            const uint16_t rhs = hires_prev_blend_mode_ ? hires_prev_main_color_ : fixed_color_;
            belowColor = addColors(belowColor, rhs, (cgadsub_ & 0x80) != 0,
                                   hires_prev_color_halve_);
        }
        framebuffer_[rowBase + static_cast<size_t>(x * 2)] =
            rgbaFromBgr555(belowColor, brightness);
        framebuffer_[rowBase + static_cast<size_t>(x * 2 + 1)] = rgba;
        hires_prev_main_color_ = main.color;
        hires_prev_main_color_enable_ = mainColorEnable;
        hires_prev_math_enable_ = mathEnable;
        if (mathEnable) {
            hires_prev_blend_mode_ = currentBlendMode;
            hires_prev_color_halve_ = currentColorHalve;
        }
    } else if (wideFrame) {
        framebuffer_[rowBase + static_cast<size_t>(x * 2)] = rgba;
        framebuffer_[rowBase + static_cast<size_t>(x * 2 + 1)] = rgba;
    } else {
        framebuffer_[rowBase + static_cast<size_t>(x)] = rgba;
    }
    if (frame_present_ && frame_scale_ > 1 && !hires && activeMode == 7 && main.layer == 0 && (mosaic_ & 0x01) == 0)
        presentMode7Block(x, line, main, sub, subBackdrop, insideColorWindow, brightness, true, x);
}
void PPU::renderScanline(int line) {
    if (line < 1 || line > nativeHeight()) return;
    line_obj_line_ = -1;
    for (int x = 0; x < 256; ++x)
        renderPixel(x, line);
}
void PPU::setMode7Widescreen(bool enabled) {
    if (mode7_widescreen_ == enabled) return;
    mode7_widescreen_ = enabled;
    reconfigurePresentation();
}
void PPU::setMode7HdScale(int scale) {
    scale = std::clamp(scale, 1, MAX_HD_SCALE);
    if (mode7_hd_scale_ == scale) return;
    mode7_hd_scale_ = scale;
    reconfigurePresentation();
}
void PPU::reconfigurePresentation() {
    present_valid_ = false;
    frame_present_ = false;
    if (mode7_widescreen_ || mode7_hd_scale_ > 1) {
        const size_t overlayWidth = static_cast<size_t>(256 + 2 * WIDESCREEN_PAD) * MAX_HD_SCALE;
        const size_t outputWidth = static_cast<size_t>(512 + 4 * WIDESCREEN_PAD) * MAX_HD_SCALE;
        const size_t rows = static_cast<size_t>(239) * MAX_HD_SCALE;
        present_overlay_.assign(overlayWidth * rows, 0u);
        present_output_.assign(outputWidth * rows, 0xFF000000u);
    } else {
        std::vector<uint32_t>().swap(present_overlay_);
        std::vector<uint32_t>().swap(present_output_);
    }
}
void PPU::presentationLineHook(int x, int line, uint8_t inidisp) {
    const size_t scale = static_cast<size_t>(frame_scale_);
    const size_t overlayWidth = static_cast<size_t>(256 + 2 * frame_pad_) * scale;
    uint32_t* rows = present_overlay_.data() + static_cast<size_t>(line - 1) * scale * overlayWidth;
    if (x == 0) {
        std::fill(rows, rows + scale * overlayWidth, 0u);
        for (int p = 0; p < frame_pad_; ++p) presentPad(p - frame_pad_, line, inidisp);
    }
    if (x == 255) {
        for (int p = 0; p < frame_pad_; ++p) presentPad(256 + p, line, inidisp);
    }
}
void PPU::presentPad(int logicalX, int line, uint8_t inidisp) {
    const size_t scale = static_cast<size_t>(frame_scale_);
    const size_t overlayWidth = static_cast<size_t>(256 + 2 * frame_pad_) * scale;
    uint32_t* base = present_overlay_.data() + static_cast<size_t>(line - 1) * scale * overlayWidth +
                     static_cast<size_t>(logicalX + frame_pad_) * scale;
    const uint8_t mode = bgmode_ & 7;
    if ((inidisp & 0x80) != 0 || mode == 5 || mode == 6) {
        for (size_t j = 0; j < scale; ++j)
            for (size_t i = 0; i < scale; ++i) base[j * overlayWidth + i] = 0xFF000000u;
        return;
    }
    const int sampleX = logicalX;
    Pixel main;
    Pixel sub;
    if (mode == 7) {
        main = mode7Pixel(sampleX, line, false);
        sub = mode7Pixel(sampleX, line, true);
    } else {
        screenPixelPair(sampleX, line, main, sub);
    }
    if (main.transparent) {
        main.color = cgram_[0];
        main.cgramIndex = 0;
        main.layer = 5;
        main.transparent = false;
    }
    const bool subBackdrop = sub.transparent;
    if (sub.transparent) {
        sub.color = cgram_[0];
        sub.cgramIndex = 0;
        sub.layer = 5;
        sub.transparent = false;
    }
    presentMode7Block(logicalX, line, main, sub, subBackdrop, colorWindowResult(logicalX),
                      static_cast<uint8_t>(inidisp & 0x0F), mode == 7 && main.layer == 0 && (mosaic_ & 0x01) == 0, sampleX);
}
void PPU::presentMode7Block(int logicalX, int line, const Pixel& main, const Pixel& sub, bool subBackdrop,
                            bool insideColorWindow, uint8_t brightness, bool hdEligible, int sampleX) {
    const int scale = frame_scale_;
    const size_t overlayWidth = static_cast<size_t>(256 + 2 * frame_pad_) * static_cast<size_t>(scale);
    uint32_t* base = present_overlay_.data() + static_cast<size_t>(line - 1) * static_cast<size_t>(scale) * overlayWidth +
                     static_cast<size_t>(logicalX + frame_pad_) * static_cast<size_t>(scale);
    const uint8_t clipMode = static_cast<uint8_t>((cgwsel_ >> 6) & 3);
    static constexpr uint8_t kClipRegionMask[4] = { 0x0, 0x1, 0x2, 0x3 };
    const bool clipMainToBlack =
        ((kClipRegionMask[clipMode] >> static_cast<unsigned>(insideColorWindow)) & 1u) != 0;
    const bool mainColorEnable = !clipMainToBlack;
    const bool mathEnable = colorMathAllowed(main, insideColorWindow);
    const bool useSubscreen = (cgwsel_ & 0x02) != 0;
    bool blend = false;
    bool halve = false;
    if (mathEnable && !(useSubscreen && subBackdrop)) {
        blend = useSubscreen;
        halve = (cgadsub_ & 0x40) != 0 && mainColorEnable;
    }
    const uint16_t rhs = blend ? sub.color : fixed_color_;
    for (int j = 0; j < scale; ++j) {
        for (int i = 0; i < scale; ++i) {
            uint16_t mainColor = main.color;
            uint8_t raw = 0;
            if (hdEligible && mode7HdRaw(sampleX, line, i, j, raw) && raw != 0)
                mainColor = (cgwsel_ & 0x01) ? directColor(0, raw) : cgram_[raw];
            uint16_t color = mainColorEnable ? mainColor : 0;
            if (mathEnable) color = addColors(color, rhs, (cgadsub_ & 0x80) != 0, halve);
            base[static_cast<size_t>(j) * overlayWidth + static_cast<size_t>(i)] = rgbaFromBgr555(color, brightness);
        }
    }
}
bool PPU::mode7HdRaw(int logicalX, int line, int subX, int subY, uint8_t& raw) const {
    const int64_t scale = frame_scale_;
    const int64_t denom = 2 * scale;
    int64_t xn = denom * logicalX + (2 * subX + 1 - scale);
    int64_t yn = denom * line + (2 * subY + 1 - scale);
    if (m7sel_ & 0x01) xn = denom * 255 - xn;
    if (m7sel_ & 0x02) yn = denom * 255 - yn;
    auto floorDiv = [](int64_t n, int64_t d) -> int64_t {
        int64_t q = n / d;
        if ((n % d != 0) && ((n < 0) != (d < 0))) --q;
        return q;
    };
    auto sign13 = [](uint16_t value) -> int16_t {
        value &= 0x1FFF;
        return static_cast<int16_t>((value & 0x1000) ? (value | 0xE000) : value);
    };
    const int64_t a = static_cast<int16_t>(m7a_);
    const int64_t b = static_cast<int16_t>(m7b_);
    const int64_t c = static_cast<int16_t>(m7c_);
    const int64_t d = static_cast<int16_t>(m7d_);
    const int64_t cx = sign13(m7x_);
    const int64_t cy = sign13(m7y_);
    const int64_t ho = sign13(m7_hofs_);
    const int64_t vo = sign13(m7_vofs_);
    const auto clip = [](int64_t n) -> int64_t { return (n & 0x2000) ? (n | ~int64_t{1023}) : (n & 1023); };
    const int64_t hOffset = clip(ho - cx);
    const int64_t vOffset = clip(vo - cy);
    const int64_t originX = ((a * hOffset) & ~int64_t{63}) + ((b * vOffset) & ~int64_t{63}) +
                            (floorDiv(b * yn, denom) & ~int64_t{63}) + cx * 256;
    const int64_t originY = ((c * hOffset) & ~int64_t{63}) + ((d * vOffset) & ~int64_t{63}) +
                            (floorDiv(d * yn, denom) & ~int64_t{63}) + cy * 256;
    const int64_t pixelX = floorDiv(originX * denom + a * xn, denom * 256);
    const int64_t pixelY = floorDiv(originY * denom + c * xn, denom * 256);
    const int repeat = (m7sel_ >> 6) & 3;
    const bool outside = ((pixelX | pixelY) & ~int64_t{1023}) != 0;
    if (repeat == 2 && outside) {
        raw = 0;
        return true;
    }
    uint8_t tile = 0;
    if (!(repeat == 3 && outside)) {
        const uint32_t mapWord = static_cast<uint32_t>(((pixelY >> 3) & 127) * 128 + ((pixelX >> 3) & 127));
        tile = vram_[(mapWord * 2) & 0xFFFF];
    }
    const uint32_t chr = static_cast<uint32_t>(tile) * 64 + static_cast<uint32_t>(pixelY & 7) * 8 + static_cast<uint32_t>(pixelX & 7);
    raw = vram_[(chr * 2 + 1) & 0xFFFF];
    return true;
}
void PPU::composePresentation() {
    const int scale = frame_scale_;
    const int pad = frame_pad_;
    const bool hr = frame_hires_output_;
    const int unit = hr ? 2 : 1;
    const int centerWidth = 256 * unit;
    const int rowsNative = nativeHeight();
    const int outWidth = (centerWidth + 2 * pad * unit) * scale;
    const size_t overlayWidth = static_cast<size_t>(256 + 2 * pad) * static_cast<size_t>(scale);
    for (int line = 1; line <= rowsNative; ++line) {
        const uint32_t* legacy = framebuffer_.data() + static_cast<size_t>(line - 1) * 512;
        for (int j = 0; j < scale; ++j) {
            const size_t outRow = static_cast<size_t>(line - 1) * scale + j;
            uint32_t* dst = present_output_.data() + outRow * static_cast<size_t>(outWidth);
            const uint32_t* ov = present_overlay_.data() + outRow * overlayWidth;
            const uint32_t* padRow = ov;
            for (int p = 0; p < pad; ++p)
                for (int k = 0; k < unit; ++k)
                    for (int i = 0; i < scale; ++i) *dst++ = padRow[static_cast<size_t>(p) * scale + i];
            for (int cx = 0; cx < centerWidth; ++cx) {
                const size_t lx = static_cast<size_t>(cx / unit);
                for (int i = 0; i < scale; ++i) {
                    const uint32_t v = hr ? 0u : ov[(lx + pad) * scale + i];
                    *dst++ = v != 0 ? v : legacy[cx];
                }
            }
            for (int p = 0; p < pad; ++p)
                for (int k = 0; k < unit; ++k)
                    for (int i = 0; i < scale; ++i) *dst++ = padRow[static_cast<size_t>(256 + pad + p) * scale + i];
        }
    }
    present_pad_ = pad;
    present_width_ = outWidth;
    present_height_ = rowsNative * scale;
    present_valid_ = true;
}
void PPU::setFastScanline(bool enabled) {
    if (fast_scanline_enabled_ == enabled) return;
    fast_scanline_enabled_ = enabled;
    fast_line_ = -1;
}
void PPU::fillBackgroundLine(int bg, int line, uint8_t mode, std::array<uint8_t, 256>& index,
                             std::array<uint8_t, 256>& priority, std::array<uint8_t, 256>& palette) const {
    index.fill(kTransparent);
    const int bpp = backgroundBpp(mode, bg);
    if (bpp == 0) return;
    const bool tile16 = (bgmode_ & (0x10u << bg)) != 0;
    const int shift = tile16 ? 4 : 3;
    const int tileSize = 1 << shift;
    const int tileMask = tileSize - 1;
    const int hofs = static_cast<int>(bg_hofs_[bg] & 0x3FF);
    const int vofs = static_cast<int>(bg_vofs_[bg] & 0x3FF);
    const int sy = (line + vofs) & 0x3FF;
    const int tileY = sy >> shift;
    const int rawY = sy & tileMask;
    uint32_t tileBase = 0;
    if (bg == 0) tileBase = static_cast<uint32_t>(bg12nba_ & 0x0F) * 0x2000u;
    else if (bg == 1) tileBase = static_cast<uint32_t>((bg12nba_ >> 4) & 0x0F) * 0x2000u;
    else if (bg == 2) tileBase = static_cast<uint32_t>(bg34nba_ & 0x0F) * 0x2000u;
    else tileBase = static_cast<uint32_t>((bg34nba_ >> 4) & 0x0F) * 0x2000u;
    int x = 0;
    while (x < 256) {
        const int sx = (x + hofs) & 0x3FF;
        const int tileX = sx >> shift;
        const uint16_t entry = tilemapEntry(bg, tileX, tileY);
        const bool hflip = (entry & 0x4000) != 0;
        const bool vflip = (entry & 0x8000) != 0;
        const int sourceY = vflip ? tileSize - 1 - rawY : rawY;
        const uint16_t baseTile = static_cast<uint16_t>(entry & 0x03FF);
        const uint16_t verticalTile = static_cast<uint16_t>(baseTile + (tile16 ? (((sourceY >> 3) & 1) << 4) : 0));
        std::array<uint8_t, 8> left{};
        std::array<uint8_t, 8> right{};
        decodeTileRow(tileBase, verticalTile, bpp, sourceY & 7, left);
        if (tile16) decodeTileRow(tileBase, static_cast<uint16_t>(verticalTile + 1), bpp, sourceY & 7, right);
        const uint8_t pal = static_cast<uint8_t>((entry >> 10) & 7);
        const uint8_t pri = static_cast<uint8_t>(priorityFor(mode, bg, (entry & 0x2000) != 0, (bgmode_ & 0x08) != 0));
        for (int rawX = sx & tileMask; rawX < tileSize && x < 256; ++rawX, ++x) {
            const int localX = hflip ? tileSize - 1 - rawX : rawX;
            const uint8_t pixel = localX < 8 ? left[static_cast<size_t>(localX)] : right[static_cast<size_t>(localX - 8)];
            if (pixel == 0) continue;
            uint8_t value;
            if (mode == 0) value = static_cast<uint8_t>(bg * 32 + pal * 4 + pixel);
            else if (bpp == 2) value = static_cast<uint8_t>(pal * 4 + pixel);
            else if (bpp == 4) value = static_cast<uint8_t>(pal * 16 + pixel);
            else value = pixel;
            index[static_cast<size_t>(x)] = value;
            priority[static_cast<size_t>(x)] = pri;
            palette[static_cast<size_t>(x)] = pal;
        }
    }
}
bool PPU::buildFastLine(int line) {
    const uint8_t mode = bgmode_ & 7;
    if (mode != 0 && mode != 1 && mode != 3) return false;
    if ((mosaic_ & 0x0F) != 0 || hiresOutputEnabled()) return false;
    const uint8_t bgMask = mode == 1 ? 0x07 : 0x0F;
    const uint8_t anyEnable = static_cast<uint8_t>((tm_ | ts_) & bgMask);
    for (auto& p : fast_main_) p = Pixel{};
    for (auto& p : fast_sub_) p = Pixel{};
    if (anyEnable == 0) return true;
    std::array<std::array<uint8_t, 256>, 4> index;
    std::array<std::array<uint8_t, 256>, 4> priority;
    std::array<std::array<uint8_t, 256>, 4> palette;
    for (int bg = 0; bg < 4; ++bg) {
        if ((anyEnable & (1u << bg)) != 0) fillBackgroundLine(bg, line, mode, index[bg], priority[bg], palette[bg]);
        else index[bg].fill(kTransparent);
    }
    const bool directEnabled = (cgwsel_ & 0x01) != 0 && mode == 3;
    const int lastBg = mode == 1 ? 2 : 3;
    for (int x = 0; x < 256; ++x) {
        const bool win1 = (wh0_ <= wh1_) && (x >= static_cast<int>(wh0_)) && (x <= static_cast<int>(wh1_));
        const bool win2 = (wh2_ <= wh3_) && (x >= static_cast<int>(wh2_)) && (x <= static_cast<int>(wh3_));
        Pixel& main = fast_main_[static_cast<size_t>(x)];
        Pixel& sub = fast_sub_[static_cast<size_t>(x)];
        for (int bg = lastBg; bg >= 0; --bg) {
            const uint8_t bit = static_cast<uint8_t>(1u << bg);
            if ((anyEnable & bit) == 0) continue;
            const uint8_t value = index[bg][static_cast<size_t>(x)];
            if (value == kTransparent) continue;
            const bool windowed = ((tmw_ | tsw_) & bit) != 0 && windowResultFromGeometry(bg, win1, win2);
            const bool onMain = (tm_ & bit) != 0 && !(((tmw_ & bit) != 0) && windowed);
            const bool onSub = (ts_ & bit) != 0 && !(((tsw_ & bit) != 0) && windowed);
            if (!onMain && !onSub) continue;
            const uint8_t pri = priority[bg][static_cast<size_t>(x)];
            uint16_t color = 0;
            if (mode != 1) {
                const bool direct = bg == 0 && directEnabled;
                color = direct ? directColor(palette[bg][static_cast<size_t>(x)], value) : cgram_[value];
            }
            if (onMain && pri >= main.priority) {
                main.priority = pri;
                main.layer = static_cast<uint8_t>(bg);
                main.cgramIndex = value;
                main.transparent = false;
                main.color = color;
            }
            if (onSub && pri >= sub.priority) {
                sub.priority = pri;
                sub.layer = static_cast<uint8_t>(bg);
                sub.cgramIndex = value;
                sub.transparent = false;
                sub.color = color;
            }
        }
    }
    return true;
}
namespace {
struct JitCtx {
    const uint8_t* bgIdx[4];
    const uint8_t* bgPri[4];
    const uint8_t* obj;
    const uint16_t* cgram;
    const uint32_t* rgbaCache;
    const uint32_t* rgbaTable;
    uint32_t* out;
    uint8_t* pipeMain;
    uint32_t fixed;
};
using JitKernel = void (*)(JitCtx*);
}
void PPU::setJitScanline(bool enabled) {
    if (jit_scanline_enabled_ == enabled) return;
    jit_scanline_enabled_ = enabled;
    jit_line_ = -1;
    if (enabled && !jit_) jit_ = std::make_unique<JitState>();
}
void* PPU::jitKernel(uint8_t mode, uint8_t bgEnable, bool objEnabled, uint8_t mathMask, bool subtract, bool half) {
    const uint32_t key = static_cast<uint32_t>(mode) | (static_cast<uint32_t>(bgEnable) << 4) |
        (objEnabled ? 0x100u : 0u) | (static_cast<uint32_t>(mathMask) << 9) |
        (subtract ? 0x10000u : 0u) | (half ? 0x20000u : 0u);
    auto found = jit_->kernels.find(key);
    if (found != jit_->kernels.end()) return found->second;
    using namespace asmjit;
    CodeHolder code;
    code.init(jit_->runtime.environment(), jit_->runtime.cpu_features());
    x86::Assembler a(&code);
    const x86::Gp ctx = x86::rsi;
    const x86::Gp xi = x86::rdi;
    const x86::Gp pri = x86::r8d;
    const x86::Gp idx = x86::r9d;
    const x86::Gp lay = x86::r10d;
    a.push(x86::rbx);
    a.push(x86::rsi);
    a.push(x86::rdi);
    a.push(x86::r12);
    a.push(x86::r13);
    a.mov(ctx, x86::rcx);
    a.xor_(x86::edi, x86::edi);
    Label loop = a.new_label();
    a.bind(loop);
    a.xor_(pri, pri);
    a.xor_(idx, idx);
    a.mov(lay, 5);
    const int firstBg = mode == 1 ? 2 : 3;
    for (int bg = firstBg; bg >= 0; --bg) {
        if ((bgEnable & (1u << bg)) == 0) continue;
        Label skip = a.new_label();
        a.mov(x86::rax, x86::qword_ptr(ctx, static_cast<int32_t>(offsetof(JitCtx, bgIdx) + bg * sizeof(void*))));
        a.movzx(x86::edx, x86::byte_ptr(x86::rax, xi));
        a.cmp(x86::edx, 0xFF);
        a.je(skip);
        a.mov(x86::rax, x86::qword_ptr(ctx, static_cast<int32_t>(offsetof(JitCtx, bgPri) + bg * sizeof(void*))));
        a.movzx(x86::ecx, x86::byte_ptr(x86::rax, xi));
        a.cmp(x86::ecx, pri);
        a.jb(skip);
        a.mov(pri, x86::ecx);
        a.mov(idx, x86::edx);
        a.mov(lay, bg);
        a.bind(skip);
    }
    if (objEnabled) {
        Label skip = a.new_label();
        a.mov(x86::rax, x86::qword_ptr(ctx, static_cast<int32_t>(offsetof(JitCtx, obj))));
        a.movzx(x86::ecx, x86::byte_ptr(x86::rax, xi, 3, static_cast<int32_t>(offsetof(Pixel, transparent))));
        a.test(x86::ecx, x86::ecx);
        a.jnz(skip);
        a.movzx(x86::ecx, x86::byte_ptr(x86::rax, xi, 3, static_cast<int32_t>(offsetof(Pixel, priority))));
        a.cmp(x86::ecx, pri);
        a.jb(skip);
        a.mov(pri, x86::ecx);
        a.movzx(idx, x86::byte_ptr(x86::rax, xi, 3, static_cast<int32_t>(offsetof(Pixel, cgramIndex))));
        a.movzx(x86::edx, x86::byte_ptr(x86::rax, xi, 3, static_cast<int32_t>(offsetof(Pixel, objPalette))));
        a.mov(lay, 4);
        a.mov(x86::ecx, 6);
        a.cmp(x86::edx, 4);
        a.cmovb(lay, x86::ecx);
        a.bind(skip);
    }
    a.mov(x86::rax, x86::qword_ptr(ctx, static_cast<int32_t>(offsetof(JitCtx, pipeMain))));
    a.mov(x86::byte_ptr(x86::rax, xi), x86::r9b);
    Label noMath = a.new_label();
    Label store = a.new_label();
    if (mathMask != 0) {
        a.mov(x86::eax, static_cast<uint32_t>(mathMask));
        a.bt(x86::eax, lay);
        a.jnc(noMath);
        a.mov(x86::rcx, x86::qword_ptr(ctx, static_cast<int32_t>(offsetof(JitCtx, cgram))));
        a.mov(x86::edx, idx);
        a.movzx(x86::eax, x86::word_ptr(x86::rcx, x86::rdx, 1));
        a.and_(x86::eax, 0x7FFF);
        a.mov(x86::edx, x86::dword_ptr(ctx, static_cast<int32_t>(offsetof(JitCtx, fixed))));
        a.and_(x86::edx, 0x7FFF);
        a.mov(x86::r11d, x86::eax);
        a.xor_(x86::r11d, x86::edx);
        a.mov(x86::ecx, x86::eax);
        if (!subtract) {
            a.and_(x86::r11d, 0x0421);
            a.add(x86::ecx, x86::edx);
            if (half) {
                a.sub(x86::ecx, x86::r11d);
                a.shr(x86::ecx, 1);
            } else {
                a.mov(x86::ebx, x86::ecx);
                a.sub(x86::ebx, x86::r11d);
                a.and_(x86::ebx, 0x8420);
                a.mov(x86::r12d, x86::ebx);
                a.shr(x86::r12d, 5);
                a.mov(x86::r13d, x86::ebx);
                a.sub(x86::r13d, x86::r12d);
                a.sub(x86::ecx, x86::ebx);
                a.or_(x86::ecx, x86::r13d);
                a.and_(x86::ecx, 0x7FFF);
            }
        } else {
            a.and_(x86::r11d, 0x8420);
            a.sub(x86::ecx, x86::edx);
            a.add(x86::ecx, 0x8420);
            a.mov(x86::ebx, x86::ecx);
            a.sub(x86::ebx, x86::r11d);
            a.and_(x86::ebx, 0x8420);
            a.mov(x86::r12d, x86::ebx);
            a.shr(x86::r12d, 5);
            a.mov(x86::r13d, x86::ebx);
            a.sub(x86::r13d, x86::r12d);
            a.sub(x86::ecx, x86::ebx);
            a.and_(x86::ecx, x86::r13d);
            if (half) {
                a.and_(x86::ecx, 0x7BDE);
                a.shr(x86::ecx, 1);
            }
            a.and_(x86::ecx, 0x7FFF);
        }
        a.mov(x86::rax, x86::qword_ptr(ctx, static_cast<int32_t>(offsetof(JitCtx, rgbaTable))));
        a.mov(x86::eax, x86::dword_ptr(x86::rax, x86::rcx, 2));
        a.jmp(store);
    }
    a.bind(noMath);
    a.mov(x86::rax, x86::qword_ptr(ctx, static_cast<int32_t>(offsetof(JitCtx, rgbaCache))));
    a.mov(x86::edx, idx);
    a.mov(x86::eax, x86::dword_ptr(x86::rax, x86::rdx, 2));
    a.bind(store);
    a.mov(x86::rdx, x86::qword_ptr(ctx, static_cast<int32_t>(offsetof(JitCtx, out))));
    a.mov(x86::dword_ptr(x86::rdx, xi, 2), x86::eax);
    a.inc(x86::edi);
    a.cmp(x86::edi, 256);
    a.jb(loop);
    a.pop(x86::r13);
    a.pop(x86::r12);
    a.pop(x86::rdi);
    a.pop(x86::rsi);
    a.pop(x86::rbx);
    a.ret();
    JitKernel kernel = nullptr;
    if (jit_->runtime.add(&kernel, &code) != Error::kOk) kernel = nullptr;
    jit_->kernels.emplace(key, reinterpret_cast<void*>(kernel));
    return reinterpret_cast<void*>(kernel);
}
bool PPU::buildJitLine(int line, uint8_t brightness, size_t rowBase) {
    const uint8_t mode = bgmode_ & 7;
    if (mode != 0 && mode != 1) return false;
    if (ts_ != 0 || (tmw_ & 0x0F) != 0 || (cgwsel_ & 0xC0) != 0 || (wobjsel_ & 0xA0) != 0) return false;
    if ((mosaic_ & 0x0F) != 0 || hiresOutputEnabled() || frame_hires_output_) return false;
    const uint8_t bgMask = mode == 1 ? 0x07 : 0x0F;
    const uint8_t bgEnable = static_cast<uint8_t>(tm_ & bgMask);
    const bool objEnabled = (tm_ & 0x10) != 0;
    static constexpr uint8_t kAllowedRegionMask[4] = { 0x3, 0x2, 0x1, 0x0 };
    const uint8_t regionMode = static_cast<uint8_t>((cgwsel_ >> 4) & 3);
    uint8_t mathMask = 0;
    if ((kAllowedRegionMask[regionMode] & 1u) != 0) mathMask = static_cast<uint8_t>(cgadsub_ & 0x3F);
    const bool useSubscreen = (cgwsel_ & 0x02) != 0;
    const bool subtract = (cgadsub_ & 0x80) != 0;
    const bool half = (cgadsub_ & 0x40) != 0 && !useSubscreen;
    JitKernel kernel = reinterpret_cast<JitKernel>(jitKernel(mode, bgEnable, objEnabled, mathMask, subtract, half));
    if (kernel == nullptr) return false;
    JitCtx ctx{};
    for (int bg = 0; bg < 4; ++bg) {
        if ((bgEnable & (1u << bg)) != 0)
            fillBackgroundLine(bg, line, mode, jit_->index[bg], jit_->priority[bg], jit_->palette[bg]);
        else
            jit_->index[bg].fill(kTransparent);
        ctx.bgIdx[bg] = jit_->index[bg].data();
        ctx.bgPri[bg] = jit_->priority[bg].data();
    }
    ctx.obj = reinterpret_cast<const uint8_t*>(line_obj_main_.data());
    ctx.cgram = cgram_.data();
    ctx.rgbaCache = cgram_rgba_cache_[brightness].data();
    ctx.rgbaTable = kBgr555Rgba.data() + static_cast<size_t>(std::min<uint8_t>(brightness, 15)) * 32768u;
    ctx.out = framebuffer_.data() + rowBase;
    ctx.pipeMain = pipeline_main_cgram_.data();
    ctx.fixed = fixed_color_;
    kernel(&ctx);
    pipeline_sub_cgram_.fill(0);
    return true;
}

#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <vector>
class Bus;
class PPU {
public:
    PPU();
    ~PPU();
    void attachBus(Bus* bus) { bus_ = bus; }
    void setSpriteLimitRemoved(bool removed) { sprite_limit_removed_ = removed; }
    bool spriteLimitRemoved() const { return sprite_limit_removed_; }
    static constexpr int WIDESCREEN_PAD = 72;
    static constexpr int MAX_HD_SCALE = 4;
    void setMode7Widescreen(bool enabled);
    bool mode7Widescreen() const { return mode7_widescreen_; }
    void setMode7HdScale(int scale);
    int mode7HdScale() const { return mode7_hd_scale_; }
    void setJitScanline(bool enabled);
    bool jitScanline() const { return jit_scanline_enabled_; }
    void setFastScanline(bool enabled);
    bool fastScanline() const { return fast_scanline_enabled_; }
    bool presentationActive() const { return present_valid_; }
    int logicalWidth() const { return present_valid_ ? 256 + 2 * present_pad_ : 256; }
    int logicalHeight() const { return nativeHeight(); }
    int nativeHeight() const { return overscanEnabled() ? 239 : 224; }
    enum class Ppu2Revision : uint8_t {
        PPU2 = 0,
        PPU2A = 1,
        PPU2B = 2,
        PPU2C = 3,
        OneChip = 4,
    };
    void power();
    void reset();
    void tick();
    void latchCounters();
    uint8_t readRegister(uint16_t address);
    void writeRegister(uint16_t address, uint8_t value, int previousCpuBus = -1);
    const uint32_t* framebuffer() const { return present_valid_ ? present_output_.data() : framebuffer_.data(); }
    int width() const { return present_valid_ ? present_width_ : ((frame_hires_output_ || hiresOutputEnabled()) ? 512 : 256); }
    int framebufferStride() const { return present_valid_ ? present_width_ : 512; }
    int height() const { return present_valid_ ? present_height_ : nativeHeight(); }
    bool hiresOutputEnabled() const {
        const uint8_t mode = bgmode_ & 0x07;
        return (setini_ & 0x08) != 0 || mode == 5 || mode == 6;
    }
    bool pseudoHiresOutputEnabled() const {
        const uint8_t mode = bgmode_ & 0x07;
        return frame_hires_output_ && mode != 5 && mode != 6;
    }
    int scanline() const { return scanline_; }
    int dot() const { return dot_; }
    int dotsThisLine() const;
    int masterClocksThisDot() const;
    int masterClocksForLine(int line, bool oddField) const;
    int masterClockPhaseInDot() const;
    bool hblank() const {
        return dot_ >= HBLANK_START_DOT || dot_ == 0;
    }
    bool vblank() const { return scanline_ >= vblankStartLine(); }
    int vblankStartLine() const { return overscanEnabled() ? 240 : 225; }
    int linesThisFrame() const {
        const int base = pal_region_ ? 312 : 262;
        return base + ((interlaceEnabled() && ((frame_counter_ & 1u) != 0)) ? 1 : 0);
    }
    uint64_t frameCounter() const { return frame_counter_; }
    uint16_t latchedHCounter() const { return latched_hcounter_; }
    uint8_t bgMode() const { return bgmode_ & 0x07; }
    uint16_t mode7HOffsetRaw() const { return m7_hofs_; }
    uint16_t mode7VOffsetRaw() const { return m7_vofs_; }
    bool forcedBlank() const { return (inidisp_ & 0x80) != 0; }
    bool interlaceEnabled() const { return (setini_ & 0x01) != 0; }
    bool overscanEnabled() const { return (setini_ & 0x04) != 0; }
    bool palRegion() const { return pal_region_; }
    void setPalRegion(bool pal) { pal_region_ = pal; }
    void setPpu2Revision(Ppu2Revision revision) { ppu2_revision_ = revision; }
    uint8_t ppu1Version() const { return 1; }
    uint8_t ppu2Version() const {
        switch (ppu2_revision_) {
        case Ppu2Revision::PPU2: return 1;
        case Ppu2Revision::PPU2A: return 2;
        case Ppu2Revision::PPU2B:
        case Ppu2Revision::PPU2C:
        case Ppu2Revision::OneChip: return 3;
        }
        return 3;
    }
    bool oneChipPpu() const { return ppu2_revision_ == Ppu2Revision::OneChip; }
    uint16_t vramWordAddress() const { return vmadd_; }
    uint8_t vmain() const { return vmain_; }
    uint16_t cgramColor(uint8_t index) const { return cgram_[index] & 0x7FFF; }
    uint8_t vramByte(uint16_t address) const { return vram_[address]; }
    uint8_t oamByte(uint16_t address) const { return oam_[address % oam_.size()]; }
    uint8_t brightness() const { return inidisp_ & 0x0F; }
    uint8_t mainScreenMask() const { return tm_; }
    uint8_t subScreenMask() const { return ts_; }
    uint8_t bgScreenConfig(int bg) const { return bgsc_[bg & 3]; }
    uint16_t bgHOffset(int bg) const { return bg_hofs_[bg & 3]; }
    uint16_t bgVOffset(int bg) const { return bg_vofs_[bg & 3]; }
    bool objRangeOver() const { return obj_range_over_; }
    bool objTimeOver() const { return obj_time_over_; }
    bool objPriorityRotation() const { return obj_priority_rotation_; }
    uint8_t firstSpriteIndex() const;
    uint16_t internalOamByteAddress() const;
    uint16_t activeOamByteAddress() const;
    static constexpr int DOTS_PER_LINE = 340;
    static constexpr int LONG_DOTS_PER_LINE = 341;
    static constexpr int LINES_PER_FRAME = 262;
    static constexpr int VISIBLE_DOT_START = 22;
    static constexpr int VISIBLE_DOT_END = 278;
    static constexpr int OBJ_RANGE_END_DOT = 256;
    static constexpr int OBJ_FETCH_START_DOT = 272;
    static constexpr int HBLANK_START_DOT = 274;
    static constexpr int HDMA_INIT_DOT = 6;
    static constexpr int HDMA_START_DOT = 276;
    static constexpr int OAM_RESET_DOT = 10;
    static constexpr int VBLANK_START_LINE = 225;
    static int screenXFromDot(int dot) {
        if (dot < VISIBLE_DOT_START || dot >= VISIBLE_DOT_END) return -1;
        return dot - VISIBLE_DOT_START;
    }
    friend class SNES;
private:
    struct Pixel {
        uint16_t color = 0;
        uint8_t priority = 0;
        uint8_t layer = 5;
        uint8_t objPalette = 0;
        uint8_t cgramIndex = 0;
        bool transparent = true;
    };
    static uint32_t rgbaFromBgr555(uint16_t color, uint8_t brightness);
    static uint16_t addColors(uint16_t a, uint16_t b, bool subtract, bool half);
    uint16_t remapVramAddress(uint16_t wordAddress) const;
    uint16_t vramIncrement() const;
    void incrementVramAddress(bool highPort);
    uint16_t readVramWord(uint16_t wordAddress) const;
    uint16_t tilemapEntry(int bg, int tileX, int tileY) const;
    uint8_t backgroundPixel(int bg, int x, int y, uint8_t& priority, uint8_t& palette, bool subScreen = false) const;
    uint8_t backgroundPixelMode1(int bg, int x, int y, uint8_t& priority) const;
    uint8_t tilePixel(uint32_t tileBase, uint16_t tileNumber, int bpp, int x, int y) const;
    void decodeTileRow(uint32_t tileBase, uint16_t tileNumber, int bpp, int y, std::array<uint8_t, 8>& out) const;
    Pixel mode7Pixel(int x, int y, bool subScreen) const;
    Pixel screenPixel(int x, int y, bool subScreen) const;
    void screenPixelPair(int x, int y, Pixel& main, Pixel& sub) const;
    void screenPixelPairMode1(int x, int y, bool win1, bool win2, Pixel& main, Pixel& sub) const;
    void renderScanline(int line);
    void fillBackgroundLine(int bg, int line, uint8_t mode, std::array<uint8_t, 256>& index,
                            std::array<uint8_t, 256>& priority, std::array<uint8_t, 256>& palette) const;
    bool buildFastLine(int line);
    struct JitState;
    void* jitKernel(uint8_t mode, uint8_t bgEnable, bool objEnabled, uint8_t mathMask, bool subtract, bool half);
    bool buildJitLine(int line, uint8_t brightness, size_t rowBase);
    void reconfigurePresentation();
    void composePresentation();
    void presentationLineHook(int x, int line, uint8_t inidisp);
    void presentPad(int logicalX, int line, uint8_t inidisp);
    void presentMode7Block(int logicalX, int line, const Pixel& main, const Pixel& sub, bool subBackdrop,
                           bool insideColorWindow, uint8_t brightness, bool hdEligible, int sampleX);
    bool mode7HdRaw(int logicalX, int line, int subX, int subY, uint8_t& raw) const;
    void renderPixel(int x, int line);
    void ensureLineObjectLayers(int line);
    void renderObjects(int line, bool subScreen, std::array<Pixel, 256>& pixels) const;
    void renderObjectPair(int line, std::array<Pixel, 256>& mainPixels, std::array<Pixel, 256>& subPixels);
    struct ObjectDimensions { int width; int height; };
    ObjectDimensions objectDimensions(bool large) const;
    struct EvaluatedObject {
        int sprite = 0;
        int x = 0;
        int localY = 0;
        ObjectDimensions dims{};
    };
    struct FetchedObjectSliver {
        std::array<uint8_t, 8> pixels{};
        uint8_t palette = 0;
        uint8_t priority = 0;
        bool valid = false;
    };
    bool objectOnScanline(int sprite, int line, int& x, int& localY, ObjectDimensions& dims) const;
    void evaluateObjectLimits(int line);
    void beginObjectEvaluation(int line);
    void stepObjectRangeEvaluation();
    void stepObjectSliverEvaluation();
    void publishObjectEvaluation();
    void invalidateObjectEvaluation() { ++obj_eval_generation_; obj_eval_cache_line_ = -1; }
    void invalidateMode1Rows() { ++mode1_row_generation_; }
    uint8_t activeCgramAddress() const;
    bool activeCgramPreferSub() const;
    bool windowResult(int layer, int x) const;
    bool windowResultFromGeometry(int layer, bool win1, bool win2) const;
    bool colorWindowResultFromGeometry(bool win1, bool win2) const;
    bool layerWindowMasked(int layer, int x, bool subScreen) const;
    bool colorWindowResult(int x) const;
    bool colorMathAllowed(const Pixel& main, bool insideColorWindow) const;
    uint16_t directColor(uint8_t palette, uint8_t pixel) const;
    void writeMode7Word(uint16_t& target, uint8_t value);
    bool cpuCanAccessVram() const;
    bool cpuCanAccessCgram() const;
    bool cpuCanAccessOam() const;
    void incrementInternalOamAddress();
    Bus* bus_ = nullptr;
    std::array<uint32_t, 512 * 239> framebuffer_{};
    bool jit_scanline_enabled_ = false;
    int jit_line_ = -1;
    uint64_t jit_gen_ = 0;
    std::unique_ptr<JitState> jit_;
    bool fast_scanline_enabled_ = false;
    uint64_t reg_generation_ = 1;
    int fast_line_ = -1;
    uint64_t fast_gen_ = 0;
    std::array<Pixel, 256> fast_main_{};
    std::array<Pixel, 256> fast_sub_{};
    bool mode7_widescreen_ = false;
    int mode7_hd_scale_ = 1;
    bool present_valid_ = false;
    bool frame_present_ = false;
    int frame_pad_ = 0;
    int frame_scale_ = 1;
    int present_pad_ = 0;
    int present_width_ = 256;
    int present_height_ = 224;
    std::vector<uint32_t> present_overlay_;
    std::vector<uint32_t> present_output_;
    bool frame_hires_output_ = false;
    bool frame_overscan_output_ = false;
    std::array<uint8_t, 64 * 1024> vram_{};
    std::array<uint16_t, 256> cgram_{};
    std::array<uint8_t, 544> oam_{};
    std::array<uint8_t, 0x40> registers_{};
    uint8_t inidisp_ = 0x80;
    uint8_t objsel_ = 0;
    uint8_t bgmode_ = 0;
    uint8_t mosaic_ = 0;
    int mosaic_vcounter_ = 0;
    int mosaicVOffset() const { return ((mosaic_ >> 4) & 0x0F) + 1 - mosaic_vcounter_; }
    void mosaicScanline();
    std::array<uint8_t, 4> bgsc_{};
    uint8_t bg12nba_ = 0;
    uint8_t bg34nba_ = 0;
    std::array<uint16_t, 4> bg_hofs_{};
    std::array<uint16_t, 4> bg_vofs_{};
    struct Mode1RowCache {
        uint64_t generation = 0;
        uint32_t rowKey = 0xFFFFFFFFu;
        uint8_t palette = 0;
        uint8_t priority = 0;
        std::array<uint8_t, 16> pixels{};
    };
    struct BgRowCache {
        uint64_t generation = 0;
        int tileX = -1;
        int tileY = -1;
        int localY = -1;
        int tileSizeX = 8;
        int tileSizeY = 8;
        uint8_t palette = 0;
        uint8_t priority = 0;
        std::array<uint8_t, 16> pixels{};
    };
    uint64_t mode1_row_generation_ = 1;
    mutable std::array<Mode1RowCache, 3> mode1_row_cache_{};
    mutable std::array<BgRowCache, 4> bg_row_cache_{};
    struct OptOffsetCache {
        uint64_t generation = 0;
        int tileX = -1;
        uint16_t h = 0;
        uint16_t v = 0;
        bool hasV = false;
    };
    mutable OptOffsetCache opt_cache_{};
    uint8_t scroll_latch_ = 0;
    uint8_t bghofs_latch_ = 0;
    uint8_t m7sel_ = 0;
    uint16_t m7a_ = 0;
    uint16_t m7b_ = 0;
    uint16_t m7c_ = 0;
    uint16_t m7d_ = 0;
    uint16_t m7x_ = 0;
    uint16_t m7y_ = 0;
    uint16_t m7_hofs_ = 0;
    uint16_t m7_vofs_ = 0;
    uint8_t m7_latch_ = 0;
    uint8_t w12sel_ = 0;
    uint8_t w34sel_ = 0;
    uint8_t wobjsel_ = 0;
    uint8_t wh0_ = 0;
    uint8_t wh1_ = 0;
    uint8_t wh2_ = 0;
    uint8_t wh3_ = 0;
    uint8_t wbglog_ = 0;
    uint8_t wobjlog_ = 0;
    uint8_t tm_ = 0;
    uint8_t ts_ = 0;
    uint8_t tmw_ = 0;
    uint8_t tsw_ = 0;
    uint8_t cgwsel_ = 0;
    uint8_t cgadsub_ = 0;
    uint16_t fixed_color_ = 0;
    uint8_t setini_ = 0;
    bool pal_region_ = false;
    Ppu2Revision ppu2_revision_ = Ppu2Revision::PPU2B;
    uint8_t ppu1_open_bus_ = 0;
    uint8_t ppu2_open_bus_ = 0;
    bool inidisp_early_pending_ = false;
    uint8_t inidisp_early_value_ = 0;
    bool setini_early_pending_ = false;
    uint8_t setini_early_value_ = 0;
    uint8_t vmain_ = 0;
    uint16_t vmadd_ = 0;
    uint16_t vram_read_latch_ = 0;
    uint16_t oam_word_address_ = 0;
    uint16_t oam_reload_address_ = 0;
    bool oam_high_next_ = false;
    uint8_t oam_write_latch_ = 0;
    bool obj_priority_rotation_ = false;
    bool obj_range_over_ = false;
    bool obj_time_over_ = false;
    bool sprite_limit_removed_ = false;
    int obj_eval_target_line_ = -1;
    uint64_t obj_eval_generation_ = 1;
    uint64_t obj_eval_cache_generation_ = 0;
    int obj_eval_cache_line_ = -1;
    int obj_eval_cache_count_ = 0;
    std::array<EvaluatedObject, 32> obj_eval_cache_{};
    std::array<uint8_t, 32> obj_eval_sliver_mask_{};
    std::array<std::array<FetchedObjectSliver, 8>, 32> obj_eval_fetched_{};
    int obj_ready_line_ = -1;
    int obj_ready_count_ = 0;
    std::array<EvaluatedObject, 32> obj_ready_cache_{};
    std::array<uint8_t, 32> obj_ready_sliver_mask_{};
    std::array<std::array<FetchedObjectSliver, 8>, 32> obj_ready_fetched_{};
    bool obj_eval_active_ = false;
    int obj_eval_scan_index_ = 0;
    uint8_t obj_eval_oam_index_ = 0;
    int obj_eval_selected_count_ = 0;
    bool obj_sliver_eval_active_ = false;
    int obj_sliver_eval_index_ = -1;
    int obj_sliver_eval_col_ = 0;
    int obj_slivers_fetched_ = 0;
    uint8_t cgadd_ = 0;
    bool cgram_high_next_ = false;
    uint8_t cgram_write_latch_ = 0;
    bool cgram_read_high_next_ = false;
    int pipeline_cgram_line_ = -1;
    std::array<uint8_t, 256> pipeline_main_cgram_{};
    std::array<uint8_t, 256> pipeline_sub_cgram_{};
    int hires_dac_line_ = -1;
    uint16_t hires_prev_main_color_ = 0;
    bool hires_prev_main_color_enable_ = false;
    bool hires_prev_math_enable_ = false;
    bool hires_prev_blend_mode_ = false;
    bool hires_prev_color_halve_ = false;
    std::array<std::array<uint32_t, 256>, 16> cgram_rgba_cache_{};
    int line_obj_line_ = -1;
    std::array<Pixel, 256> line_obj_main_{};
    std::array<Pixel, 256> line_obj_sub_{};
    int scanline_ = 0;
    int dot_ = 0;
    uint64_t dot_start_master_clock_ = 0;
    uint64_t frame_counter_ = 0;
    bool stat78_field_ = false;
    uint16_t latched_hcounter_ = 0;
    uint16_t latched_vcounter_ = 0;
    bool counters_latched_ = false;
    bool hcounter_high_next_ = false;
    bool vcounter_high_next_ = false;
};

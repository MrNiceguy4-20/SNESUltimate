#pragma once
#ifdef _WIN32
#include <SDL3/SDL.h>
#include "controller.hpp"
#include <array>
#include <filesystem>
namespace Frontend {
enum class GraphicsFilter : int {
    Nearest = 0,
    Linear = 1,
    SharpBilinear = 2,
    Bicubic = 3,
};
enum class AspectRatio : int {
    Corrected4x3 = 0,
    Snes8x7 = 1,
    Stretch = 2,
};
struct GraphicsSettings {
    GraphicsFilter filter = GraphicsFilter::Linear;
    AspectRatio aspect = AspectRatio::Corrected4x3;
    bool integer_scaling = false;
    bool vsync = true;
    bool pseudo_hires_blend = true;
    bool scanlines = false;
    float scanline_strength = 0.16f;
    float sharpening = 0.12f;
    float crt_mask_strength = 0.0f;
    float gamma = 1.0f;
    float saturation = 1.0f;
    float contrast = 1.0f;
    float brightness = 0.0f;
    float curvature = 0.0f;
    float vignette = 0.0f;
    int crt_mask_style = 0;
    float overscan = 0.0f;
    float temperature = 0.0f;
    float composite_blur = 0.0f;
    int color_profile = 0;
    bool remove_sprite_limit = false;
    bool mode7_widescreen = false;
    bool hd_mode7 = false;
    int hd_mode7_scale = 2;
};
struct ExperimentalSettings {
    bool fast_scanline_ppu = false;
    bool jit_ppu = false;
};
struct AudioSettings {
    float master_volume = 1.0f;
    bool muted = false;
};
struct GeneralSettings {
    bool pause_when_unfocused = true;
    bool show_fps = false;
};
struct ControlSettings {
    std::array<std::array<SDL_Scancode, Controller::ButtonCount>, 2> keyboard{};
    void resetPlayer(int playerIndex);
    void resetAll();
};
struct Settings {
    GraphicsSettings graphics{};
    ExperimentalSettings experimental{};
    AudioSettings audio{};
    GeneralSettings general{};
    ControlSettings controls{};
    Settings();
    void resetAll();
    bool load(const std::filesystem::path& path);
    bool save(const std::filesystem::path& path) const;
};
const char* controllerButtonName(int button);
const char* scancodeName(SDL_Scancode scancode);
}
#endif

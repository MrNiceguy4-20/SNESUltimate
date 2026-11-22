#ifdef _WIN32
#include "frontend_settings.hpp"
#include <algorithm>
#include <charconv>
#include <cctype>
#include <fstream>
#include <string>
#include <string_view>
namespace Frontend {
namespace {
constexpr std::array<const char*, Controller::ButtonCount> kButtonNames = {
    "B", "Y", "Select", "Start", "Up", "Down", "Left", "Right", "A", "X", "L", "R"
};
constexpr std::array<SDL_Scancode, Controller::ButtonCount> kPlayer1Defaults = {
    SDL_SCANCODE_Z,
    SDL_SCANCODE_A,
    SDL_SCANCODE_RSHIFT,
    SDL_SCANCODE_RETURN,
    SDL_SCANCODE_UP,
    SDL_SCANCODE_DOWN,
    SDL_SCANCODE_LEFT,
    SDL_SCANCODE_RIGHT,
    SDL_SCANCODE_X,
    SDL_SCANCODE_S,
    SDL_SCANCODE_Q,
    SDL_SCANCODE_W,
};
constexpr std::array<SDL_Scancode, Controller::ButtonCount> kPlayer2Defaults = {
    SDL_SCANCODE_KP_1,
    SDL_SCANCODE_KP_4,
    SDL_SCANCODE_KP_0,
    SDL_SCANCODE_KP_ENTER,
    SDL_SCANCODE_KP_8,
    SDL_SCANCODE_KP_2,
    SDL_SCANCODE_KP_4,
    SDL_SCANCODE_KP_6,
    SDL_SCANCODE_KP_2,
    SDL_SCANCODE_KP_5,
    SDL_SCANCODE_KP_7,
    SDL_SCANCODE_KP_9,
};
constexpr std::array<SDL_Scancode, Controller::ButtonCount> kPlayer2DefaultsUnique = {
    SDL_SCANCODE_KP_1,
    SDL_SCANCODE_KP_3,
    SDL_SCANCODE_KP_0,
    SDL_SCANCODE_KP_ENTER,
    SDL_SCANCODE_KP_8,
    SDL_SCANCODE_KP_2,
    SDL_SCANCODE_KP_4,
    SDL_SCANCODE_KP_6,
    SDL_SCANCODE_KP_5,
    SDL_SCANCODE_KP_PERIOD,
    SDL_SCANCODE_KP_7,
    SDL_SCANCODE_KP_9,
};
std::string trim(std::string value)
{
    auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}
bool parseInt(std::string_view text, int& value)
{
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value);
    return result.ec == std::errc{} && result.ptr == end;
}
bool parseFloat(std::string_view text, float& value)
{
    try {
        std::size_t used = 0;
        const std::string owned(text);
        value = std::stof(owned, &used);
        return used == owned.size();
    } catch (...) {
        return false;
    }
}
bool parseBool(std::string_view text, bool& value)
{
    if (text == "1" || text == "true" || text == "on") {
        value = true;
        return true;
    }
    if (text == "0" || text == "false" || text == "off") {
        value = false;
        return true;
    }
    return false;
}
}
void ControlSettings::resetPlayer(int playerIndex)
{
    if (playerIndex == 0) keyboard[0] = kPlayer1Defaults;
    else if (playerIndex == 1) keyboard[1] = kPlayer2DefaultsUnique;
}
void ControlSettings::resetAll()
{
    resetPlayer(0);
    resetPlayer(1);
}
Settings::Settings()
{
    controls.resetAll();
}
void Settings::resetAll()
{
    graphics = {};
    experimental = {};
    audio = {};
    general = {};
    controls.resetAll();
}
bool Settings::load(const std::filesystem::path& path)
{
    std::ifstream in(path);
    if (!in) return false;
    Settings loaded;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::string key = trim(line.substr(0, equals));
        const std::string value = trim(line.substr(equals + 1));
        int intValue = 0;
        float floatValue = 0.0f;
        bool boolValue = false;
        if (key == "graphics.filter" && parseInt(value, intValue) && intValue >= 0 && intValue <= 3) {
            loaded.graphics.filter = static_cast<GraphicsFilter>(intValue);
        } else if (key == "graphics.aspect" && parseInt(value, intValue) && intValue >= 0 && intValue <= 2) {
            loaded.graphics.aspect = static_cast<AspectRatio>(intValue);
        } else if (key == "graphics.integer_scaling" && parseBool(value, boolValue)) {
            loaded.graphics.integer_scaling = boolValue;
        } else if (key == "graphics.vsync" && parseBool(value, boolValue)) {
            loaded.graphics.vsync = boolValue;
        } else if (key == "graphics.pseudo_hires_blend" && parseBool(value, boolValue)) {
            loaded.graphics.pseudo_hires_blend = boolValue;
        } else if (key == "graphics.scanlines" && parseBool(value, boolValue)) {
            loaded.graphics.scanlines = boolValue;
        } else if (key == "graphics.scanline_strength" && parseFloat(value, floatValue)) {
            loaded.graphics.scanline_strength = std::clamp(floatValue, 0.0f, 0.60f);
        } else if (key == "graphics.sharpening" && parseFloat(value, floatValue)) {
            loaded.graphics.sharpening = std::clamp(floatValue, 0.0f, 1.0f);
        } else if (key == "graphics.crt_mask_strength" && parseFloat(value, floatValue)) {
            loaded.graphics.crt_mask_strength = std::clamp(floatValue, 0.0f, 0.70f);
        } else if (key == "graphics.gamma" && parseFloat(value, floatValue)) {
            loaded.graphics.gamma = std::clamp(floatValue, 0.5f, 2.5f);
        } else if (key == "graphics.saturation" && parseFloat(value, floatValue)) {
            loaded.graphics.saturation = std::clamp(floatValue, 0.0f, 2.0f);
        } else if (key == "graphics.contrast" && parseFloat(value, floatValue)) {
            loaded.graphics.contrast = std::clamp(floatValue, 0.0f, 2.0f);
        } else if (key == "graphics.brightness" && parseFloat(value, floatValue)) {
            loaded.graphics.brightness = std::clamp(floatValue, -1.0f, 1.0f);
        } else if (key == "graphics.curvature" && parseFloat(value, floatValue)) {
            loaded.graphics.curvature = std::clamp(floatValue, 0.0f, 0.35f);
        } else if (key == "graphics.vignette" && parseFloat(value, floatValue)) {
            loaded.graphics.vignette = std::clamp(floatValue, 0.0f, 1.0f);
        } else if (key == "graphics.crt_mask_style" && parseInt(value, intValue)) {
            loaded.graphics.crt_mask_style = std::clamp(intValue, 0, 2);
        } else if (key == "graphics.overscan" && parseFloat(value, floatValue)) {
            loaded.graphics.overscan = std::clamp(floatValue, 0.0f, 0.12f);
        } else if (key == "graphics.temperature" && parseFloat(value, floatValue)) {
            loaded.graphics.temperature = std::clamp(floatValue, -1.0f, 1.0f);
        } else if (key == "graphics.composite_blur" && parseFloat(value, floatValue)) {
            loaded.graphics.composite_blur = std::clamp(floatValue, 0.0f, 1.0f);
        } else if (key == "graphics.color_profile" && parseInt(value, intValue)) {
            loaded.graphics.color_profile = std::clamp(intValue, 0, 3);
        } else if (key == "graphics.remove_sprite_limit" && parseBool(value, boolValue)) {
            loaded.graphics.remove_sprite_limit = boolValue;
        } else if (key == "graphics.mode7_widescreen" && parseBool(value, boolValue)) {
            loaded.graphics.mode7_widescreen = boolValue;
        } else if (key == "graphics.hd_mode7" && parseBool(value, boolValue)) {
            loaded.graphics.hd_mode7 = boolValue;
        } else if (key == "graphics.hd_mode7_scale" && parseInt(value, intValue)) {
            loaded.graphics.hd_mode7_scale = std::clamp(intValue, 2, 4);
        } else if (key == "experimental.fast_scanline_ppu" && parseBool(value, boolValue)) {
            loaded.experimental.fast_scanline_ppu = boolValue;
        } else if (key == "experimental.jit_ppu" && parseBool(value, boolValue)) {
            loaded.experimental.jit_ppu = boolValue;
        } else if (key == "audio.master_volume" && parseFloat(value, floatValue)) {
            loaded.audio.master_volume = std::clamp(floatValue, 0.0f, 1.0f);
        } else if (key == "audio.muted" && parseBool(value, boolValue)) {
            loaded.audio.muted = boolValue;
        } else if (key == "general.pause_when_unfocused" && parseBool(value, boolValue)) {
            loaded.general.pause_when_unfocused = boolValue;
        } else if (key == "general.show_fps" && parseBool(value, boolValue)) {
            loaded.general.show_fps = boolValue;
        } else {
            for (int player = 0; player < 2; ++player) {
                for (int button = 0; button < Controller::ButtonCount; ++button) {
                    const std::string bindingKey = "controls.p" + std::to_string(player + 1) + "." + kButtonNames[button];
                    if (key == bindingKey && parseInt(value, intValue)) {
                        if (intValue >= static_cast<int>(SDL_SCANCODE_UNKNOWN) &&
                            intValue < static_cast<int>(SDL_SCANCODE_COUNT)) {
                            loaded.controls.keyboard[player][button] = static_cast<SDL_Scancode>(intValue);
                        }
                    }
                }
            }
        }
    }
    *this = loaded;
    return true;
}
bool Settings::save(const std::filesystem::path& path) const
{
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out) return false;
    out << "# SNESUltimate frontend settings\n";
    out << "version=1\n";
    out << "graphics.filter=" << static_cast<int>(graphics.filter) << '\n';
    out << "graphics.aspect=" << static_cast<int>(graphics.aspect) << '\n';
    out << "graphics.integer_scaling=" << (graphics.integer_scaling ? 1 : 0) << '\n';
    out << "graphics.vsync=" << (graphics.vsync ? 1 : 0) << '\n';
    out << "graphics.pseudo_hires_blend=" << (graphics.pseudo_hires_blend ? 1 : 0) << '\n';
    out << "graphics.scanlines=" << (graphics.scanlines ? 1 : 0) << '\n';
    out << "graphics.scanline_strength=" << graphics.scanline_strength << '\n';
    out << "graphics.sharpening=" << graphics.sharpening << '\n';
    out << "graphics.crt_mask_strength=" << graphics.crt_mask_strength << '\n';
    out << "graphics.gamma=" << graphics.gamma << '\n';
    out << "graphics.saturation=" << graphics.saturation << '\n';
    out << "graphics.contrast=" << graphics.contrast << '\n';
    out << "graphics.brightness=" << graphics.brightness << '\n';
    out << "graphics.curvature=" << graphics.curvature << '\n';
    out << "graphics.vignette=" << graphics.vignette << '\n';
    out << "graphics.crt_mask_style=" << graphics.crt_mask_style << '\n';
    out << "graphics.overscan=" << graphics.overscan << '\n';
    out << "graphics.temperature=" << graphics.temperature << '\n';
    out << "graphics.composite_blur=" << graphics.composite_blur << '\n';
    out << "graphics.color_profile=" << graphics.color_profile << '\n';
    out << "graphics.remove_sprite_limit=" << (graphics.remove_sprite_limit ? 1 : 0) << '\n';
    out << "graphics.mode7_widescreen=" << (graphics.mode7_widescreen ? 1 : 0) << '\n';
    out << "graphics.hd_mode7=" << (graphics.hd_mode7 ? 1 : 0) << '\n';
    out << "graphics.hd_mode7_scale=" << graphics.hd_mode7_scale << '\n';
    out << "experimental.fast_scanline_ppu=" << (experimental.fast_scanline_ppu ? 1 : 0) << '\n';
    out << "experimental.jit_ppu=" << (experimental.jit_ppu ? 1 : 0) << '\n';
    out << "audio.master_volume=" << audio.master_volume << '\n';
    out << "audio.muted=" << (audio.muted ? 1 : 0) << '\n';
    out << "general.pause_when_unfocused=" << (general.pause_when_unfocused ? 1 : 0) << '\n';
    out << "general.show_fps=" << (general.show_fps ? 1 : 0) << '\n';
    for (int player = 0; player < 2; ++player) {
        for (int button = 0; button < Controller::ButtonCount; ++button) {
            out << "controls.p" << (player + 1) << '.' << kButtonNames[button] << '='
                << static_cast<int>(controls.keyboard[player][button]) << '\n';
        }
    }
    return static_cast<bool>(out);
}
const char* controllerButtonName(int button)
{
    if (button < 0 || button >= Controller::ButtonCount) return "Unknown";
    return kButtonNames[static_cast<std::size_t>(button)];
}
const char* scancodeName(SDL_Scancode scancode)
{
    if (scancode == SDL_SCANCODE_UNKNOWN) return "Unbound";
    const char* name = SDL_GetScancodeName(scancode);
    return (name && *name) ? name : "Unknown key";
}
}
#endif

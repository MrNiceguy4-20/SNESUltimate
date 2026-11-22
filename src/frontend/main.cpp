#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_vulkan.h"
#include "vulkan_renderer.hpp"
#include "sdl_fallback_renderer.hpp"
#include "renderer_backend.hpp"
#include "frontend_settings.hpp"
#include "snes.hpp"
#include "video_capture.hpp"
#include "cheats.hpp"
#include <stdio.h>
#include <string>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <array>
#include <fstream>
#include <cstdint>
#include <vector>
#include <windows.h>
#include <urlmon.h>
#pragma comment(lib, "urlmon.lib")
#include <commdlg.h>
#include <memory>
#include <filesystem>
#include <system_error>
#include <atomic>
#include <thread>
#include <mutex>
static uint64_t TraceFnv1a64(const uint8_t* data, std::size_t size)
{
    uint64_t hash = 1469598103934665603ull;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 1099511628211ull;
    }
    return hash;
}
static uint32_t DimPixel(uint32_t pixel, float factor)
{
    const uint32_t r = static_cast<uint32_t>(static_cast<float>(pixel & 0xFFu) * factor + 0.5f);
    const uint32_t g = static_cast<uint32_t>(static_cast<float>((pixel >> 8) & 0xFFu) * factor + 0.5f);
    const uint32_t b = static_cast<uint32_t>(static_cast<float>((pixel >> 16) & 0xFFu) * factor + 0.5f);
    return (std::min)(r, 255u) |
           ((std::min)(g, 255u) << 8) |
           ((std::min)(b, 255u) << 16) |
           (pixel & 0xFF000000u);
}
static const uint32_t* PresentationFramebuffer(
    const PPU& ppu, const Frontend::GraphicsSettings& graphics,
    int& width, int& height, int& stride)
{
    width = ppu.width();
    height = ppu.height();
    stride = ppu.framebufferStride();
    const uint32_t* source = ppu.framebuffer();
    const bool blendPseudoHires = graphics.pseudo_hires_blend && !ppu.presentationActive() &&
                                  ppu.pseudoHiresOutputEnabled() && width == 512;
    const int rowScale = ppu.presentationActive() ? (std::max)(1, height / (std::max)(1, ppu.logicalHeight())) : 1;
    const bool applyScanlines = graphics.scanlines && graphics.scanline_strength > 0.001f;
    if (!blendPseudoHires && !applyScanlines) return source;
    static std::vector<uint32_t> processed;
    processed.resize((std::max)(processed.size(), static_cast<std::size_t>(width) * static_cast<std::size_t>(height)));
    static uint64_t processed_frame = ~uint64_t{0};
    static int processed_width = 0;
    static int processed_height = 0;
    static bool processed_blend = false;
    static bool processed_scanlines = false;
    static float processed_scanline_strength = -1.0f;
    const uint64_t frame = ppu.frameCounter();
    if (frame == processed_frame && processed_width == width && processed_height == height &&
        processed_blend == blendPseudoHires && processed_scanlines == applyScanlines &&
        std::abs(processed_scanline_strength - graphics.scanline_strength) < 0.0001f) {
        stride = width;
        return processed.data();
    }
    const float darkFactor = 1.0f - std::clamp(graphics.scanline_strength, 0.0f, 0.60f);
    for (int y = 0; y < height; ++y) {
        const uint32_t* src = source + static_cast<std::ptrdiff_t>(y) * stride;
        uint32_t* dst = processed.data() + static_cast<std::ptrdiff_t>(y) * width;
        if (blendPseudoHires) {
            for (int x = 0; x < width; x += 2) {
                const uint32_t a = src[x];
                const uint32_t b = src[x + 1];
                const uint32_t r = (((a      ) & 0xFFu) + ((b      ) & 0xFFu) + 1u) >> 1;
                const uint32_t g = (((a >>  8) & 0xFFu) + ((b >>  8) & 0xFFu) + 1u) >> 1;
                const uint32_t bl= (((a >> 16) & 0xFFu) + ((b >> 16) & 0xFFu) + 1u) >> 1;
                uint32_t pixel = r | (g << 8) | (bl << 16) | 0xFF000000u;
                if (applyScanlines && ((y / rowScale) & 1)) pixel = DimPixel(pixel, darkFactor);
                dst[x] = pixel;
                dst[x + 1] = pixel;
            }
        } else {
            if (applyScanlines && ((y / rowScale) & 1)) {
                for (int x = 0; x < width; ++x) dst[x] = DimPixel(src[x], darkFactor);
            } else {
                std::memcpy(dst, src, static_cast<std::size_t>(width) * sizeof(uint32_t));
            }
        }
    }
    processed_frame = frame;
    processed_width = width;
    processed_height = height;
    processed_blend = blendPseudoHires;
    processed_scanlines = applyScanlines;
    processed_scanline_strength = graphics.scanline_strength;
    stride = width;
    return processed.data();
}
static std::filesystem::path CheatDatabasePath()
{
    char modulePath[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameA(nullptr, modulePath, MAX_PATH);
    return (length != 0 ? std::filesystem::path(modulePath).parent_path() : std::filesystem::current_path()) / "cheats.db";
}
static bool OpenROMDialog(std::string& outPath)
{
    char filename[MAX_PATH] = {};
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter =
        "SNES Games (*.sfc;*.smc;*.fig;*.swc;*.bin;*.rom;*.gd3;*.gd7;*.dx2;*.bs;*.zip;*.rar;*.7z)\0"
        "*.sfc;*.smc;*.fig;*.swc;*.bin;*.rom;*.gd3;*.gd7;*.dx2;*.bs;*.zip;*.rar;*.7z\0"
        "SNES ROMs (*.sfc;*.smc;*.fig;*.swc;*.bin;*.rom;*.gd3;*.gd7;*.dx2;*.bs)\0"
        "*.sfc;*.smc;*.fig;*.swc;*.bin;*.rom;*.gd3;*.gd7;*.dx2;*.bs\0"
        "Compressed Games (*.zip;*.rar;*.7z)\0*.zip;*.rar;*.7z\0"
        "All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = "Open SNES ROM";
    if (GetOpenFileNameA(&ofn)) {
        outPath = filename;
        return true;
    }
    return false;
}
static std::filesystem::path EmulatorRootDirectory()
{
    char exePath[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        return std::filesystem::path(exePath).parent_path();
    }
    std::error_code ec;
    const auto cwd = std::filesystem::current_path(ec);
    return ec ? std::filesystem::path(".") : cwd;
}
static std::string SafeRecordingStem(const Cartridge& cart)
{
    std::string stem = "SNESUltimate";
    if (!cart.sourcePath().empty()) {
        stem = std::filesystem::path(cart.sourcePath()).stem().string();
        if (stem.empty()) stem = "SNESUltimate";
    }
    for (char& ch : stem) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c < 32 || ch == '<' || ch == '>' || ch == ':' || ch == '"' ||
            ch == '/' || ch == '\\' || ch == '|' || ch == '?' || ch == '*') {
            ch = '_';
        }
    }
    while (!stem.empty() && (stem.back() == ' ' || stem.back() == '.')) stem.pop_back();
    return stem.empty() ? "SNESUltimate" : stem;
}
static bool AutomaticVideoPath(const Cartridge& cart, std::string& outPath, std::string& error,
    const char* directory = "recordings", const char* extension = ".mp4")
{
    const std::filesystem::path recordings = EmulatorRootDirectory() / directory;
    std::error_code ec;
    std::filesystem::create_directories(recordings, ec);
    if (ec) {
        error = "Could not create recordings directory: " + recordings.string() +
            " (" + ec.message() + ")";
        return false;
    }
    SYSTEMTIME local = {};
    GetLocalTime(&local);
    char stamp[32] = {};
    sprintf_s(stamp, "%04u%02u%02u_%02u%02u%02u",
        static_cast<unsigned>(local.wYear), static_cast<unsigned>(local.wMonth),
        static_cast<unsigned>(local.wDay), static_cast<unsigned>(local.wHour),
        static_cast<unsigned>(local.wMinute), static_cast<unsigned>(local.wSecond));
    const std::string base = SafeRecordingStem(cart) + "_" + stamp;
    std::filesystem::path candidate = recordings / (base + extension);
    for (unsigned suffix = 2; std::filesystem::exists(candidate, ec) && !ec; ++suffix) {
        candidate = recordings / (base + "_" + std::to_string(suffix) + extension);
    }
    if (ec) {
        error = "Could not inspect recordings directory: " + ec.message();
        return false;
    }
    outPath = candidate.string();
    return true;
}
static std::string QuickStatePath(const Cartridge& cart)
{
    if (cart.sourcePath().empty()) return "SNESUltimate.quickstate";
    return cart.sourcePath() + ".state";
}
static const char* MapperToString(MapperType mapper)
{
    switch (mapper) {
        case MapperType::LoROM: return "LoROM";
        case MapperType::ExLoROM: return "ExLoROM";
        case MapperType::HiROM: return "HiROM";
        case MapperType::ExHiROM: return "ExHiROM";
        default: return "Unknown";
    }
}
static const char* RegionToString(uint8_t region)
{
    switch (region) {
        case 0x00: return "Japan";
        case 0x01: return "North America";
        case 0x02: return "Europe";
        case 0x03: return "Scandinavia";
        case 0x04: return "Finland";
        case 0x05: return "Denmark";
        case 0x06: return "France";
        case 0x07: return "Holland";
        case 0x08: return "Spain";
        case 0x09: return "Germany";
        case 0x0A: return "Italy";
        case 0x0B: return "China";
        case 0x0C: return "Indonesia";
        case 0x0D: return "South Korea";
        default:   return "Unknown";
    }
}
static std::string FormatSize(std::size_t bytes)
{
    std::ostringstream oss;
    if (bytes >= 1024 * 1024) {
        oss << std::fixed << std::setprecision((bytes % (1024 * 1024)) == 0 ? 0 : 1)
            << (static_cast<double>(bytes) / (1024.0 * 1024.0)) << " MB";
    } else if (bytes >= 1024) {
        oss << (bytes / 1024) << " KB";
    } else {
        oss << bytes << " B";
    }
    return oss.str();
}
static void WaitUntilCounter(long double deadline, Uint64 frequency)
{
    static HANDLE timer = []() -> HANDLE {
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
        HANDLE h = CreateWaitableTimerExW(nullptr, nullptr,
            CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        if (!h) h = CreateWaitableTimerW(nullptr, FALSE, nullptr);
        return h;
    }();
    for (;;) {
        const Uint64 now = SDL_GetPerformanceCounter();
        if (static_cast<long double>(now) >= deadline) break;
        const long double remaining_seconds =
            (deadline - static_cast<long double>(now)) / static_cast<long double>(frequency);
        if (timer && remaining_seconds > 0.00045L) {
            const long double sleep_seconds = remaining_seconds - 0.00015L;
            LARGE_INTEGER due{};
            due.QuadPart = -static_cast<LONGLONG>(sleep_seconds * 10000000.0L);
            if (due.QuadPart == 0) due.QuadPart = -1;
            if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
                WaitForSingleObject(timer, INFINITE);
                continue;
            }
        }
        if (remaining_seconds > 0.00015L) {
            SwitchToThread();
        } else {
            YieldProcessor();
        }
    }
}
static int SDLCALL EmulatorMain(int argc, char** argv)
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }
    const char* renderer_override = SDL_getenv("SNESULTIMATE_RENDERER");
    const bool skip_vulkan = renderer_override != nullptr &&
        (SDL_strcasecmp(renderer_override, "d3d12") == 0 ||
         SDL_strcasecmp(renderer_override, "opengl") == 0);
    HMODULE vulkan_loader = skip_vulkan ? nullptr : LoadLibraryA("vulkan-1.dll");
    SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (vulkan_loader != nullptr) window_flags |= SDL_WINDOW_VULKAN;
    SDL_Window* window = SDL_CreateWindow(
        "SNESUltimate — High Accuracy Emulator",
        1100, 760,
        window_flags
    );
    if (!window) {
        fprintf(stderr, "Vulkan window unavailable (%s); creating a window for D3D12/OpenGL.\n", SDL_GetError());
        window = SDL_CreateWindow("SNESUltimate — High Accuracy Emulator", 1100, 760,
                                  SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (!window) {
            const char* err = SDL_GetError();
            fprintf(stderr, "SDL_CreateWindow failed: %s\n", err);
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "SNESUltimate startup error", err, nullptr);
            SDL_Quit();
            return -1;
        }
    }
    bool vulkan_capable = vulkan_loader != nullptr &&
        (SDL_GetWindowFlags(window) & SDL_WINDOW_VULKAN) != 0;
    Frontend::Settings frontend_settings;
    const std::filesystem::path frontend_settings_path =
        EmulatorRootDirectory() / "SNESUltimate.settings.ini";
    frontend_settings.load(frontend_settings_path);
    auto save_frontend_settings = [&]() {
        frontend_settings.save(frontend_settings_path);
    };
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "1024");
    SDL_AudioSpec desired_audio = {};
    desired_audio.freq = APU::AUDIO_SAMPLE_RATE;
    desired_audio.format = SDL_AUDIO_S16;
    desired_audio.channels = 2;
    SDL_AudioStream* audio_stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired_audio, nullptr, nullptr);
    if (!audio_stream) {
        printf("SDL_OpenAudioDeviceStream failed: %s\n", SDL_GetError());
    } else {
        SDL_ResumeAudioStreamDevice(audio_stream);
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding   = 6.0f;
    style.FrameRounding    = 4.0f;
    style.WindowBorderSize = 1.0f;
    std::unique_ptr<RendererBackend> renderer;
    VulkanRenderer* active_vulkan = nullptr;
    if (vulkan_capable) {
        if (!ImGui_ImplSDL3_InitForVulkan(window)) {
            fprintf(stderr, "ImGui SDL3 Vulkan setup failed (%s); using D3D12/OpenGL.\n", SDL_GetError());
            SDL_DestroyWindow(window);
            window = SDL_CreateWindow("SNESUltimate — High Accuracy Emulator", 1100, 760,
                                      SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
            if (!window) {
                const char* err = SDL_GetError();
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "SNESUltimate startup error", err, nullptr);
                ImGui::DestroyContext();
                if (audio_stream) SDL_DestroyAudioStream(audio_stream);
                SDL_Quit();
                return -1;
            }
            vulkan_capable = false;
        }
        if (vulkan_capable) {
        auto vulkan_renderer = std::make_unique<VulkanRenderer>();
        vulkan_renderer->setVSync(frontend_settings.graphics.vsync);
        if (vulkan_renderer->initializeGuarded(window)) {
            active_vulkan = vulkan_renderer.get();
            renderer = std::move(vulkan_renderer);
        } else {
            fprintf(stderr, "Vulkan 1.4 unavailable (%s); trying Direct3D 12 / OpenGL.\n", vulkan_renderer->lastError());
            vulkan_renderer->shutdown();
            vulkan_renderer.reset();
            ImGui_ImplSDL3_Shutdown();
            SDL_DestroyWindow(window);
            window = SDL_CreateWindow(
                "SNESUltimate — High Accuracy Emulator",
                1100, 760,
                SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
            );
            if (!window) {
                const char* err = SDL_GetError();
                fprintf(stderr, "SDL_CreateWindow failed: %s\n", err);
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "SNESUltimate startup error", err, nullptr);
                ImGui::DestroyContext();
                if (audio_stream) SDL_DestroyAudioStream(audio_stream);
                SDL_Quit();
                return -1;
            }
        }
        }
    }
    if (!renderer) {
        auto fallback = std::make_unique<SdlFallbackRenderer>();
        fallback->setVSync(frontend_settings.graphics.vsync);
        if (!fallback->initialize(window)) {
            fprintf(stderr, "Renderer initialization failed: %s\n", fallback->lastError());
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "SNESUltimate renderer error",
                                     fallback->lastError(), window);
            ImGui::DestroyContext();
            if (audio_stream) SDL_DestroyAudioStream(audio_stream);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return -1;
        }
        renderer = std::move(fallback);
    }
    auto snes_storage = std::make_unique<SNES>();
    SNES& snes = *snes_storage;
    CheatManager cheats;
    if (argc > 1) {
        snes.loadROM(argv[1]);
        if (snes.isLoaded()) cheats.loadDatabase(CheatDatabasePath(), snes.cartridge().title());
    }
    std::vector<int16_t> audio_staging;
    audio_staging.reserve(static_cast<size_t>(APU::AUDIO_SAMPLE_RATE) / 10 * 2);
    bool running = true;
    bool paused  = false;
    bool reset_pacer = true;
    SDL_Event event;
    const Uint64 perf_frequency = SDL_GetPerformanceFrequency();
    long double next_frame_deadline = static_cast<long double>(SDL_GetPerformanceCounter());
    bool fullscreen = false;
    Uint64 fps_window_start = SDL_GetPerformanceCounter();
    uint64_t fps_window_frames = 0;
    double measured_fps = 0.0;
    bool show_settings = false;
    int settings_page = 0;
    int controls_player = 0;
    int capture_player = -1;
    int capture_button = -1;
    bool paused_for_focus_loss = false;
    VideoCapture video_capture;
    auto release_all_keyboard_buttons = [&]() {
        for (int player = 1; player <= 2; ++player) {
            for (int button = 0; button < Controller::ButtonCount; ++button) {
                snes.setButton(player, button, false);
            }
        }
    };
    auto key_is_controller_binding = [&](SDL_Scancode scancode) {
        if (scancode == SDL_SCANCODE_UNKNOWN) return false;
        for (int player = 0; player < 2; ++player) {
            for (int button = 0; button < Controller::ButtonCount; ++button) {
                if (frontend_settings.controls.keyboard[player][button] == scancode) return true;
            }
        }
        return false;
    };
    auto apply_controller_key = [&](SDL_Scancode scancode, bool pressed) {
        if (scancode == SDL_SCANCODE_UNKNOWN) return;
        for (int player = 0; player < 2; ++player) {
            for (int button = 0; button < Controller::ButtonCount; ++button) {
                if (frontend_settings.controls.keyboard[player][button] == scancode) {
                    snes.setButton(player + 1, button, pressed);
                }
            }
        }
    };
    auto make_presentation_options = [&]() {
        RendererBackend::PresentationOptions options{};
        switch (frontend_settings.graphics.filter) {
            case Frontend::GraphicsFilter::Linear: options.filter = RendererBackend::FilterMode::Linear; break;
            case Frontend::GraphicsFilter::SharpBilinear: options.filter = RendererBackend::FilterMode::SharpBilinear; break;
            case Frontend::GraphicsFilter::Bicubic: options.filter = RendererBackend::FilterMode::Bicubic; break;
            default: options.filter = RendererBackend::FilterMode::Nearest; break;
        }
        options.compositeBlur = frontend_settings.graphics.composite_blur;
        options.colorProfile = frontend_settings.graphics.color_profile;
        switch (frontend_settings.graphics.aspect) {
            case Frontend::AspectRatio::Snes8x7:
                options.aspect = RendererBackend::AspectMode::Snes8x7;
                break;
            case Frontend::AspectRatio::Stretch:
                options.aspect = RendererBackend::AspectMode::Stretch;
                break;
            case Frontend::AspectRatio::Corrected4x3:
            default:
                options.aspect = RendererBackend::AspectMode::Corrected4x3;
                break;
        }
        options.integerScaling = frontend_settings.graphics.integer_scaling;
        options.pseudoHiresBlend = frontend_settings.graphics.pseudo_hires_blend &&
                                  snes.isLoaded() && snes.ppu().pseudoHiresOutputEnabled() &&
                                  !snes.ppu().presentationActive();
        if (snes.isLoaded()) {
            options.logicalWidth = snes.ppu().logicalWidth();
            options.logicalHeight = snes.ppu().logicalHeight();
        }
        options.scanlineStrength = frontend_settings.graphics.scanlines
            ? frontend_settings.graphics.scanline_strength : 0.0f;
        options.sharpenStrength = frontend_settings.graphics.sharpening;
        options.crtMaskStrength = frontend_settings.graphics.crt_mask_strength;
        options.gamma = frontend_settings.graphics.gamma;
        options.saturation = frontend_settings.graphics.saturation;
        options.contrast = frontend_settings.graphics.contrast;
        options.brightness = frontend_settings.graphics.brightness;
        options.curvature = frontend_settings.graphics.curvature;
        options.vignette = frontend_settings.graphics.vignette;
        options.crtMaskStyle = frontend_settings.graphics.crt_mask_style;
        options.overscan = frontend_settings.graphics.overscan;
        options.temperature = frontend_settings.graphics.temperature;
        return options;
    };
    auto reset_host_measurements = [&]() {
        if (audio_stream != nullptr) SDL_ClearAudioStream(audio_stream);
        const Uint64 now = SDL_GetPerformanceCounter();
        next_frame_deadline = static_cast<long double>(now);
        reset_pacer = true;
    };
    auto stop_video_capture = [&]() {
        if (!video_capture.isRecording()) return;
        video_capture.stop();
    };
    auto toggle_video_capture = [&]() {
        if (video_capture.isRecording()) {
            stop_video_capture();
            return;
        }
        if (!snes.isLoaded()) return;
        std::string path;
        std::string pathError;
        if (!AutomaticVideoPath(snes.cartridge(), path, pathError)) return;
        video_capture.start(path, snes.nominalFrameRate(), APU::AUDIO_SAMPLE_RATE);
    };
    auto take_screenshot = [&]() {
        if (!snes.isLoaded()) return;
        const PPU& shot_ppu = snes.ppu();
        const int shot_width = shot_ppu.width();
        const int shot_height = shot_ppu.height();
        const int shot_stride = shot_ppu.framebufferStride();
        std::string path;
        std::string pathError;
        if (!AutomaticVideoPath(snes.cartridge(), path, pathError, "screenshots", ".png")) {
            printf("[Screenshot] %s\n", pathError.c_str());
            return;
        }
        SDL_Surface* surface = SDL_CreateSurfaceFrom(shot_width, shot_height, SDL_PIXELFORMAT_ABGR8888,
            const_cast<uint32_t*>(shot_ppu.framebuffer()), shot_stride * static_cast<int>(sizeof(uint32_t)));
        if (surface == nullptr || !SDL_SavePNG(surface, path.c_str()))
            printf("[Screenshot] Failed: %s\n", SDL_GetError());
        else
            printf("[Screenshot] Saved %s\n", path.c_str());
        if (surface != nullptr) SDL_DestroySurface(surface);
    };
    if (snes.isLoaded()) reset_host_measurements();
    while (running) {
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) running = false;
            if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                release_all_keyboard_buttons();
                if (frontend_settings.general.pause_when_unfocused && snes.isLoaded() && !paused) {
                    paused = true;
                    paused_for_focus_loss = true;
                    reset_pacer = true;
                }
            } else if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
                if (paused_for_focus_loss) {
                    paused = false;
                    paused_for_focus_loss = false;
                    reset_pacer = true;
                }
            }
            if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP) continue;
            const bool pressed = event.type == SDL_EVENT_KEY_DOWN;
            const SDL_Scancode scancode = event.key.scancode;
            if (capture_player >= 0 && capture_button >= 0) {
                if (pressed && !event.key.repeat) {
                    if (scancode == SDL_SCANCODE_ESCAPE) {
                        capture_player = -1;
                        capture_button = -1;
                    } else {
                        release_all_keyboard_buttons();
                        if (scancode == SDL_SCANCODE_BACKSPACE || scancode == SDL_SCANCODE_DELETE) {
                            frontend_settings.controls.keyboard[capture_player][capture_button] = SDL_SCANCODE_UNKNOWN;
                        } else {
                            frontend_settings.controls.keyboard[capture_player][capture_button] = scancode;
                        }
                        save_frontend_settings();
                        capture_player = -1;
                        capture_button = -1;
                    }
                }
                continue;
            }
            if (!pressed || !io.WantCaptureKeyboard) {
                apply_controller_key(scancode, pressed);
            }
            if (!pressed || event.key.repeat || io.WantCaptureKeyboard) continue;
            const bool controllerBinding = key_is_controller_binding(scancode);
            switch (scancode) {
                case SDL_SCANCODE_SPACE:
                    if (!controllerBinding) { paused = !paused; paused_for_focus_loss = false; reset_pacer = true; }
                    break;
                case SDL_SCANCODE_F11:
                    if (!controllerBinding) {
                        const bool requested = !fullscreen;
                        if (SDL_SetWindowFullscreen(window, requested)) {
                            fullscreen = requested;
                        } else {
                            printf("SDL_SetWindowFullscreen failed: %s\n", SDL_GetError());
                        }
                    }
                    break;
                case SDL_SCANCODE_F5:
                    if (!controllerBinding && snes.isLoaded()) {
                        const std::string path = QuickStatePath(snes.cartridge());
                        snes.saveStateToFile(path);
                    }
                    break;
                case SDL_SCANCODE_F8:
                    if (!controllerBinding && snes.isLoaded()) {
                        const std::string path = QuickStatePath(snes.cartridge());
                        if (snes.loadStateFromFile(path)) {
                            if (audio_stream != nullptr) SDL_ClearAudioStream(audio_stream);
                            reset_host_measurements();
                        }
                    }
                    break;
                case SDL_SCANCODE_F10:
                    if (!controllerBinding) toggle_video_capture();
                    break;
                case SDL_SCANCODE_F9:
                    if (!controllerBinding) take_screenshot();
                    break;
                case SDL_SCANCODE_O:
                    if (!controllerBinding && (event.key.mod & SDL_KMOD_CTRL) != 0) {
                        std::string path;
                        if (OpenROMDialog(path)) {
                            stop_video_capture();
                            if (snes.loadROM(path)) {
                                paused = false;
                                paused_for_focus_loss = false;
                                reset_host_measurements();
                            }
                        }
                    }
                    break;
                default:
                    break;
            }
        }
        bool ran_emulated_frame = false;
        if (!paused && snes.isLoaded()) {
            const long double frame_ticks =
                static_cast<long double>(perf_frequency) / snes.nominalFrameRate();
            if (reset_pacer) {
                next_frame_deadline = static_cast<long double>(SDL_GetPerformanceCounter()) + frame_ticks;
                reset_pacer = false;
            } else {
                next_frame_deadline += frame_ticks;
            }
            snes.ppu().setSpriteLimitRemoved(frontend_settings.graphics.remove_sprite_limit);
            snes.ppu().setFastScanline(frontend_settings.experimental.fast_scanline_ppu);
            snes.ppu().setJitScanline(frontend_settings.experimental.jit_ppu);
            snes.ppu().setMode7Widescreen(frontend_settings.graphics.mode7_widescreen);
            snes.ppu().setMode7HdScale(frontend_settings.graphics.hd_mode7 ? frontend_settings.graphics.hd_mode7_scale : 1);
            snes.runFrame();
            cheats.apply(snes.bus());
            if (video_capture.isRecording()) {
                int capture_width = 0, capture_height = 0, capture_stride = 0;
                const uint32_t* capture_pixels = PresentationFramebuffer(
                    snes.ppu(), frontend_settings.graphics,
                    capture_width, capture_height, capture_stride);
                if (!video_capture.writeFrame(capture_pixels, capture_width,
                        capture_height, capture_stride, snes.ppu().logicalWidth(),
                        snes.ppu().logicalHeight())) {
                    printf("[Capture] Video write failed: %s\n", video_capture.lastError().c_str());
                    stop_video_capture();
                }
            }
            ran_emulated_frame = true;
            ++fps_window_frames;
            const Uint64 fps_now = SDL_GetPerformanceCounter();
            const double fps_elapsed = static_cast<double>(fps_now - fps_window_start) / static_cast<double>(perf_frequency);
            if (fps_elapsed >= 0.5) {
                measured_fps = static_cast<double>(fps_window_frames) / fps_elapsed;
                fps_window_start = fps_now;
                fps_window_frames = 0;
            }
        } else {
            reset_pacer = true;
            measured_fps = 0.0;
            fps_window_start = SDL_GetPerformanceCounter();
            fps_window_frames = 0;
        }
        snes.apu().takeAudioSamples(audio_staging);
        auto& audio = audio_staging;
        if (!audio.empty()) {
            if (video_capture.isRecording()) {
                if (!video_capture.writeAudio(audio.data(), audio.size())) {
                    printf("[Capture] Audio write failed: %s\n", video_capture.lastError().c_str());
                    stop_video_capture();
                }
            }
            if (audio_stream != nullptr && !frontend_settings.audio.muted) {
                if (frontend_settings.audio.master_volume < 0.999f) {
                    const float gain = std::clamp(frontend_settings.audio.master_volume, 0.0f, 1.0f);
                    for (int16_t& sample : audio) {
                        const float scaled = static_cast<float>(sample) * gain;
                        sample = static_cast<int16_t>(std::clamp(scaled, -32768.0f, 32767.0f));
                    }
                }
                const int queued = SDL_GetAudioStreamQueued(audio_stream);
                const Uint32 max_queue = static_cast<Uint32>(
                    APU::AUDIO_SAMPLE_RATE * 2 * sizeof(int16_t));
                if (queued > static_cast<int>(max_queue)) {
                    SDL_ClearAudioStream(audio_stream);
                }
                SDL_PutAudioStreamData(audio_stream, audio.data(),
                    static_cast<int>(audio.size() * sizeof(int16_t)));
            }
        }
        {
        renderer->imguiNewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Open ROM...", "Ctrl+O")) {
                    std::string path;
                    if (OpenROMDialog(path)) {
                        stop_video_capture();
                        if (snes.loadROM(path)) {
                            cheats.loadDatabase(CheatDatabasePath(), snes.cartridge().title());
                            paused = false;
                            reset_host_measurements();
                        }
                    }
                }
                if (ImGui::MenuItem("Reset", nullptr, false, snes.isLoaded())) {
                    snes.reset();
                    reset_host_measurements();
                }
                if (ImGui::MenuItem("Save State", "F5", false, snes.isLoaded())) {
                    const std::string path = QuickStatePath(snes.cartridge());
                    snes.saveStateToFile(path);
                }
                if (ImGui::MenuItem("Load State", "F8", false, snes.isLoaded())) {
                    const std::string path = QuickStatePath(snes.cartridge());
                    if (snes.loadStateFromFile(path)) {
                        if (audio_stream != nullptr) SDL_ClearAudioStream(audio_stream);
                        reset_host_measurements();
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Screenshot", "F9", false, snes.isLoaded())) take_screenshot();
                if (ImGui::MenuItem(video_capture.isRecording() ? "Stop Video Capture" : "Start Video Capture",
                        "F10", false, snes.isLoaded() || video_capture.isRecording())) {
                    toggle_video_capture();
                }
                ImGui::Separator();
                if (ImGui::MenuItem(paused ? "Resume" : "Pause", "Space")) {
                    paused = !paused;
                    paused_for_focus_loss = false;
                    reset_pacer = true;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit")) running = false;
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Cheats", snes.isLoaded())) {
                if (ImGui::MenuItem("Reload cheats")) {
                    cheats.loadDatabase(CheatDatabasePath(), snes.cartridge().title());
                }
                if (cheats.entries().empty()) {
                    ImGui::MenuItem("No cheats found", nullptr, false, false);
                }
                for (auto& cheat : cheats.entries()) ImGui::MenuItem(cheat.description.c_str(), nullptr, &cheat.enabled);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Experimental")) {
                if (ImGui::MenuItem("Fast scanline PPU renderer", nullptr, &frontend_settings.experimental.fast_scanline_ppu)) save_frontend_settings();
                ImGui::TextDisabled("Builds each background line in one pass. Output should match the normal renderer.");
                if (ImGui::MenuItem("JIT PPU line compositor (AsmJit)", nullptr, &frontend_settings.experimental.jit_ppu)) save_frontend_settings();
                ImGui::TextDisabled("Compiles the layer priority and colour math loop to native code for plain Mode 0/1 lines. Other lines use the normal renderer.");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Settings")) {
                if (ImGui::MenuItem("Fullscreen", "F11", fullscreen)) {
                    const bool requested = !fullscreen;
                    if (SDL_SetWindowFullscreen(window, requested)) {
                        fullscreen = requested;
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Graphics & Display...")) {
                    show_settings = true;
                    settings_page = 0;
                    capture_player = -1;
                    capture_button = -1;
                }
                if (ImGui::MenuItem("Controls...")) {
                    show_settings = true;
                    settings_page = 1;
                    capture_player = -1;
                    capture_button = -1;
                }
                if (ImGui::MenuItem("Audio...")) {
                    show_settings = true;
                    settings_page = 2;
                    capture_player = -1;
                    capture_button = -1;
                }
                if (ImGui::MenuItem("General...")) {
                    show_settings = true;
                    settings_page = 3;
                    capture_player = -1;
                    capture_button = -1;
                }
                ImGui::Separator();
                bool muted = frontend_settings.audio.muted;
                if (ImGui::MenuItem("Mute audio", nullptr, &muted)) {
                    frontend_settings.audio.muted = muted;
                    if (audio_stream != nullptr) SDL_ClearAudioStream(audio_stream);
                    save_frontend_settings();
                }
                if (ImGui::MenuItem("Show FPS", nullptr, &frontend_settings.general.show_fps)) save_frontend_settings();
                ImGui::EndMenu();
            }
            if (frontend_settings.general.show_fps && snes.isLoaded()) {
                const double nominal = snes.nominalFrameRate();
                const int target_fps = static_cast<int>(nominal + 0.5);
                const int shown_fps = static_cast<int>(measured_fps + 0.5);
                const int percent = nominal > 0.0 ? static_cast<int>(100.0 * measured_fps / nominal + 0.5) : 0;
                char fps_text[64];
                snprintf(fps_text, sizeof(fps_text), "%d/%d FPS  %d%%", shown_fps, target_fps, percent);
                const float text_width = ImGui::CalcTextSize(fps_text).x;
                ImGui::SetCursorPosX(ImGui::GetWindowWidth() - text_width - ImGui::GetStyle().ItemSpacing.x * 2.0f);
                const ImVec4 color = percent >= 98 ? ImVec4(0.40f, 0.95f, 0.50f, 1.0f) : (percent >= 90 ? ImVec4(1.0f, 0.75f, 0.20f, 1.0f) : ImVec4(1.0f, 0.35f, 0.30f, 1.0f));
                ImGui::TextColored(color, "%s", fps_text);
            }
            ImGui::EndMainMenuBar();
        }
        if (show_settings) {
            ImGui::SetNextWindowSize(ImVec2(760.0f, 590.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Settings", &show_settings, ImGuiWindowFlags_NoCollapse)) {
                constexpr float navWidth = 150.0f;
                ImGui::BeginChild("##settings_nav", ImVec2(navWidth, 0.0f), true);
                if (ImGui::Selectable("Graphics", settings_page == 0)) { settings_page = 0; capture_player = -1; capture_button = -1; }
                if (ImGui::Selectable("Controls", settings_page == 1)) { settings_page = 1; capture_player = -1; capture_button = -1; }
                if (ImGui::Selectable("Audio", settings_page == 2)) { settings_page = 2; capture_player = -1; capture_button = -1; }
                if (ImGui::Selectable("General", settings_page == 3)) { settings_page = 3; capture_player = -1; capture_button = -1; }
                ImGui::EndChild();
                ImGui::SameLine();
                ImGui::BeginChild("##settings_content", ImVec2(0.0f, 0.0f), false);
                if (settings_page == 0) {
                    ImGui::Text("Graphics & Display");
                    ImGui::Separator();
                    ImGui::TextWrapped("These options change presentation only. They do not alter SNES emulation accuracy or save states.");
                    ImGui::Spacing();
                    ImGui::Text("Presets");
                    if (ImGui::Button("Smooth (Recommended)")) {
                        frontend_settings.graphics.filter = Frontend::GraphicsFilter::Linear;
                        frontend_settings.graphics.aspect = Frontend::AspectRatio::Corrected4x3;
                        frontend_settings.graphics.integer_scaling = false;
                        frontend_settings.graphics.vsync = true;
                        frontend_settings.graphics.pseudo_hires_blend = true;
                        frontend_settings.graphics.scanlines = false;
                        frontend_settings.graphics.sharpening = 0.12f;
                        frontend_settings.graphics.crt_mask_strength = 0.0f;
                        frontend_settings.graphics.composite_blur = 0.0f;
                        frontend_settings.graphics.color_profile = 0;
                        renderer->setVSync(true);
                        save_frontend_settings();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Pixel Sharp")) {
                        frontend_settings.graphics.filter = Frontend::GraphicsFilter::Nearest;
                        frontend_settings.graphics.aspect = Frontend::AspectRatio::Snes8x7;
                        frontend_settings.graphics.integer_scaling = true;
                        frontend_settings.graphics.vsync = true;
                        frontend_settings.graphics.pseudo_hires_blend = false;
                        frontend_settings.graphics.scanlines = false;
                        frontend_settings.graphics.sharpening = 0.0f;
                        frontend_settings.graphics.crt_mask_strength = 0.0f;
                        frontend_settings.graphics.composite_blur = 0.0f;
                        frontend_settings.graphics.color_profile = 0;
                        renderer->setVSync(true);
                        save_frontend_settings();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("CRT-like")) {
                        frontend_settings.graphics.filter = Frontend::GraphicsFilter::Linear;
                        frontend_settings.graphics.aspect = Frontend::AspectRatio::Corrected4x3;
                        frontend_settings.graphics.integer_scaling = false;
                        frontend_settings.graphics.vsync = true;
                        frontend_settings.graphics.pseudo_hires_blend = true;
                        frontend_settings.graphics.scanlines = true;
                        frontend_settings.graphics.scanline_strength = 0.16f;
                        frontend_settings.graphics.sharpening = 0.06f;
                        frontend_settings.graphics.crt_mask_strength = 0.18f;
                        renderer->setVSync(true);
                        save_frontend_settings();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Composite TV")) {
                        frontend_settings.graphics.filter = Frontend::GraphicsFilter::Bicubic;
                        frontend_settings.graphics.aspect = Frontend::AspectRatio::Corrected4x3;
                        frontend_settings.graphics.integer_scaling = false;
                        frontend_settings.graphics.pseudo_hires_blend = true;
                        frontend_settings.graphics.scanlines = true;
                        frontend_settings.graphics.scanline_strength = 0.14f;
                        frontend_settings.graphics.sharpening = 0.0f;
                        frontend_settings.graphics.crt_mask_strength = 0.10f;
                        frontend_settings.graphics.composite_blur = 0.7f;
                        frontend_settings.graphics.color_profile = 1;
                        frontend_settings.graphics.curvature = 0.04f;
                        frontend_settings.graphics.vignette = 0.12f;
                        save_frontend_settings();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Crisp + Vibrant")) {
                        frontend_settings.graphics.filter = Frontend::GraphicsFilter::SharpBilinear;
                        frontend_settings.graphics.aspect = Frontend::AspectRatio::Corrected4x3;
                        frontend_settings.graphics.integer_scaling = false;
                        frontend_settings.graphics.pseudo_hires_blend = true;
                        frontend_settings.graphics.scanlines = false;
                        frontend_settings.graphics.sharpening = 0.0f;
                        frontend_settings.graphics.crt_mask_strength = 0.0f;
                        frontend_settings.graphics.composite_blur = 0.0f;
                        frontend_settings.graphics.color_profile = 3;
                        frontend_settings.graphics.curvature = 0.0f;
                        frontend_settings.graphics.vignette = 0.0f;
                        save_frontend_settings();
                    }
                    ImGui::Spacing();
                    ImGui::SeparatorText("Scaling");
                    int filter = static_cast<int>(frontend_settings.graphics.filter);
                    const char* filterNames[] = {"Nearest (sharp pixels)", "Bilinear (smooth)", "Sharp bilinear (crisp, smooth edges)", "Bicubic (soft, high quality)"};
                    if (!renderer->supportsLinearFiltering() && filter != static_cast<int>(Frontend::GraphicsFilter::Nearest)) {
                        filter = static_cast<int>(Frontend::GraphicsFilter::Nearest);
                        frontend_settings.graphics.filter = Frontend::GraphicsFilter::Nearest;
                    }
                    if (ImGui::Combo("Scaling filter", &filter, filterNames, IM_ARRAYSIZE(filterNames))) {
                        frontend_settings.graphics.filter = static_cast<Frontend::GraphicsFilter>(filter);
                        save_frontend_settings();
                    }
                    if (!renderer->supportsLinearFiltering()) {
                        ImGui::TextDisabled("This GPU does not expose linear filtering for the framebuffer blit format; nearest is used.");
                    }
                    int aspect = static_cast<int>(frontend_settings.graphics.aspect);
                    const char* aspectNames[] = {"Corrected 4:3", "SNES 8:7 pixel aspect", "Stretch to window"};
                    if (ImGui::Combo("Aspect ratio", &aspect, aspectNames, IM_ARRAYSIZE(aspectNames))) {
                        frontend_settings.graphics.aspect = static_cast<Frontend::AspectRatio>(aspect);
                        save_frontend_settings();
                    }
                    if (ImGui::Checkbox("Integer vertical scaling", &frontend_settings.graphics.integer_scaling)) {
                        save_frontend_settings();
                    }
                    ImGui::TextDisabled("Integer scaling keeps source scanlines on whole-pixel boundaries when the window is large enough.");
                    bool vsync = frontend_settings.graphics.vsync;
                    if (ImGui::Checkbox("VSync", &vsync)) {
                        frontend_settings.graphics.vsync = vsync;
                        renderer->setVSync(vsync);
                        save_frontend_settings();
                    }
                    ImGui::TextDisabled("VSync uses Vulkan FIFO presentation to eliminate tearing. Disabling it prefers immediate presentation.");
                    if (ImGui::Checkbox("Pseudo-hires blending", &frontend_settings.graphics.pseudo_hires_blend)) {
                        save_frontend_settings();
                    }
                    ImGui::TextDisabled("Blends alternating pseudo-hires color pairs for transparency effects on modern displays.");
                    if (ImGui::Checkbox("Scanlines", &frontend_settings.graphics.scanlines)) {
                        save_frontend_settings();
                    }
                    if (frontend_settings.graphics.scanlines) {
                        float strength = frontend_settings.graphics.scanline_strength * 100.0f;
                        if (ImGui::SliderFloat("Scanline strength", &strength, 4.0f, 40.0f, "%.0f%%")) {
                            frontend_settings.graphics.scanline_strength = strength / 100.0f;
                            save_frontend_settings();
                        }
                    }
                    float sharpening = frontend_settings.graphics.sharpening * 100.0f;
                    if (ImGui::SliderFloat("GPU sharpening", &sharpening, 0.0f, 50.0f, "%.0f%%")) {
                        frontend_settings.graphics.sharpening = sharpening / 100.0f;
                        save_frontend_settings();
                    }
                    ImGui::TextDisabled("Adds edge clarity after scaling; useful with bilinear smoothing.");
                    float maskStrength = frontend_settings.graphics.crt_mask_strength * 100.0f;
                    if (ImGui::SliderFloat("CRT phosphor mask", &maskStrength, 0.0f, 45.0f, "%.0f%%")) {
                        frontend_settings.graphics.crt_mask_strength = maskStrength / 100.0f;
                        save_frontend_settings();
                    }
                    ImGui::TextDisabled("Subtle RGB phosphor structure applied by the Vulkan presentation shader.");
                    const char* maskStyles[] = {"RGB stripe", "Aperture grille", "Shadow mask"};
                    int maskStyle = frontend_settings.graphics.crt_mask_style;
                    if (ImGui::Combo("CRT mask style", &maskStyle, maskStyles, IM_ARRAYSIZE(maskStyles))) { frontend_settings.graphics.crt_mask_style = maskStyle; save_frontend_settings(); }
                    float overscan = frontend_settings.graphics.overscan * 100.0f;
                    if (ImGui::SliderFloat("Overscan crop", &overscan, 0.0f, 12.0f, "%.1f%%")) { frontend_settings.graphics.overscan = overscan / 100.0f; save_frontend_settings(); }
                    float temperature = frontend_settings.graphics.temperature;
                    if (ImGui::SliderFloat("Color temperature", &temperature, -1.0f, 1.0f, "%.2f")) { frontend_settings.graphics.temperature = temperature; save_frontend_settings(); }
                    float gamma = frontend_settings.graphics.gamma;
                    if (ImGui::SliderFloat("Gamma", &gamma, 0.5f, 2.5f, "%.2f")) { frontend_settings.graphics.gamma = gamma; save_frontend_settings(); }
                    float saturation = frontend_settings.graphics.saturation;
                    if (ImGui::SliderFloat("Saturation", &saturation, 0.0f, 2.0f, "%.2f")) { frontend_settings.graphics.saturation = saturation; save_frontend_settings(); }
                    float contrast = frontend_settings.graphics.contrast;
                    if (ImGui::SliderFloat("Contrast", &contrast, 0.0f, 2.0f, "%.2f")) { frontend_settings.graphics.contrast = contrast; save_frontend_settings(); }
                    float brightness = frontend_settings.graphics.brightness;
                    if (ImGui::SliderFloat("Brightness", &brightness, -1.0f, 1.0f, "%.2f")) { frontend_settings.graphics.brightness = brightness; save_frontend_settings(); }
                    float curvature = frontend_settings.graphics.curvature;
                    if (ImGui::SliderFloat("CRT curvature", &curvature, 0.0f, 0.35f, "%.2f")) { frontend_settings.graphics.curvature = curvature; save_frontend_settings(); }
                    float vignette = frontend_settings.graphics.vignette;
                    if (ImGui::SliderFloat("CRT vignette", &vignette, 0.0f, 1.0f, "%.2f")) { frontend_settings.graphics.vignette = vignette; save_frontend_settings(); }
                    float compositeBlur = frontend_settings.graphics.composite_blur;
                    if (ImGui::SliderFloat("Composite blur (NTSC colour bleed)", &compositeBlur, 0.0f, 1.0f, "%.2f")) { frontend_settings.graphics.composite_blur = compositeBlur; save_frontend_settings(); }
                    int colorProfile = frontend_settings.graphics.color_profile;
                    const char* colorProfileNames[] = {"Off (raw SNES colours)", "NTSC TV phosphors (SMPTE-C)", "PAL TV phosphors (EBU)", "Vibrant"};
                    if (ImGui::Combo("Colour profile", &colorProfile, colorProfileNames, IM_ARRAYSIZE(colorProfileNames))) { frontend_settings.graphics.color_profile = colorProfile; save_frontend_settings(); }
                    ImGui::Spacing();
                    ImGui::SeparatorText("Hardware limits");
                    if (ImGui::Checkbox("Remove sprite limit (no flicker)", &frontend_settings.graphics.remove_sprite_limit)) save_frontend_settings();
                    ImGui::TextDisabled("Draws every sprite on a line instead of 32 sprites / 34 tiles. Not hardware accurate; the status flags games read are unchanged.");
                    if (ImGui::Checkbox("Mode 7 widescreen", &frontend_settings.graphics.mode7_widescreen)) save_frontend_settings();
                    ImGui::TextDisabled("Extends Mode 7 backgrounds past the left and right edges. Other layers and sprites are not extended; lines that are not Mode 7 get black bars.");
                    if (ImGui::Checkbox("HD Mode 7", &frontend_settings.graphics.hd_mode7)) save_frontend_settings();
                    if (frontend_settings.graphics.hd_mode7) {
                        int hdScale = frontend_settings.graphics.hd_mode7_scale;
                        if (ImGui::SliderInt("HD Mode 7 scale", &hdScale, 2, 4, "%dx")) { frontend_settings.graphics.hd_mode7_scale = hdScale; save_frontend_settings(); }
                    }
                    ImGui::TextDisabled("Renders Mode 7 background 1 at 2x to 4x resolution; everything else is repeated pixels. Not hardware accurate.");
                    ImGui::Spacing();
                    ImGui::SeparatorText("Window");
                    bool requestedFullscreen = fullscreen;
                    if (ImGui::Checkbox("Fullscreen", &requestedFullscreen)) {
                        if (SDL_SetWindowFullscreen(window, requestedFullscreen)) {
                            fullscreen = requestedFullscreen;
                        }
                    }
                    ImGui::Text("Renderer: %s — %s", renderer->backendLabel(), renderer->adapterName());
                } else if (settings_page == 1) {
                    ImGui::Text("Keyboard Controls");
                    ImGui::Separator();
                    ImGui::TextWrapped("Click a binding, then press any keyboard key. Escape cancels. Backspace/Delete clears the binding. Player 1 and Player 2 are independent.");
                    ImGui::Spacing();
                    if (ImGui::RadioButton("Player 1", controls_player == 0)) {
                        controls_player = 0;
                        capture_player = -1;
                        capture_button = -1;
                    }
                    ImGui::SameLine();
                    if (ImGui::RadioButton("Player 2", controls_player == 1)) {
                        controls_player = 1;
                        capture_player = -1;
                        capture_button = -1;
                    }
                    ImGui::Spacing();
                    if (ImGui::BeginTable("##bindings", 3,
                            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                        ImGui::TableSetupColumn("SNES Button", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                        ImGui::TableSetupColumn("Keyboard Key", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 95.0f);
                        ImGui::TableHeadersRow();
                        for (int button = 0; button < Controller::ButtonCount; ++button) {
                            ImGui::PushID(button);
                            const SDL_Scancode assigned = frontend_settings.controls.keyboard[controls_player][button];
                            int duplicateCount = 0;
                            if (assigned != SDL_SCANCODE_UNKNOWN) {
                                for (int player = 0; player < 2; ++player) {
                                    for (int other = 0; other < Controller::ButtonCount; ++other) {
                                        if (frontend_settings.controls.keyboard[player][other] == assigned) ++duplicateCount;
                                    }
                                }
                            }
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextUnformatted(Frontend::controllerButtonName(button));
                            ImGui::TableSetColumnIndex(1);
                            const bool capturing = capture_player == controls_player && capture_button == button;
                            const char* keyName = capturing ? "Press a key..." : Frontend::scancodeName(assigned);
                            if (ImGui::Button(keyName, ImVec2(-1.0f, 0.0f))) {
                                release_all_keyboard_buttons();
                                capture_player = controls_player;
                                capture_button = button;
                            }
                            ImGui::TableSetColumnIndex(2);
                            if (duplicateCount > 1) {
                                ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.25f, 1.0f), "Duplicate");
                            } else if (assigned == SDL_SCANCODE_UNKNOWN) {
                                ImGui::TextDisabled("Unbound");
                            } else {
                                ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.55f, 1.0f), "OK");
                            }
                            ImGui::PopID();
                        }
                        ImGui::EndTable();
                    }
                    ImGui::Spacing();
                    if (ImGui::Button("Reset this player to defaults")) {
                        release_all_keyboard_buttons();
                        frontend_settings.controls.resetPlayer(controls_player);
                        capture_player = -1;
                        capture_button = -1;
                        save_frontend_settings();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Clear this player")) {
                        release_all_keyboard_buttons();
                        frontend_settings.controls.keyboard[controls_player].fill(SDL_SCANCODE_UNKNOWN);
                        capture_player = -1;
                        capture_button = -1;
                        save_frontend_settings();
                    }
                    ImGui::TextDisabled("If a controller binding uses Space/F5/F8/F9/F10/F11/Tab or Ctrl+O, gameplay takes priority over that shortcut.");
                } else if (settings_page == 2) {
                    ImGui::Text("Audio");
                    ImGui::Separator();
                    int volumePercent = static_cast<int>(frontend_settings.audio.master_volume * 100.0f + 0.5f);
                    if (ImGui::SliderInt("Master volume", &volumePercent, 0, 100, "%d%%")) {
                        frontend_settings.audio.master_volume = static_cast<float>(volumePercent) / 100.0f;
                        save_frontend_settings();
                    }
                    bool muted = frontend_settings.audio.muted;
                    if (ImGui::Checkbox("Mute", &muted)) {
                        frontend_settings.audio.muted = muted;
                        if (audio_stream != nullptr) SDL_ClearAudioStream(audio_stream);
                        save_frontend_settings();
                    }
                    ImGui::TextDisabled("Volume affects local playback only. Video captures retain the emulator's original audio level.");
                    if (audio_stream != nullptr) {
                        const float queueMs = 1000.0f * SDL_GetAudioStreamQueued(audio_stream) /
                            static_cast<float>(APU::AUDIO_SAMPLE_RATE * 2 * sizeof(int16_t));
                        ImGui::Text("Current audio queue: %.1f ms", queueMs);
                    } else {
                        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "No SDL audio output device is active.");
                    }
                } else {
                    ImGui::Text("General");
                    ImGui::Separator();
                    if (ImGui::Checkbox("Pause emulation when the window loses focus",
                                        &frontend_settings.general.pause_when_unfocused)) {
                        if (!frontend_settings.general.pause_when_unfocused) paused_for_focus_loss = false;
                        save_frontend_settings();
                    }
                    ImGui::TextDisabled("This also releases all held controller keys to prevent stuck input after Alt+Tab.");
                    ImGui::Spacing();
                    ImGui::SeparatorText("Configuration");
                    ImGui::TextWrapped("Settings file: %s", frontend_settings_path.string().c_str());
                    if (ImGui::Button("Reset all settings to defaults")) {
                        release_all_keyboard_buttons();
                        frontend_settings.resetAll();
                        renderer->setVSync(frontend_settings.graphics.vsync);
                        if (audio_stream != nullptr) SDL_ClearAudioStream(audio_stream);
                        capture_player = -1;
                        capture_button = -1;
                        save_frontend_settings();
                    }
                }
                ImGui::EndChild();
            }
            ImGui::End();
        } else {
            capture_player = -1;
            capture_button = -1;
        }
        ImGui::Render();
        int host_width = 0, host_height = 0, host_stride = 0;
        const uint32_t* host_pixels = nullptr;
        if (snes.isLoaded()) {
            host_width = snes.ppu().width();
            host_height = snes.ppu().height();
            host_stride = snes.ppu().framebufferStride();
            host_pixels = snes.ppu().framebuffer();
        }
        if (!renderer->render(host_pixels, host_width, host_height, host_stride,
                         make_presentation_options(), ImGui::GetDrawData())) {
            fprintf(stderr, "%s render failed: %s\n", renderer->backendLabel(), renderer->lastError());
            running = false;
        }
        }
        if (ran_emulated_frame) {
            const long double frame_ticks =
                static_cast<long double>(perf_frequency) / snes.nominalFrameRate();
            const Uint64 now = SDL_GetPerformanceCounter();
            if (static_cast<long double>(now) > next_frame_deadline + frame_ticks * 0.5L) {
                next_frame_deadline = static_cast<long double>(now);
            } else {
                WaitUntilCounter(next_frame_deadline, perf_frequency);
            }
        } else if (!ran_emulated_frame) {
            SDL_Delay(8);
        }
    }
    stop_video_capture();
    renderer->shutdown();
    if (active_vulkan != nullptr) ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    if (audio_stream) SDL_DestroyAudioStream(audio_stream);
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (vulkan_loader != nullptr) FreeLibrary(vulkan_loader);
    return 0;
}
int main(int argc, char** argv)
{
    SDL_SetMainReady();
    return EmulatorMain(argc, argv);
}

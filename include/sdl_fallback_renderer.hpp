#pragma once
#ifdef _WIN32
#include <SDL3/SDL.h>
#include "renderer_backend.hpp"
#include <array>
#include <vector>
#include <cstdint>
#include <string>
class SdlFallbackRenderer final : public RendererBackend {
public:
    SdlFallbackRenderer() = default;
    ~SdlFallbackRenderer() override;
    SdlFallbackRenderer(const SdlFallbackRenderer&) = delete;
    SdlFallbackRenderer& operator=(const SdlFallbackRenderer&) = delete;
    bool initialize(SDL_Window* window) override;
    void shutdown() override;
    bool render(const uint32_t* pixels, int sourceWidth, int sourceHeight,
                int sourceStridePixels,
                const PresentationOptions& options, ImDrawData* imguiDrawData) override;
    void imguiNewFrame() override;
    void setVSync(bool enabled) override;
    bool vsyncEnabled() const override { return vsync_enabled_; }
    bool supportsLinearFiltering() const override { return true; }
    bool supportsShaderEffects() const override { return false; }
    const char* lastError() const override { return last_error_.c_str(); }
    const char* adapterName() const override { return adapter_name_.c_str(); }
    const char* backendLabel() const override { return backend_label_.c_str(); }
private:
    static constexpr int kTextureWidth = 3200;
    static constexpr int kTextureHeight = 956;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* game_texture_ = nullptr;
    std::vector<uint32_t> staging_ = std::vector<uint32_t>(static_cast<std::size_t>(kTextureWidth) * kTextureHeight);
    bool imgui_initialized_ = false;
    bool imgui_platform_initialized_ = false;
    bool vsync_enabled_ = true;
    std::string last_error_;
    std::string adapter_name_;
    std::string backend_label_ = "Fallback";
};
#endif

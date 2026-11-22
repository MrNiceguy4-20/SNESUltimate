#ifdef _WIN32
#include "sdl_fallback_renderer.hpp"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace {
const char* driverLabel(const char* driverName) {
    if (driverName == nullptr) return "SDL";
    if (std::strcmp(driverName, "direct3d12") == 0) return "Direct3D 12";
    if (std::strcmp(driverName, "direct3d11") == 0) return "Direct3D 11";
    if (std::strcmp(driverName, "direct3d") == 0) return "Direct3D 9";
    if (std::strcmp(driverName, "opengl") == 0) return "OpenGL";
    if (std::strcmp(driverName, "opengles2") == 0) return "OpenGL ES 2";
    if (std::strcmp(driverName, "vulkan") == 0) return "Vulkan (SDL)";
    if (std::strcmp(driverName, "software") == 0) return "Software";
    return driverName;
}
}
SdlFallbackRenderer::~SdlFallbackRenderer() { shutdown(); }
bool SdlFallbackRenderer::initialize(SDL_Window* window) {
    window_ = window;
    const char* requested = SDL_getenv("SNESULTIMATE_RENDERER");
    const bool openglFirst = requested != nullptr && SDL_strcasecmp(requested, "opengl") == 0;
    static const char* const kD3dFirst[] = {"direct3d12", "opengl", nullptr};
    static const char* const kOpenGlFirst[] = {"opengl", "direct3d12", nullptr};
    for (const char* driver : (openglFirst ? kOpenGlFirst : kD3dFirst)) {
        renderer_ = SDL_CreateRenderer(window_, driver);
        if (renderer_ != nullptr) break;
        fprintf(stderr, "[Renderer] %s unavailable: %s\n",
                driver ? driverLabel(driver) : "SDL default", SDL_GetError());
    }
    if (renderer_ == nullptr) {
        last_error_ = std::string("SDL_CreateRenderer failed for every fallback driver: ") + SDL_GetError();
        return false;
    }
    const char* driverName = SDL_GetRendererName(renderer_);
    backend_label_ = driverLabel(driverName);
    adapter_name_ = driverName != nullptr ? driverName : "unknown";
    SDL_SetRenderVSync(renderer_, vsync_enabled_ ? 1 : 0);
    game_texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ABGR8888,
                                      SDL_TEXTUREACCESS_STREAMING, kTextureWidth, kTextureHeight);
    if (game_texture_ == nullptr) {
        last_error_ = std::string("SDL_CreateTexture failed: ") + SDL_GetError();
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
        return false;
    }
    if (!ImGui_ImplSDL3_InitForSDLRenderer(window_, renderer_)) {
        last_error_ = "ImGui SDL3 platform backend initialization failed.";
        shutdown();
        return false;
    }
    imgui_platform_initialized_ = true;
    if (!ImGui_ImplSDLRenderer3_Init(renderer_)) {
        last_error_ = "ImGui SDL_Renderer backend initialization failed.";
        ImGui_ImplSDL3_Shutdown();
        imgui_platform_initialized_ = false;
        shutdown();
        return false;
    }
    imgui_initialized_ = true;
    return true;
}
void SdlFallbackRenderer::shutdown() {
    if (imgui_initialized_) {
        ImGui_ImplSDLRenderer3_Shutdown();
        imgui_initialized_ = false;
    }
    if (imgui_platform_initialized_) {
        ImGui_ImplSDL3_Shutdown();
        imgui_platform_initialized_ = false;
    }
    if (game_texture_ != nullptr) {
        SDL_DestroyTexture(game_texture_);
        game_texture_ = nullptr;
    }
    if (renderer_ != nullptr) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    window_ = nullptr;
}
void SdlFallbackRenderer::imguiNewFrame() { ImGui_ImplSDLRenderer3_NewFrame(); }
void SdlFallbackRenderer::setVSync(bool enabled) {
    vsync_enabled_ = enabled;
    if (renderer_ != nullptr) SDL_SetRenderVSync(renderer_, enabled ? 1 : 0);
}
bool SdlFallbackRenderer::render(const uint32_t* pixels, int sourceWidth, int sourceHeight,
                                 int sourceStridePixels,
                                 const PresentationOptions& options, ImDrawData* imguiDrawData) {
    if (renderer_ == nullptr) {
        last_error_ = "SdlFallbackRenderer::render called before initialize.";
        return false;
    }
    if ((SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED) != 0) return true;
    int outputWidth = 0;
    int outputHeight = 0;
    if (!SDL_GetRenderOutputSize(renderer_, &outputWidth, &outputHeight)) {
        last_error_ = std::string("SDL_GetRenderOutputSize failed: ") + SDL_GetError();
        return false;
    }
    if (outputWidth <= 0 || outputHeight <= 0) return true;
    if (pixels && (sourceWidth <= 0 || sourceHeight <= 0 ||
                   sourceWidth > kTextureWidth || sourceStridePixels < sourceWidth)) {
        last_error_ = "Invalid source framebuffer dimensions/stride passed to SdlFallbackRenderer::render.";
        return false;
    }
    const int copyWidth = pixels ? (sourceWidth >= 512 ? sourceWidth : sourceWidth * 2) : 0;
    const int copyHeight = pixels ? (std::min)(sourceHeight, kTextureHeight) : 0;
    const int logicalWidth = (std::max)(256, options.logicalWidth);
    const int logicalHeight = options.logicalHeight > 0 ? options.logicalHeight : copyHeight;
    if (pixels) {
        for (int y = 0; y < copyHeight; ++y) {
            uint32_t* dst = staging_.data() + static_cast<std::size_t>(y) * kTextureWidth;
            const int sourceY = y;
            const uint32_t* src = pixels + static_cast<std::ptrdiff_t>(sourceY) * sourceStridePixels;
            if (sourceWidth >= 512) {
                std::memcpy(dst, src, static_cast<std::size_t>(copyWidth) * sizeof(uint32_t));
            } else {
                for (int x = 0; x < copyWidth; ++x) dst[x] = src[x / 2];
            }
        }
        const SDL_Rect updateRect{0, 0, copyWidth, copyHeight};
        if (!SDL_UpdateTexture(game_texture_, &updateRect, staging_.data(),
                               kTextureWidth * static_cast<int>(sizeof(uint32_t)))) {
            last_error_ = std::string("SDL_UpdateTexture failed: ") + SDL_GetError();
            return false;
        }
    }
    SDL_SetRenderDrawColor(renderer_, 8, 10, 16, 255);
    SDL_RenderClear(renderer_);
    if (pixels && copyWidth > 0 && copyHeight > 0) {
        const int contentWidth = (std::max)(0, outputWidth);
        if (contentWidth > 0) {
            int drawWidth = contentWidth;
            int drawHeight = outputHeight;
            if (options.aspect != AspectMode::Stretch) {
                double aspect = (4.0 / 3.0) * (static_cast<double>(logicalWidth) / 256.0);
                if (options.aspect == AspectMode::Snes8x7) {
                    aspect = (static_cast<double>(logicalWidth) * (8.0 / 7.0)) /
                             static_cast<double>((std::max)(logicalHeight, 1));
                }
                drawWidth = contentWidth;
                drawHeight = static_cast<int>(static_cast<double>(drawWidth) / aspect + 0.5);
                if (drawHeight > outputHeight) {
                    drawHeight = outputHeight;
                    drawWidth = static_cast<int>(static_cast<double>(drawHeight) * aspect + 0.5);
                }
                if (options.integerScaling && logicalHeight > 0) {
                    const int maxScaleY = outputHeight / logicalHeight;
                    if (maxScaleY >= 1) {
                        int scaleY = maxScaleY;
                        int candidateHeight = logicalHeight * scaleY;
                        int candidateWidth = static_cast<int>(candidateHeight * aspect + 0.5);
                        while (scaleY > 1 && candidateWidth > contentWidth) {
                            --scaleY;
                            candidateHeight = logicalHeight * scaleY;
                            candidateWidth = static_cast<int>(candidateHeight * aspect + 0.5);
                        }
                        if (candidateWidth <= contentWidth) {
                            drawHeight = candidateHeight;
                            drawWidth = candidateWidth;
                        }
                    }
                }
            }
            const int presentWidth = (std::max)(1, drawWidth);
            const int presentHeight = (std::max)(1, drawHeight);
            const int presentX = (contentWidth - presentWidth) / 2;
            const int presentY = (outputHeight - presentHeight) / 2;
            SDL_SetTextureScaleMode(game_texture_,
                                    options.filter != FilterMode::Nearest
                                        ? SDL_SCALEMODE_LINEAR
                                        : SDL_SCALEMODE_NEAREST);
            SDL_FRect dst{static_cast<float>(presentX), static_cast<float>(presentY),
                          static_cast<float>(presentWidth), static_cast<float>(presentHeight)};
            const SDL_FRect src{0.0f, 0.0f, static_cast<float>(copyWidth), static_cast<float>(copyHeight)};
            SDL_RenderTexture(renderer_, game_texture_, &src, &dst);
        }
    }
    if (imguiDrawData) ImGui_ImplSDLRenderer3_RenderDrawData(imguiDrawData, renderer_);
    if (!SDL_RenderPresent(renderer_)) {
        last_error_ = std::string("SDL_RenderPresent failed: ") + SDL_GetError();
        return false;
    }
    return true;
}
#endif

#pragma once
#ifdef _WIN32
#include <cstdint>
struct SDL_Window;
struct ImDrawData;
class RendererBackend {
public:
    enum class FilterMode {
        Nearest,
        Linear,
        SharpBilinear,
        Bicubic,
    };
    enum class AspectMode {
        Corrected4x3,
        Snes8x7,
        Stretch,
    };
    struct PresentationOptions {
        FilterMode filter = FilterMode::Linear;
        AspectMode aspect = AspectMode::Corrected4x3;
        bool integerScaling = false;
        bool pseudoHiresBlend = false;
        float scanlineStrength = 0.0f;
        float sharpenStrength = 0.0f;
        float crtMaskStrength = 0.0f;
        float gamma = 1.0f;
        float saturation = 1.0f;
        float contrast = 1.0f;
        float brightness = 0.0f;
        float curvature = 0.0f;
        float vignette = 0.0f;
        int crtMaskStyle = 0;
        float overscan = 0.0f;
        float temperature = 0.0f;
        float compositeBlur = 0.0f;
        int colorProfile = 0;
        int logicalWidth = 256;
        int logicalHeight = 0;
    };
    virtual ~RendererBackend() = default;
    virtual bool initialize(SDL_Window* window) = 0;
    virtual void shutdown() = 0;
    virtual bool render(const uint32_t* pixels, int sourceWidth, int sourceHeight,
                        int sourceStridePixels,
                        const PresentationOptions& options, ImDrawData* imguiDrawData) = 0;
    virtual void imguiNewFrame() = 0;
    virtual void setVSync(bool enabled) = 0;
    virtual bool vsyncEnabled() const = 0;
    virtual bool supportsLinearFiltering() const = 0;
    virtual bool supportsShaderEffects() const = 0;
    virtual const char* lastError() const = 0;
    virtual const char* adapterName() const = 0;
    virtual const char* backendLabel() const = 0;
};
#endif

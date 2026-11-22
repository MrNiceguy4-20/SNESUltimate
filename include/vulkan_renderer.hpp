#pragma once
#ifdef _WIN32
#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>
#include "imgui_impl_vulkan.h"
#include "renderer_backend.hpp"
#include <cstdint>
#include <string>
#include <vector>
struct ImDrawData;
class VulkanRenderer final : public RendererBackend {
public:
    VulkanRenderer() = default;
    ~VulkanRenderer() override;
    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;
    bool initialize(SDL_Window* window) override;
    bool initializeGuarded(SDL_Window* window);
    void shutdown() override;
    bool render(const uint32_t* pixels, int sourceWidth, int sourceHeight,
                int sourceStridePixels,
                const PresentationOptions& options, ImDrawData* imguiDrawData) override;
    void imguiNewFrame() override { ImGui_ImplVulkan_NewFrame(); }
    void setVSync(bool enabled) override;
    bool vsyncEnabled() const override { return vsync_enabled_; }
    bool supportsLinearFiltering() const override { return linear_filter_supported_; }
    bool supportsShaderEffects() const override { return true; }
    const char* lastError() const override { return last_error_.c_str(); }
    const char* adapterName() const override { return adapter_name_.c_str(); }
    const char* backendLabel() const override { return "Vulkan 1.4"; }
    uint32_t apiVersion() const { return api_version_; }
private:
    static constexpr int kTextureWidth = 3200;
    static constexpr int kTextureHeight = 956;
    static constexpr VkFormat kGameFormat = VK_FORMAT_R8G8B8A8_UNORM;
    struct UploadFrame {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        uint8_t* mapped = nullptr;
        bool host_coherent = true;
    };
    bool createInstance();
    bool createSurface();
    bool selectPhysicalDevice();
    bool createDevice();
    bool createOrResizeSwapchain(int width, int height);
    bool createGameImage();
    bool createUploadFrames();
    bool createPresentationResources();
    bool createPresentationPipeline();
    bool initializeImGui();
    bool validateBlitSupport();
    bool resizeIfNeeded();
    void destroyUploadFrames();
    void destroyPresentationPipeline();
    void destroyPresentationResources();
    void destroyGameImage();
    void destroySwapchain();
    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags required) const;
    bool setVkError(const char* stage, VkResult result);
    bool checkVk(const char* stage, VkResult result);
    void transitionImage(VkCommandBuffer cmd, VkImage image,
                         VkImageLayout oldLayout, VkImageLayout newLayout,
                         VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
                         VkAccessFlags srcAccess, VkAccessFlags dstAccess) const;
    SDL_Window* window_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queue_family_ = UINT32_MAX;
    uint32_t api_version_ = VK_API_VERSION_1_4;
    ImGui_ImplVulkanH_Window window_data_{};
    uint32_t min_image_count_ = 2;
    bool swapchain_rebuild_ = false;
    bool swapchain_configured_ = false;
    bool imgui_initialized_ = false;
    bool vsync_enabled_ = true;
    bool linear_filter_supported_ = false;
    VkImage game_image_ = VK_NULL_HANDLE;
    VkDeviceMemory game_image_memory_ = VK_NULL_HANDLE;
    VkImageView game_image_view_ = VK_NULL_HANDLE;
    VkImageLayout game_image_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    VkSampler game_sampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout presentation_descriptor_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool presentation_descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet presentation_descriptor_set_ = VK_NULL_HANDLE;
    VkPipelineLayout presentation_pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline presentation_pipeline_ = VK_NULL_HANDLE;
    std::vector<UploadFrame> upload_frames_;
    std::string last_error_;
    std::string adapter_name_;
    const char* init_stage_ = "";
    bool driver_crashed_ = false;
};
#endif

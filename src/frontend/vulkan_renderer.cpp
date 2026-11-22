#include "vulkan_renderer.hpp"
#include "embedded_shaders.hpp"
#ifdef _WIN32
#include "imgui.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <SDL3/SDL_vulkan.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <iterator>
#include <sstream>
#include <vector>
#include <fstream>
#include <filesystem>
namespace {
void ImGuiCheckVkResult(VkResult result) {
    if (result == VK_SUCCESS) return;
    std::fprintf(stderr, "[Vulkan/ImGui] VkResult = %d\n", static_cast<int>(result));
    if (result < 0) std::abort();
}
bool HasExtension(const std::vector<VkExtensionProperties>& properties, const char* name) {
    return std::any_of(properties.begin(), properties.end(), [name](const VkExtensionProperties& p) {
        return std::strcmp(p.extensionName, name) == 0;
    });
}
std::string VulkanVersionString(uint32_t version) {
    std::ostringstream out;
    out << VK_VERSION_MAJOR(version) << '.' << VK_VERSION_MINOR(version) << '.' << VK_VERSION_PATCH(version);
    return out.str();
}
}
VulkanRenderer::~VulkanRenderer() {
    shutdown();
}
bool VulkanRenderer::setVkError(const char* stage, VkResult result) {
    std::ostringstream out;
    out << stage << " failed with VkResult " << static_cast<int>(result);
    last_error_ = out.str();
    return false;
}
bool VulkanRenderer::checkVk(const char* stage, VkResult result) {
    return result == VK_SUCCESS ? true : setVkError(stage, result);
}
uint32_t VulkanRenderer::findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags required) const {
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(physical_device_, &memory);
    for (uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
        if ((typeBits & (uint32_t{1} << i)) != 0 &&
            (memory.memoryTypes[i].propertyFlags & required) == required) {
            return i;
        }
    }
    return UINT32_MAX;
}
void VulkanRenderer::transitionImage(VkCommandBuffer cmd, VkImage image,
                                     VkImageLayout oldLayout, VkImageLayout newLayout,
                                     VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
                                     VkAccessFlags srcAccess, VkAccessFlags dstAccess) const {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}
bool VulkanRenderer::createInstance() {
    uint32_t loaderVersion = VK_API_VERSION_1_0;
    auto enumerateVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
        vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));
    if (enumerateVersion) {
        const VkResult versionResult = enumerateVersion(&loaderVersion);
        if (versionResult != VK_SUCCESS) return setVkError("vkEnumerateInstanceVersion", versionResult);
    }
    if (loaderVersion < VK_API_VERSION_1_4) {
        last_error_ = "The installed Vulkan loader exposes " + VulkanVersionString(loaderVersion) +
                      ", but SNESUltimate is configured to require Vulkan 1.4.";
        return false;
    }
    uint32_t extensionCount = 0;
    if (vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr) != VK_SUCCESS) {
        last_error_ = "Could not enumerate Vulkan instance extensions.";
        return false;
    }
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    if (extensionCount != 0 &&
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, availableExtensions.data()) != VK_SUCCESS) {
        last_error_ = "Could not read Vulkan instance extension properties.";
        return false;
    }
    uint32_t sdlExtensionCount = 0;
    const char* const* sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&sdlExtensionCount);
    if (!sdlExtensions || sdlExtensionCount == 0) {
        last_error_ = std::string("SDL_Vulkan_GetInstanceExtensions failed: ") + SDL_GetError();
        return false;
    }
    std::vector<const char*> enabledExtensions(sdlExtensions, sdlExtensions + sdlExtensionCount);
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "SNESUltimate";
    app.applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
    app.pEngineName = "SNESUltimate Vulkan Renderer";
    app.engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
    app.apiVersion = api_version_;
    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
    info.ppEnabledExtensionNames = enabledExtensions.data();
    const VkResult result = vkCreateInstance(&info, nullptr, &instance_);
    if (result != VK_SUCCESS) return setVkError("vkCreateInstance (Vulkan 1.4)", result);
    return true;
}
bool VulkanRenderer::createSurface() {
    if (!SDL_Vulkan_CreateSurface(window_, instance_, nullptr, &surface_)) {
        last_error_ = std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError();
        return false;
    }
    window_data_.Surface = surface_;
    return true;
}
bool VulkanRenderer::selectPhysicalDevice() {
    uint32_t deviceCount = 0;
    VkResult result = vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr);
    if (result != VK_SUCCESS) return setVkError("vkEnumeratePhysicalDevices", result);
    if (deviceCount == 0) {
        last_error_ = "No Vulkan-capable GPU was reported by the Vulkan loader.";
        return false;
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    result = vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data());
    if (result != VK_SUCCESS) return setVkError("vkEnumeratePhysicalDevices", result);
    int bestScore = std::numeric_limits<int>::min();
    VkPhysicalDevice best = VK_NULL_HANDLE;
    uint32_t bestQueueFamily = UINT32_MAX;
    VkPhysicalDeviceProperties bestProperties{};
    for (VkPhysicalDevice candidate : devices) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(candidate, &properties);
        if (properties.apiVersion < VK_API_VERSION_1_4) continue;
        uint32_t deviceExtensionCount = 0;
        if (vkEnumerateDeviceExtensionProperties(candidate, nullptr, &deviceExtensionCount, nullptr) != VK_SUCCESS) continue;
        std::vector<VkExtensionProperties> deviceExtensions(deviceExtensionCount);
        if (deviceExtensionCount != 0 &&
            vkEnumerateDeviceExtensionProperties(candidate, nullptr, &deviceExtensionCount, deviceExtensions.data()) != VK_SUCCESS) continue;
        if (!HasExtension(deviceExtensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) continue;
        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
        uint32_t presentGraphicsFamily = UINT32_MAX;
        for (uint32_t i = 0; i < familyCount; ++i) {
            if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0 || families[i].queueCount == 0) continue;
            VkBool32 present = VK_FALSE;
            if (vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface_, &present) == VK_SUCCESS && present) {
                presentGraphicsFamily = i;
                break;
            }
        }
        if (presentGraphicsFamily == UINT32_MAX) continue;
        uint32_t formatCount = 0;
        uint32_t presentModeCount = 0;
        if (vkGetPhysicalDeviceSurfaceFormatsKHR(candidate, surface_, &formatCount, nullptr) != VK_SUCCESS || formatCount == 0) continue;
        if (vkGetPhysicalDeviceSurfacePresentModesKHR(candidate, surface_, &presentModeCount, nullptr) != VK_SUCCESS || presentModeCount == 0) continue;
        int score = static_cast<int>(properties.limits.maxImageDimension2D / 1024);
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score += 10000;
        else if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score += 5000;
        if (score > bestScore) {
            bestScore = score;
            best = candidate;
            bestQueueFamily = presentGraphicsFamily;
            bestProperties = properties;
        }
    }
    if (best == VK_NULL_HANDLE) {
        last_error_ = "No GPU/driver combination supports Vulkan 1.4, VK_KHR_swapchain, and a graphics queue capable of presenting to this SDL window.";
        return false;
    }
    physical_device_ = best;
    queue_family_ = bestQueueFamily;
    adapter_name_ = bestProperties.deviceName;
    return true;
}
bool VulkanRenderer::createDevice() {
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = queue_family_;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queueInfo;
    info.enabledExtensionCount = 1;
    info.ppEnabledExtensionNames = extensions;
    const VkResult result = vkCreateDevice(physical_device_, &info, nullptr, &device_);
    if (result != VK_SUCCESS) return setVkError("vkCreateDevice", result);
    vkGetDeviceQueue(device_, queue_family_, 0, &queue_);
    if (queue_ == VK_NULL_HANDLE) {
        last_error_ = "vkGetDeviceQueue returned a null graphics queue.";
        return false;
    }
    return true;
}
bool VulkanRenderer::createOrResizeSwapchain(int width, int height) {
    if (width <= 0 || height <= 0) return true;
    VkSurfaceCapabilitiesKHR capabilities{};
    VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_, &capabilities);
    if (result != VK_SUCCESS) return setVkError("vkGetPhysicalDeviceSurfaceCapabilitiesKHR", result);
    const VkImageUsageFlags requiredSwapchainUsage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if ((capabilities.supportedUsageFlags & requiredSwapchainUsage) != requiredSwapchainUsage) {
        last_error_ = "The selected presentation surface does not support the COLOR_ATTACHMENT + TRANSFER_DST usage required by the Vulkan frontend.";
        return false;
    }
    if (!swapchain_configured_) {
        const VkFormat formats[] = {
            VK_FORMAT_B8G8R8A8_UNORM,
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_FORMAT_B8G8R8A8_SRGB,
            VK_FORMAT_R8G8B8A8_SRGB,
        };
        window_data_.SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(
            physical_device_, surface_, formats, static_cast<int>(std::size(formats)),
            VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
        if (vsync_enabled_) {
            const VkPresentModeKHR modes[] = {VK_PRESENT_MODE_FIFO_KHR};
            window_data_.PresentMode = ImGui_ImplVulkanH_SelectPresentMode(
                physical_device_, surface_, modes, static_cast<int>(std::size(modes)));
        } else {
            const VkPresentModeKHR modes[] = {
                VK_PRESENT_MODE_IMMEDIATE_KHR,
                VK_PRESENT_MODE_MAILBOX_KHR,
                VK_PRESENT_MODE_FIFO_KHR,
            };
            window_data_.PresentMode = ImGui_ImplVulkanH_SelectPresentMode(
                physical_device_, surface_, modes, static_cast<int>(std::size(modes)));
        }
        min_image_count_ = static_cast<uint32_t>(
            (std::max)(2, ImGui_ImplVulkanH_GetMinImageCountFromPresentMode(window_data_.PresentMode)));
        swapchain_configured_ = true;
    }
    window_data_.UseDynamicRendering = false;
    window_data_.AttachmentDesc.format = window_data_.SurfaceFormat.format;
    window_data_.AttachmentDesc.samples = VK_SAMPLE_COUNT_1_BIT;
    window_data_.AttachmentDesc.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    window_data_.AttachmentDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    window_data_.AttachmentDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    window_data_.AttachmentDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    window_data_.AttachmentDesc.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    window_data_.AttachmentDesc.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    if (imgui_initialized_) ImGui_ImplVulkan_SetMinImageCount(min_image_count_);
    if (presentation_pipeline_ != VK_NULL_HANDLE) destroyPresentationPipeline();
    ImGui_ImplVulkanH_CreateOrResizeWindow(
        instance_, physical_device_, device_, &window_data_, queue_family_, nullptr,
        width, height, min_image_count_, VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    window_data_.FrameIndex = 0;
    window_data_.SemaphoreIndex = 0;
    if (window_data_.Swapchain == VK_NULL_HANDLE || window_data_.ImageCount == 0) {
        last_error_ = "Vulkan swapchain creation returned no usable images.";
        return false;
    }
    destroyUploadFrames();
    if (!createUploadFrames()) return false;
    if (presentation_pipeline_layout_ != VK_NULL_HANDLE && !createPresentationPipeline()) return false;
    swapchain_rebuild_ = false;
    return true;
}
bool VulkanRenderer::validateBlitSupport() {
    VkFormatProperties source{};
    vkGetPhysicalDeviceFormatProperties(physical_device_, kGameFormat, &source);
    if ((source.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) == 0) {
        last_error_ = "VK_FORMAT_R8G8B8A8_UNORM cannot be sampled by the selected Vulkan GPU.";
        return false;
    }
    linear_filter_supported_ = true;
    return true;
}
bool VulkanRenderer::createGameImage() {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = kGameFormat;
    imageInfo.extent = {kTextureWidth, kTextureHeight, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkResult result = vkCreateImage(device_, &imageInfo, nullptr, &game_image_);
    if (result != VK_SUCCESS) return setVkError("vkCreateImage(game framebuffer)", result);
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device_, game_image_, &requirements);
    const uint32_t memoryType = findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memoryType == UINT32_MAX) {
        last_error_ = "No device-local Vulkan memory type is available for the game framebuffer image.";
        return false;
    }
    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = requirements.size;
    alloc.memoryTypeIndex = memoryType;
    result = vkAllocateMemory(device_, &alloc, nullptr, &game_image_memory_);
    if (result != VK_SUCCESS) return setVkError("vkAllocateMemory(game framebuffer)", result);
    result = vkBindImageMemory(device_, game_image_, game_image_memory_, 0);
    if (result != VK_SUCCESS) return setVkError("vkBindImageMemory(game framebuffer)", result);
    VkImageViewCreateInfo view{};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = game_image_;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = kGameFormat;
    view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view.subresourceRange.baseMipLevel = 0;
    view.subresourceRange.levelCount = 1;
    view.subresourceRange.baseArrayLayer = 0;
    view.subresourceRange.layerCount = 1;
    result = vkCreateImageView(device_, &view, nullptr, &game_image_view_);
    if (result != VK_SUCCESS) return setVkError("vkCreateImageView(game framebuffer)", result);
    game_image_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    return true;
}
bool VulkanRenderer::createUploadFrames() {
    if (window_data_.ImageCount == 0) return true;
    const VkDeviceSize uploadBytes = static_cast<VkDeviceSize>(kTextureWidth) * kTextureHeight * sizeof(uint32_t);
    upload_frames_.resize(window_data_.ImageCount);
    for (UploadFrame& upload : upload_frames_) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = uploadBytes;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkResult result = vkCreateBuffer(device_, &bufferInfo, nullptr, &upload.buffer);
        if (result != VK_SUCCESS) return setVkError("vkCreateBuffer(framebuffer upload)", result);
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device_, upload.buffer, &requirements);
        uint32_t memoryType = findMemoryType(
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        upload.host_coherent = (memoryType != UINT32_MAX);
        if (memoryType == UINT32_MAX) {
            memoryType = findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        }
        if (memoryType == UINT32_MAX) {
            last_error_ = "No HOST_VISIBLE Vulkan memory type is available for framebuffer uploads.";
            return false;
        }
        VkMemoryAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = requirements.size;
        alloc.memoryTypeIndex = memoryType;
        result = vkAllocateMemory(device_, &alloc, nullptr, &upload.memory);
        if (result != VK_SUCCESS) return setVkError("vkAllocateMemory(framebuffer upload)", result);
        result = vkBindBufferMemory(device_, upload.buffer, upload.memory, 0);
        if (result != VK_SUCCESS) return setVkError("vkBindBufferMemory(framebuffer upload)", result);
        result = vkMapMemory(device_, upload.memory, 0, VK_WHOLE_SIZE, 0, reinterpret_cast<void**>(&upload.mapped));
        if (result != VK_SUCCESS) return setVkError("vkMapMemory(framebuffer upload)", result);
    }
    return true;
}
bool VulkanRenderer::createPresentationPipeline() {
    destroyPresentationPipeline();
    if (presentation_pipeline_layout_ == VK_NULL_HANDLE || window_data_.RenderPass == VK_NULL_HANDLE) {
        last_error_ = "Vulkan presentation pipeline prerequisites are missing.";
        return false;
    }
    auto createModule = [&](const uint32_t* words, std::size_t wordCount, VkShaderModule& module, const char* stage) -> bool {
        VkShaderModuleCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = wordCount * sizeof(uint32_t);
        info.pCode = words;
        const VkResult result = vkCreateShaderModule(device_, &info, nullptr, &module);
        return result == VK_SUCCESS ? true : setVkError(stage, result);
    };
    VkShaderModule vert = VK_NULL_HANDLE;
    VkShaderModule frag = VK_NULL_HANDLE;
    if (!createModule(kPresentationVertSpv, kPresentationVertSpvWords, vert, "vkCreateShaderModule(presentation vertex)")) return false;
    if (!createModule(kPresentationFragSpv, kPresentationFragSpvWords, frag, "vkCreateShaderModule(presentation fragment)")) {
        vkDestroyShaderModule(device_, vert, nullptr);
        return false;
    }
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;
    const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = static_cast<uint32_t>(std::size(dynamicStates));
    dynamic.pDynamicStates = dynamicStates;
    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = presentation_pipeline_layout_;
    info.renderPass = window_data_.RenderPass;
    info.subpass = 0;
    const VkResult result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &presentation_pipeline_);
    vkDestroyShaderModule(device_, frag, nullptr);
    vkDestroyShaderModule(device_, vert, nullptr);
    if (result != VK_SUCCESS) return setVkError("vkCreateGraphicsPipelines(presentation)", result);
    return true;
}
bool VulkanRenderer::createPresentationResources() {
    VkSamplerCreateInfo sampler{};
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter = VK_FILTER_NEAREST;
    sampler.minFilter = VK_FILTER_NEAREST;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.maxLod = 0.0f;
    VkResult result = vkCreateSampler(device_, &sampler, nullptr, &game_sampler_);
    if (result != VK_SUCCESS) return setVkError("vkCreateSampler(game framebuffer)", result);
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    result = vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &presentation_descriptor_layout_);
    if (result != VK_SUCCESS) return setVkError("vkCreateDescriptorSetLayout(presentation)", result);
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    result = vkCreateDescriptorPool(device_, &poolInfo, nullptr, &presentation_descriptor_pool_);
    if (result != VK_SUCCESS) return setVkError("vkCreateDescriptorPool(presentation)", result);
    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool = presentation_descriptor_pool_;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &presentation_descriptor_layout_;
    result = vkAllocateDescriptorSets(device_, &alloc, &presentation_descriptor_set_);
    if (result != VK_SUCCESS) return setVkError("vkAllocateDescriptorSets(presentation)", result);
    VkDescriptorImageInfo image{};
    image.sampler = game_sampler_;
    image.imageView = game_image_view_;
    image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = presentation_descriptor_set_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    struct PushConstants {
        float sourceSize[2];
        float scanlineStrength;
        float sharpenStrength;
        float maskStrength;
        uint32_t flags;
        float gamma;
        float saturation;
        float contrast;
        float brightness;
        float curvature;
        float vignette;
        int32_t maskStyle;
        float overscan;
        float temperature;
        float outScale[2];
        float compositeBlur;
        int32_t scaleMode;
        int32_t colorProfile;
    };
    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    push.offset = 0;
    push.size = sizeof(PushConstants);
    VkPipelineLayoutCreateInfo pipelineLayout{};
    pipelineLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayout.setLayoutCount = 1;
    pipelineLayout.pSetLayouts = &presentation_descriptor_layout_;
    pipelineLayout.pushConstantRangeCount = 1;
    pipelineLayout.pPushConstantRanges = &push;
    result = vkCreatePipelineLayout(device_, &pipelineLayout, nullptr, &presentation_pipeline_layout_);
    if (result != VK_SUCCESS) return setVkError("vkCreatePipelineLayout(presentation)", result);
    return createPresentationPipeline();
}
bool VulkanRenderer::initializeImGui() {
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = api_version_;
    info.Instance = instance_;
    info.PhysicalDevice = physical_device_;
    info.Device = device_;
    info.QueueFamily = queue_family_;
    info.Queue = queue_;
    info.DescriptorPoolSize = 64;
    info.MinImageCount = min_image_count_;
    info.ImageCount = window_data_.ImageCount;
    info.PipelineInfoMain.RenderPass = window_data_.RenderPass;
    info.PipelineInfoMain.Subpass = 0;
    info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    info.CheckVkResultFn = ImGuiCheckVkResult;
    info.MinAllocationSize = 1024 * 1024;
    if (!ImGui_ImplVulkan_Init(&info)) {
        last_error_ = "ImGui_ImplVulkan_Init failed.";
        return false;
    }
    imgui_initialized_ = true;
    return true;
}
void VulkanRenderer::setVSync(bool enabled) {
    if (vsync_enabled_ == enabled) return;
    vsync_enabled_ = enabled;
    swapchain_configured_ = false;
    if (window_ != nullptr && device_ != VK_NULL_HANDLE) swapchain_rebuild_ = true;
}
bool VulkanRenderer::initialize(SDL_Window* window) {
    window_ = window;
    last_error_.clear();
    if (!window_) {
        last_error_ = "VulkanRenderer::initialize received a null SDL window.";
        return false;
    }
    init_stage_ = "instance creation";
    if (!createInstance()) return false;
    init_stage_ = "surface creation";
    if (!createSurface()) return false;
    init_stage_ = "physical device selection";
    if (!selectPhysicalDevice()) return false;
    init_stage_ = "logical device creation";
    if (!createDevice()) return false;
    int width = 0, height = 0;
    SDL_GetWindowSizeInPixels(window_, &width, &height);
    width = (std::max)(width, 1);
    height = (std::max)(height, 1);
    init_stage_ = "swapchain creation";
    if (!createOrResizeSwapchain(width, height)) return false;
    init_stage_ = "blit validation";
    if (!validateBlitSupport()) return false;
    init_stage_ = "game image creation";
    if (!createGameImage()) return false;
    init_stage_ = "presentation resources";
    if (!createPresentationResources()) return false;
    init_stage_ = "ImGui Vulkan backend";
    if (!initializeImGui()) return false;
    init_stage_ = "";
    return true;
}
namespace {
bool runVulkanInitGuarded(VulkanRenderer* renderer, SDL_Window* window, unsigned long* exceptionCode) {
    __try {
        return renderer->initialize(window);
    } __except (*exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
}
bool VulkanRenderer::initializeGuarded(SDL_Window* window) {
    unsigned long exceptionCode = 0;
    if (runVulkanInitGuarded(this, window, &exceptionCode)) return true;
    if (exceptionCode != 0) {
        driver_crashed_ = true;
        char message[256];
        std::snprintf(message, sizeof(message),
                      "The Vulkan driver crashed (exception 0x%08lX) during %s.",
                      exceptionCode, init_stage_[0] ? init_stage_ : "initialization");
        last_error_ = message;
    }
    return false;
}
bool VulkanRenderer::resizeIfNeeded() {
    if (!window_) return false;
    if ((SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED) != 0) return true;
    int width = 0, height = 0;
    SDL_GetWindowSizeInPixels(window_, &width, &height);
    if (width <= 0 || height <= 0) return true;
    if (!swapchain_rebuild_ && width == window_data_.Width && height == window_data_.Height) return true;
    return createOrResizeSwapchain(width, height);
}
bool VulkanRenderer::render(const uint32_t* pixels, int sourceWidth, int sourceHeight,
                            int sourceStridePixels,
                            const PresentationOptions& options, ImDrawData* imguiDrawData) {
    if (!resizeIfNeeded()) return false;
    if ((SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED) != 0 ||
        window_data_.Width <= 0 || window_data_.Height <= 0) {
        return true;
    }
    if (pixels && (sourceWidth <= 0 || sourceHeight <= 0 || sourceWidth > kTextureWidth || sourceStridePixels < sourceWidth)) {
        last_error_ = "Invalid source framebuffer dimensions/stride passed to VulkanRenderer::render.";
        return false;
    }
    const int copyWidth = pixels ? (sourceWidth >= 512 ? sourceWidth : sourceWidth * 2) : 0;
    const int copyHeight = pixels ? (std::min)(sourceHeight, kTextureHeight) : 0;
    const int logicalWidth = (std::max)(256, options.logicalWidth);
    const int logicalHeight = options.logicalHeight > 0 ? options.logicalHeight : copyHeight;
    VkSemaphore imageAcquired = window_data_.FrameSemaphores[window_data_.SemaphoreIndex].ImageAcquiredSemaphore;
    VkSemaphore renderComplete = window_data_.FrameSemaphores[window_data_.SemaphoreIndex].RenderCompleteSemaphore;
    VkResult result = vkAcquireNextImageKHR(
        device_, window_data_.Swapchain, UINT64_MAX, imageAcquired, VK_NULL_HANDLE,
        &window_data_.FrameIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        swapchain_rebuild_ = true;
        return true;
    }
    if (result == VK_SUBOPTIMAL_KHR) {
        swapchain_rebuild_ = true;
    } else if (result != VK_SUCCESS) {
        return setVkError("vkAcquireNextImageKHR", result);
    }
    if (window_data_.FrameIndex >= upload_frames_.size()) {
        last_error_ = "Swapchain frame index exceeds the allocated upload-frame count.";
        return false;
    }
    ImGui_ImplVulkanH_Frame& frame = window_data_.Frames[window_data_.FrameIndex];
    UploadFrame& upload = upload_frames_[window_data_.FrameIndex];
    std::vector<VkFence> frameFences;
    frameFences.reserve(window_data_.ImageCount);
    for (uint32_t i = 0; i < window_data_.ImageCount; ++i) {
        if (window_data_.Frames[i].Fence != VK_NULL_HANDLE) frameFences.push_back(window_data_.Frames[i].Fence);
    }
    result = vkWaitForFences(device_, static_cast<uint32_t>(frameFences.size()), frameFences.data(), VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS) return setVkError("vkWaitForFences", result);
    result = vkResetFences(device_, 1, &frame.Fence);
    if (result != VK_SUCCESS) return setVkError("vkResetFences", result);
    if (pixels && copyWidth > 0 && copyHeight > 0) {
        const std::size_t packedPitch = static_cast<std::size_t>(copyWidth) * sizeof(uint32_t);
        for (int y = 0; y < copyHeight; ++y) {
            uint32_t* dst = reinterpret_cast<uint32_t*>(upload.mapped + static_cast<std::size_t>(y) * packedPitch);
            const int sourceY = y;
            const uint32_t* src = pixels + static_cast<std::ptrdiff_t>(sourceY) * sourceStridePixels;
            if (sourceWidth >= 512) {
                std::memcpy(dst, src, packedPitch);
            } else {
                for (int x = 0; x < copyWidth; ++x) dst[x] = src[x / 2];
            }
        }
        if (!upload.host_coherent) {
            VkMappedMemoryRange flush{};
            flush.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            flush.memory = upload.memory;
            flush.offset = 0;
            flush.size = VK_WHOLE_SIZE;
            result = vkFlushMappedMemoryRanges(device_, 1, &flush);
            if (result != VK_SUCCESS) return setVkError("vkFlushMappedMemoryRanges(framebuffer upload)", result);
        }
    }
    result = vkResetCommandPool(device_, frame.CommandPool, 0);
    if (result != VK_SUCCESS) return setVkError("vkResetCommandPool", result);
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = vkBeginCommandBuffer(frame.CommandBuffer, &begin);
    if (result != VK_SUCCESS) return setVkError("vkBeginCommandBuffer", result);
    transitionImage(frame.CommandBuffer, frame.Backbuffer,
                    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0, VK_ACCESS_TRANSFER_WRITE_BIT);
    VkClearColorValue clear{};
    clear.float32[0] = 8.0f / 255.0f;
    clear.float32[1] = 10.0f / 255.0f;
    clear.float32[2] = 16.0f / 255.0f;
    clear.float32[3] = 1.0f;
    VkImageSubresourceRange fullColor{};
    fullColor.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    fullColor.baseMipLevel = 0;
    fullColor.levelCount = 1;
    fullColor.baseArrayLayer = 0;
    fullColor.layerCount = 1;
    vkCmdClearColorImage(frame.CommandBuffer, frame.Backbuffer,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &fullColor);
    int presentX = 0;
    int presentY = 0;
    int presentWidth = 0;
    int presentHeight = 0;
    bool drawGame = false;
    if (pixels && copyWidth > 0 && copyHeight > 0) {
        const VkImageLayout oldGameLayout = game_image_layout_;
        transitionImage(frame.CommandBuffer, game_image_, oldGameLayout,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        oldGameLayout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        oldGameLayout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_SHADER_READ_BIT,
                        VK_ACCESS_TRANSFER_WRITE_BIT);
        VkBufferImageCopy copy{};
        copy.bufferOffset = 0;
        copy.bufferRowLength = 0;
        copy.bufferImageHeight = 0;
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.mipLevel = 0;
        copy.imageSubresource.baseArrayLayer = 0;
        copy.imageSubresource.layerCount = 1;
        copy.imageOffset = {0, 0, 0};
        copy.imageExtent = {static_cast<uint32_t>(copyWidth), static_cast<uint32_t>(copyHeight), 1};
        vkCmdCopyBufferToImage(frame.CommandBuffer, upload.buffer, game_image_,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        transitionImage(frame.CommandBuffer, game_image_,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
        game_image_layout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        const int contentWidth = (std::max)(0, window_data_.Width);
        if (contentWidth > 0) {
            int drawWidth = contentWidth;
            int drawHeight = window_data_.Height;
            if (options.aspect != AspectMode::Stretch) {
                double aspect = (4.0 / 3.0) * (static_cast<double>(logicalWidth) / 256.0);
                if (options.aspect == AspectMode::Snes8x7) {
                    aspect = (static_cast<double>(logicalWidth) * (8.0 / 7.0)) /
                             static_cast<double>((std::max)(logicalHeight, 1));
                }
                drawWidth = contentWidth;
                drawHeight = static_cast<int>(static_cast<double>(drawWidth) / aspect + 0.5);
                if (drawHeight > window_data_.Height) {
                    drawHeight = window_data_.Height;
                    drawWidth = static_cast<int>(static_cast<double>(drawHeight) * aspect + 0.5);
                }
                if (options.integerScaling && logicalHeight > 0) {
                    const int maxScaleY = window_data_.Height / logicalHeight;
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
            presentWidth = (std::max)(1, drawWidth);
            presentHeight = (std::max)(1, drawHeight);
            presentX = (contentWidth - presentWidth) / 2;
            presentY = (window_data_.Height - presentHeight) / 2;
            drawGame = true;
        }
    }
    transitionImage(frame.CommandBuffer, frame.Backbuffer,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    VkRenderPassBeginInfo renderPass{};
    renderPass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPass.renderPass = window_data_.RenderPass;
    renderPass.framebuffer = frame.Framebuffer;
    renderPass.renderArea.offset = {0, 0};
    renderPass.renderArea.extent = {
        static_cast<uint32_t>(window_data_.Width),
        static_cast<uint32_t>(window_data_.Height)};
    renderPass.clearValueCount = 0;
    renderPass.pClearValues = nullptr;
    vkCmdBeginRenderPass(frame.CommandBuffer, &renderPass, VK_SUBPASS_CONTENTS_INLINE);
    if (drawGame && presentation_pipeline_ != VK_NULL_HANDLE) {
        VkViewport viewport{};
        viewport.x = static_cast<float>(presentX);
        viewport.y = static_cast<float>(presentY);
        viewport.width = static_cast<float>(presentWidth);
        viewport.height = static_cast<float>(presentHeight);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        VkRect2D scissor{};
        scissor.offset = {presentX, presentY};
        scissor.extent = {static_cast<uint32_t>(presentWidth), static_cast<uint32_t>(presentHeight)};
        struct PushConstants {
            float sourceSize[2];
            float scanlineStrength;
            float sharpenStrength;
            float maskStrength;
            uint32_t flags;
            float gamma;
            float saturation;
            float contrast;
            float brightness;
            float curvature;
            float vignette;
            int32_t maskStyle;
            float overscan;
            float temperature;
            float outScale[2];
            float compositeBlur;
            int32_t scaleMode;
            int32_t colorProfile;
        } push{};
        push.sourceSize[0] = static_cast<float>(copyWidth);
        push.sourceSize[1] = static_cast<float>(copyHeight);
        push.scanlineStrength = (std::clamp)(options.scanlineStrength, 0.0f, 0.60f);
        push.sharpenStrength = (std::clamp)(options.sharpenStrength, 0.0f, 1.0f);
        push.maskStrength = (std::clamp)(options.crtMaskStrength, 0.0f, 0.70f);
        push.gamma = (std::clamp)(options.gamma, 0.5f, 2.5f);
        push.saturation = (std::clamp)(options.saturation, 0.0f, 2.0f);
        push.contrast = (std::clamp)(options.contrast, 0.0f, 2.0f);
        push.brightness = (std::clamp)(options.brightness, -1.0f, 1.0f);
        push.curvature = (std::clamp)(options.curvature, 0.0f, 0.35f);
        push.vignette = (std::clamp)(options.vignette, 0.0f, 1.0f);
        push.maskStyle = (std::clamp)(options.crtMaskStyle, 0, 2);
        push.overscan = (std::clamp)(options.overscan, 0.0f, 0.12f);
        push.temperature = (std::clamp)(options.temperature, -1.0f, 1.0f);
        push.outScale[0] = static_cast<float>(presentWidth) / (std::max)(1.0f, push.sourceSize[0]);
        push.outScale[1] = static_cast<float>(presentHeight) / (std::max)(1.0f, push.sourceSize[1]);
        push.compositeBlur = (std::clamp)(options.compositeBlur, 0.0f, 1.0f);
        push.scaleMode = options.filter == FilterMode::SharpBilinear ? 2 : (options.filter == FilterMode::Bicubic ? 3 : (options.filter == FilterMode::Linear ? 1 : 0));
        push.colorProfile = (std::clamp)(options.colorProfile, 0, 3);
        if (options.filter != FilterMode::Nearest) push.flags |= 1u;
        if (options.pseudoHiresBlend && sourceWidth >= 512) push.flags |= 2u;
        vkCmdBindPipeline(frame.CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, presentation_pipeline_);
        vkCmdSetViewport(frame.CommandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(frame.CommandBuffer, 0, 1, &scissor);
        vkCmdBindDescriptorSets(frame.CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                presentation_pipeline_layout_, 0, 1,
                                &presentation_descriptor_set_, 0, nullptr);
        vkCmdPushConstants(frame.CommandBuffer, presentation_pipeline_layout_,
                           VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
        vkCmdDraw(frame.CommandBuffer, 3, 1, 0, 0);
    }
    if (imguiDrawData) ImGui_ImplVulkan_RenderDrawData(imguiDrawData, frame.CommandBuffer);
    vkCmdEndRenderPass(frame.CommandBuffer);
    result = vkEndCommandBuffer(frame.CommandBuffer);
    if (result != VK_SUCCESS) return setVkError("vkEndCommandBuffer", result);
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &imageAcquired;
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &frame.CommandBuffer;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &renderComplete;
    result = vkQueueSubmit(queue_, 1, &submit, frame.Fence);
    if (result != VK_SUCCESS) return setVkError("vkQueueSubmit", result);
    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &renderComplete;
    present.swapchainCount = 1;
    present.pSwapchains = &window_data_.Swapchain;
    present.pImageIndices = &window_data_.FrameIndex;
    result = vkQueuePresentKHR(queue_, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        swapchain_rebuild_ = true;
    } else if (result != VK_SUCCESS) {
        return setVkError("vkQueuePresentKHR", result);
    }
    window_data_.SemaphoreIndex = (window_data_.SemaphoreIndex + 1) % window_data_.SemaphoreCount;
    return true;
}
void VulkanRenderer::destroyUploadFrames() {
    if (device_ == VK_NULL_HANDLE) {
        upload_frames_.clear();
        return;
    }
    for (UploadFrame& upload : upload_frames_) {
        if (upload.mapped && upload.memory) vkUnmapMemory(device_, upload.memory);
        upload.mapped = nullptr;
        upload.host_coherent = true;
        if (upload.buffer) vkDestroyBuffer(device_, upload.buffer, nullptr);
        if (upload.memory) vkFreeMemory(device_, upload.memory, nullptr);
        upload.buffer = VK_NULL_HANDLE;
        upload.memory = VK_NULL_HANDLE;
    }
    upload_frames_.clear();
}
void VulkanRenderer::destroyPresentationPipeline() {
    if (device_ != VK_NULL_HANDLE && presentation_pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, presentation_pipeline_, nullptr);
    }
    presentation_pipeline_ = VK_NULL_HANDLE;
}
void VulkanRenderer::destroyPresentationResources() {
    if (device_ == VK_NULL_HANDLE) return;
    destroyPresentationPipeline();
    if (presentation_pipeline_layout_) vkDestroyPipelineLayout(device_, presentation_pipeline_layout_, nullptr);
    if (presentation_descriptor_pool_) vkDestroyDescriptorPool(device_, presentation_descriptor_pool_, nullptr);
    if (presentation_descriptor_layout_) vkDestroyDescriptorSetLayout(device_, presentation_descriptor_layout_, nullptr);
    if (game_sampler_) vkDestroySampler(device_, game_sampler_, nullptr);
    presentation_pipeline_layout_ = VK_NULL_HANDLE;
    presentation_descriptor_pool_ = VK_NULL_HANDLE;
    presentation_descriptor_layout_ = VK_NULL_HANDLE;
    presentation_descriptor_set_ = VK_NULL_HANDLE;
    game_sampler_ = VK_NULL_HANDLE;
}
void VulkanRenderer::destroyGameImage() {
    if (device_ == VK_NULL_HANDLE) return;
    if (game_image_view_) vkDestroyImageView(device_, game_image_view_, nullptr);
    if (game_image_) vkDestroyImage(device_, game_image_, nullptr);
    if (game_image_memory_) vkFreeMemory(device_, game_image_memory_, nullptr);
    game_image_view_ = VK_NULL_HANDLE;
    game_image_ = VK_NULL_HANDLE;
    game_image_memory_ = VK_NULL_HANDLE;
    game_image_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
}
void VulkanRenderer::destroySwapchain() {
    if (device_ != VK_NULL_HANDLE && window_data_.Surface != VK_NULL_HANDLE) {
        ImGui_ImplVulkanH_DestroyWindow(instance_, device_, &window_data_, nullptr);
    }
}
void VulkanRenderer::shutdown() {
    if (driver_crashed_) return;
    if (device_ != VK_NULL_HANDLE) vkDeviceWaitIdle(device_);
    if (imgui_initialized_) {
        ImGui_ImplVulkan_Shutdown();
        imgui_initialized_ = false;
    }
    destroyUploadFrames();
    destroyPresentationResources();
    destroyGameImage();
    destroySwapchain();
    if (surface_ != VK_NULL_HANDLE && instance_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
    }
    surface_ = VK_NULL_HANDLE;
    window_data_.Surface = VK_NULL_HANDLE;
    if (device_ != VK_NULL_HANDLE) vkDestroyDevice(device_, nullptr);
    device_ = VK_NULL_HANDLE;
    queue_ = VK_NULL_HANDLE;
    physical_device_ = VK_NULL_HANDLE;
    queue_family_ = UINT32_MAX;
    if (instance_ != VK_NULL_HANDLE) vkDestroyInstance(instance_, nullptr);
    instance_ = VK_NULL_HANDLE;
    window_ = nullptr;
    swapchain_rebuild_ = false;
    swapchain_configured_ = false;
    linear_filter_supported_ = false;
}
#endif

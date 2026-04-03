//
// VulkanParticleEngine.cpp
// Particle Flow - Vulkan Migration
// NEXUS Engineering — Canyon (Kaneon)
// Behavioral parity with original RenderScript implementation
//

#include "VulkanParticleEngine.h"
#include "VulkanHelpers.h"
#include <android/log.h>
#include <cstring>
#include <cmath>
#include <vector>
#include <stdexcept>
#include <random>

#define LOG_TAG "ParticleFlow"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ═══════════════════════════════════════════════════════════════
// CONSTANTS
// ═══════════════════════════════════════════════════════════════

static const uint32_t WORKGROUP_SIZE       = 256;
static const uint32_t MAX_TOUCH_POINTS     = 16;
static const float    PI                   = 3.14159265358979f;

// ═══════════════════════════════════════════════════════════════
// CONSTRUCTOR / DESTRUCTOR
// ═══════════════════════════════════════════════════════════════

VulkanParticleEngine::VulkanParticleEngine()
    : instance_(VK_NULL_HANDLE)
    , physicalDevice_(VK_NULL_HANDLE)
    , device_(VK_NULL_HANDLE)
    , graphicsQueue_(VK_NULL_HANDLE)
    , computeQueue_(VK_NULL_HANDLE)
    , surface_(VK_NULL_HANDLE)
    , swapchain_(VK_NULL_HANDLE)
    , renderPass_(VK_NULL_HANDLE)
    , computePipeline_(VK_NULL_HANDLE)
    , computePipelineInit_(VK_NULL_HANDLE)
    , graphicsPipeline_(VK_NULL_HANDLE)
    , computePipelineLayout_(VK_NULL_HANDLE)
    , graphicsPipelineLayout_(VK_NULL_HANDLE)
    , computeDescriptorSetLayout_(VK_NULL_HANDLE)
    , computeDescriptorPool_(VK_NULL_HANDLE)
    , computeDescriptorSet_(VK_NULL_HANDLE)
    , commandPool_(VK_NULL_HANDLE)
    , positionBuffer_(VK_NULL_HANDLE)
    , positionMemory_(VK_NULL_HANDLE)
    , velocityBuffer_(VK_NULL_HANDLE)
    , velocityMemory_(VK_NULL_HANDLE)
    , colorBuffer_(VK_NULL_HANDLE)
    , colorMemory_(VK_NULL_HANDLE)
    , touchBuffer_(VK_NULL_HANDLE)
    , touchMemory_(VK_NULL_HANDLE)
    , touchStagingBuffer_(VK_NULL_HANDLE)
    , touchStagingMemory_(VK_NULL_HANDLE)
    , touchMapped_(nullptr)
    , graphicsFamily_(0)
    , computeFamily_(0)
    , initialized_(false)
    , width_(0)
    , height_(0)
    , numParticles_(50000)
    , numTouchPoints_(5)
    , particleSize_(1)
    , frameSeed_(0)
{
    // Default parameters matching original app
    params_.slowH      = 0.667f;  // Blue hue
    params_.slowS      = 1.0f;
    params_.slowV      = 1.0f;
    params_.fastH      = 0.0f;   // Red hue
    params_.fastS      = 1.0f;
    params_.fastV      = 1.0f;
    params_.hueDir     = 0;
    params_.attract    = 150.0f;
    params_.drag       = 0.88f;
    params_.width      = 1.0f;
    params_.height     = 1.0f;
    params_.numPart    = numParticles_;
    params_.numTouch   = numTouchPoints_;
    params_.partSize   = 1;
    params_.bgColor    = 0xFF000000;

    touchPositions_.resize(MAX_TOUCH_POINTS, {-1.0f, -1.0f});
}

VulkanParticleEngine::~VulkanParticleEngine() {
    cleanup();
}

// ═══════════════════════════════════════════════════════════════
// INITIALIZATION
// ═══════════════════════════════════════════════════════════════

bool VulkanParticleEngine::initialize(ANativeWindow* window) {
    LOGI("Initializing Vulkan Particle Engine");

    try {
        createInstance();
        createSurface(window);
        pickPhysicalDevice();
        createLogicalDevice();
        createSwapchain();
        createRenderPass();
        createFramebuffers();
        createCommandPool();
        createParticleBuffers();
        createTouchBuffers();
        setDefaultAttractionPoints(); // place initial circle before first initParticles()
        createComputeDescriptors();
        createComputePipelines();
        createGraphicsPipeline();
        createCommandBuffers();
        createSyncObjects();

        initialized_ = true;
        LOGI("Vulkan initialization complete. Particles: %d", numParticles_);
        return true;

    } catch (const std::exception& e) {
        LOGE("Vulkan init failed: %s", e.what());
        cleanup();
        return false;
    }
}

void VulkanParticleEngine::setParameters(
    float slowH, float slowS, float slowV,
    float fastH, float fastS, float fastV,
    int hueDir, float attract, float drag,
    float w, float h, int numPart, int numTouch,
    int partSize, int bgColor)
{
    params_.slowH   = slowH;
    params_.slowS   = slowS;
    params_.slowV   = slowV;
    params_.fastH   = fastH;
    params_.fastS   = fastS;
    params_.fastV   = fastV;
    params_.hueDir  = hueDir;
    params_.attract = attract;
    params_.drag    = drag;
    params_.width   = w;
    params_.height  = h;
    params_.numPart = numPart;
    params_.numTouch= numTouch;
    params_.partSize= partSize;
    params_.bgColor = bgColor;

    numParticles_   = numPart;
    numTouchPoints_ = numTouch;
    particleSize_   = partSize;
    width_          = (uint32_t)w;
    height_         = (uint32_t)h;
}

// ═══════════════════════════════════════════════════════════════
// PARTICLE INITIALIZATION
// Replicates original RenderScript forEach_initParticles
// ═══════════════════════════════════════════════════════════════

void VulkanParticleEngine::initParticles() {
    if (!initialized_) return;

    // Dispatch init compute shader — scatters particles randomly across screen
    VkCommandBuffer cmd = beginSingleTimeCommands();

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineInit_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        computePipelineLayout_, 0, 1, &computeDescriptorSet_, 0, nullptr);

    // Push constants for init
    ComputePushConstants pc{};
    pc.numParticles  = numParticles_;
    pc.numTouch      = numTouchPoints_;
    pc.attract       = params_.attract;
    pc.drag          = params_.drag;
    pc.width         = (float)width_;
    pc.height        = (float)height_;
    pc.slowH         = params_.slowH;
    pc.slowS         = params_.slowS;
    pc.slowV         = params_.slowV;
    pc.fastH         = params_.fastH;
    pc.fastS         = params_.fastS;
    pc.fastV         = params_.fastV;
    pc.hueDir        = params_.hueDir;
    pc.frameSeed     = frameSeed_++;

    vkCmdPushConstants(cmd, computePipelineLayout_,
        VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &pc);

    uint32_t groupCount = (numParticles_ + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
    vkCmdDispatch(cmd, groupCount, 1, 1);

    endSingleTimeCommands(cmd);
    LOGI("Particles initialized: %d", numParticles_);
}

// ═══════════════════════════════════════════════════════════════
// TESLA 369 PATTERN INITIALIZATION
// 9 points: 3 outer triangle + 6 inner hexagon
// ═══════════════════════════════════════════════════════════════

void VulkanParticleEngine::initTesla369Pattern() {
    float cx = width_  / 2.0f;
    float cy = height_ / 2.0f;
    float minDim     = std::min((float)width_, (float)height_);
    float outerR     = minDim * 0.30f;
    float innerR     = outerR * 0.60f;

    // Clear all touch points first
    resetAttractionPoints();

    // 3 outer triangle points (120 degrees apart, starting top)
    for (int i = 0; i < 3; i++) {
        float angle = (i * 120.0f - 90.0f) * PI / 180.0f;
        touchPositions_[i] = {
            cx + outerR * cosf(angle),
            cy + outerR * sinf(angle)
        };
    }

    // 6 inner hexagon points (60 degrees apart)
    for (int i = 0; i < 6; i++) {
        float angle = (i * 60.0f - 90.0f) * PI / 180.0f;
        touchPositions_[3 + i] = {
            cx + innerR * cosf(angle),
            cy + innerR * sinf(angle)
        };
    }

    // Set Tesla defaults
    params_.attract  = 28.0f;
    params_.drag     = 0.0f;
    numParticles_    = 432000;

    updateTouchBuffer();
    LOGI("Tesla 369 pattern initialized");
}

// ═══════════════════════════════════════════════════════════════
// TOUCH HANDLING
// Negative coordinates (-1,-1) = disabled point
// ═══════════════════════════════════════════════════════════════

void VulkanParticleEngine::setTouch(int index, float x, float y) {
    if (index < 0 || index >= MAX_TOUCH_POINTS) return;
    touchPositions_[index] = {x, y};
    updateTouchBuffer();
}

void VulkanParticleEngine::resetAttractionPoints() {
    // Restore the default circle of attraction points (matches original .rs app).
    // Falls back to all-disabled when called before dimensions are known (e.g. from ctor).
    if (width_ > 0 && height_ > 0) {
        setDefaultAttractionPoints();
    } else {
        for (auto& pt : touchPositions_) pt = {-1.0f, -1.0f};
        if (touchMapped_) updateTouchBuffer();
    }
}

// ═══════════════════════════════════════════════════════════════
// DEFAULT ATTRACTION POINTS
// Matches original ParticlesRenderer.java resetAttractionPoints():
//   l = min(w,h)/3; points[0] at (cx, cy+l); remaining evenly on circle.
// Coordinates are in Android screen space (Y=0 at top) — no Y-flip needed
// because Vulkan vertex shader also maps Y=0 to the top of the screen.
// ═══════════════════════════════════════════════════════════════

void VulkanParticleEngine::setDefaultAttractionPoints() {
    if (width_ == 0 || height_ == 0) return;

    float cx = width_  / 2.0f;
    float cy = height_ / 2.0f;
    float l  = (float)std::min(width_, height_) / 2.5f;

    touchPositions_[0] = {cx, cy + (numTouchPoints_ == 1 ? 0.0f : l)};
    for (int i = 1; i < numTouchPoints_; i++) {
        float angle = i * 2.0f * PI / (float)numTouchPoints_;
        touchPositions_[i] = { cx + l * sinf(angle), cy + l * cosf(angle) };
    }
    for (size_t i = (size_t)numTouchPoints_; i < touchPositions_.size(); i++) {
        touchPositions_[i] = {-1.0f, -1.0f};
    }
    updateTouchBuffer();
}

void VulkanParticleEngine::updateTouchBuffer() {
    if (!touchMapped_) return;
    memcpy(touchMapped_, touchPositions_.data(),
           sizeof(Vec2) * touchPositions_.size());

    // Copy staging → device touch buffer
    VkCommandBuffer cmd = beginSingleTimeCommands();
    VkBufferCopy copy{};
    copy.size = sizeof(Vec2) * MAX_TOUCH_POINTS;
    vkCmdCopyBuffer(cmd, touchStagingBuffer_, touchBuffer_, 1, &copy);
    endSingleTimeCommands(cmd);
}

// ═══════════════════════════════════════════════════════════════
// UPDATE PARTICLES (COMPUTE PASS)
// Replicates original forEach_updateParticles — stays on GPU
// ═══════════════════════════════════════════════════════════════

void VulkanParticleEngine::updateParticles() {
    if (!initialized_) return;

    VkCommandBuffer cmd = beginSingleTimeCommands();

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        computePipelineLayout_, 0, 1, &computeDescriptorSet_, 0, nullptr);

    ComputePushConstants pc{};
    pc.numParticles  = numParticles_;
    pc.numTouch      = numTouchPoints_;
    pc.attract       = params_.attract;
    pc.drag          = params_.drag;
    pc.width         = (float)width_;
    pc.height        = (float)height_;
    pc.slowH         = params_.slowH;
    pc.slowS         = params_.slowS;
    pc.slowV         = params_.slowV;
    pc.fastH         = params_.fastH;
    pc.fastS         = params_.fastS;
    pc.fastV         = params_.fastV;
    pc.hueDir        = params_.hueDir;
    pc.frameSeed     = frameSeed_++;

    vkCmdPushConstants(cmd, computePipelineLayout_,
        VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &pc);

    // Memory barrier: ensure render finished before compute writes
    VkMemoryBarrier barrier{};
    barrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);

    uint32_t groupCount = (numParticles_ + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
    vkCmdDispatch(cmd, groupCount, 1, 1);

    // Memory barrier: ensure compute finished before render reads
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);

    endSingleTimeCommands(cmd);
}

// ═══════════════════════════════════════════════════════════════
// RENDER FRAME
// Renders particles directly from GPU buffers — no CPU readback
// ═══════════════════════════════════════════════════════════════

void VulkanParticleEngine::render() {
    if (!initialized_ || swapchainImages_.empty()) return;

    // Wait for previous frame fence
    vkWaitForFences(device_, 1, &inFlightFence_, VK_TRUE, UINT64_MAX);
    vkResetFences(device_, 1, &inFlightFence_);

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
        imageAvailableSemaphore_, VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return;
    }

    // Extract background color components
    float bgR = ((params_.bgColor >> 16) & 0xFF) / 255.0f;
    float bgG = ((params_.bgColor >>  8) & 0xFF) / 255.0f;
    float bgB = ((params_.bgColor      ) & 0xFF) / 255.0f;

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkResetCommandBuffer(graphicsCommandBuffers_[imageIndex], 0);
    vkBeginCommandBuffer(graphicsCommandBuffers_[imageIndex], &beginInfo);

    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass        = renderPass_;
    rpInfo.framebuffer       = framebuffers_[imageIndex];
    rpInfo.renderArea.extent = swapchainExtent_;

    VkClearValue clearColor{};
    clearColor.color = {{bgR, bgG, bgB, 1.0f}};
    rpInfo.clearValueCount = 1;
    rpInfo.pClearValues    = &clearColor;

    vkCmdBeginRenderPass(graphicsCommandBuffers_[imageIndex],
        &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(graphicsCommandBuffers_[imageIndex],
        VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline_);

    // Bind position and color as vertex buffers
    VkBuffer vertexBuffers[] = {positionBuffer_, colorBuffer_};
    VkDeviceSize offsets[]   = {0, 0};
    vkCmdBindVertexBuffers(graphicsCommandBuffers_[imageIndex],
        0, 2, vertexBuffers, offsets);

    // Build column-major orthographic matrix matching what the compiled SPV expects:
    //   mat4 mvpMatrix (offset 0, 64 bytes) + float pointSize (offset 64).
    // Maps particle coords [0..w]×[0..h] → Vulkan NDC [-1..1]×[-1..1].
    // Vulkan NDC Y=-1 is top, Y=+1 is bottom; particles use Android Y=0-at-top → correct.
    GraphicsPushConstants gpc{};
    float fw = (float)width_;
    float fh = (float)height_;
    // col 0
    gpc.mvpMatrix[0]  = 2.0f / fw;  gpc.mvpMatrix[1]  = 0.0f;
    gpc.mvpMatrix[2]  = 0.0f;       gpc.mvpMatrix[3]  = 0.0f;
    // col 1
    gpc.mvpMatrix[4]  = 0.0f;       gpc.mvpMatrix[5]  = 2.0f / fh;
    gpc.mvpMatrix[6]  = 0.0f;       gpc.mvpMatrix[7]  = 0.0f;
    // col 2
    gpc.mvpMatrix[8]  = 0.0f;       gpc.mvpMatrix[9]  = 0.0f;
    gpc.mvpMatrix[10] = 1.0f;       gpc.mvpMatrix[11] = 0.0f;
    // col 3 — translation
    gpc.mvpMatrix[12] = -1.0f;      gpc.mvpMatrix[13] = -1.0f;
    gpc.mvpMatrix[14] = 0.0f;       gpc.mvpMatrix[15] = 1.0f;
    gpc.pointSize = (float)particleSize_;

    vkCmdPushConstants(graphicsCommandBuffers_[imageIndex],
        graphicsPipelineLayout_,
        VK_SHADER_STAGE_VERTEX_BIT,
        0, sizeof(GraphicsPushConstants), &gpc);

    vkCmdDraw(graphicsCommandBuffers_[imageIndex], numParticles_, 1, 0, 0);
    vkCmdEndRenderPass(graphicsCommandBuffers_[imageIndex]);
    vkEndCommandBuffer(graphicsCommandBuffers_[imageIndex]);

    // Submit
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkPipelineStageFlags waitStages[] = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
    };
    submitInfo.waitSemaphoreCount   = 1;
    submitInfo.pWaitSemaphores      = &imageAvailableSemaphore_;
    submitInfo.pWaitDstStageMask    = waitStages;
    submitInfo.commandBufferCount   = 1;
    submitInfo.pCommandBuffers      = &graphicsCommandBuffers_[imageIndex];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores    = &renderFinishedSemaphore_;

    vkQueueSubmit(graphicsQueue_, 1, &submitInfo, inFlightFence_);

    // Present
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores    = &renderFinishedSemaphore_;
    presentInfo.swapchainCount     = 1;
    presentInfo.pSwapchains        = &swapchain_;
    presentInfo.pImageIndices      = &imageIndex;

    result = vkQueuePresentKHR(graphicsQueue_, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        recreateSwapchain();
    }
}

// ═══════════════════════════════════════════════════════════════
// SURFACE CHANGED
// ═══════════════════════════════════════════════════════════════

void VulkanParticleEngine::onSurfaceChanged(int width, int height) {
    width_  = width;
    height_ = height;
    params_.width  = (float)width;
    params_.height = (float)height;

    if (initialized_) {
        vkDeviceWaitIdle(device_);
        recreateSwapchain();
    }
}

// ═══════════════════════════════════════════════════════════════
// VULKAN INSTANCE CREATION
// ═══════════════════════════════════════════════════════════════

void VulkanParticleEngine::createInstance() {
    VkApplicationInfo appInfo{};
    appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName   = "ParticleFlow";
    appInfo.applicationVersion = VK_MAKE_VERSION(2, 0, 0);
    appInfo.pEngineName        = "NEXUS";
    appInfo.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion         = VK_API_VERSION_1_0;

    std::vector<const char*> extensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        "VK_KHR_android_surface"
    };

    VkInstanceCreateInfo createInfo{};
    createInfo.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo        = &appInfo;
    createInfo.enabledExtensionCount   = (uint32_t)extensions.size();
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.enabledLayerCount       = 0;

    if (vkCreateInstance(&createInfo, nullptr, &instance_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan instance");
    }
}

void VulkanParticleEngine::createSurface(ANativeWindow* window) {
    VkAndroidSurfaceCreateInfoKHR createInfo{};
    createInfo.sType  = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    createInfo.window = window;

    PFN_vkCreateAndroidSurfaceKHR vkCreateAndroidSurfaceKHR =
        (PFN_vkCreateAndroidSurfaceKHR)vkGetInstanceProcAddr(
            instance_, "vkCreateAndroidSurfaceKHR");

    if (!vkCreateAndroidSurfaceKHR ||
        vkCreateAndroidSurfaceKHR(instance_, &createInfo, nullptr, &surface_)
            != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Android surface");
    }
}

void VulkanParticleEngine::pickPhysicalDevice() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    if (count == 0) throw std::runtime_error("No Vulkan devices found");

    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());
    physicalDevice_ = devices[0]; // Use first available

    // Find queue families
    uint32_t queueCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &queueCount, nullptr);
    std::vector<VkQueueFamilyProperties> queues(queueCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &queueCount, queues.data());

    for (uint32_t i = 0; i < queueCount; i++) {
        if (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) graphicsFamily_ = i;
        if (queues[i].queueFlags & VK_QUEUE_COMPUTE_BIT)  computeFamily_  = i;
    }
}

void VulkanParticleEngine::createLogicalDevice() {
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    float priority = 1.0f;

    VkDeviceQueueCreateInfo gq{};
    gq.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    gq.queueFamilyIndex = graphicsFamily_;
    gq.queueCount       = 1;
    gq.pQueuePriorities = &priority;
    queueInfos.push_back(gq);

    if (computeFamily_ != graphicsFamily_) {
        VkDeviceQueueCreateInfo cq = gq;
        cq.queueFamilyIndex = computeFamily_;
        queueInfos.push_back(cq);
    }

    VkPhysicalDeviceFeatures features{};
    features.largePoints = VK_TRUE; // Needed for particle size > 1

    std::vector<const char*> extensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    VkDeviceCreateInfo createInfo{};
    createInfo.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount    = (uint32_t)queueInfos.size();
    createInfo.pQueueCreateInfos       = queueInfos.data();
    createInfo.pEnabledFeatures        = &features;
    createInfo.enabledExtensionCount   = (uint32_t)extensions.size();
    createInfo.ppEnabledExtensionNames = extensions.data();

    if (vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_)
            != VK_SUCCESS) {
        throw std::runtime_error("Failed to create logical device");
    }

    vkGetDeviceQueue(device_, graphicsFamily_, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, computeFamily_,  0, &computeQueue_);
}

// ═══════════════════════════════════════════════════════════════
// SWAPCHAIN
// ═══════════════════════════════════════════════════════════════

void VulkanParticleEngine::createSwapchain() {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &caps);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, formats.data());

    swapchainFormat_ = formats[0].format;
    for (auto& f : formats) {
        if (f.format == VK_FORMAT_R8G8B8A8_UNORM ||
            f.format == VK_FORMAT_B8G8R8A8_UNORM) {
            swapchainFormat_ = f.format;
            break;
        }
    }

    swapchainExtent_ = caps.currentExtent;
    if (swapchainExtent_.width == UINT32_MAX) {
        swapchainExtent_ = {width_, height_};
    }
    width_  = swapchainExtent_.width;
    height_ = swapchainExtent_.height;

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0) imageCount = std::min(imageCount, caps.maxImageCount);

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface          = surface_;
    createInfo.minImageCount    = imageCount;
    createInfo.imageFormat      = swapchainFormat_;
    createInfo.imageColorSpace  = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    createInfo.imageExtent      = swapchainExtent_;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform     = caps.currentTransform;
    createInfo.compositeAlpha   = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    createInfo.presentMode      = VK_PRESENT_MODE_FIFO_KHR;
    createInfo.clipped          = VK_TRUE;

    if (vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapchain_)
            != VK_SUCCESS) {
        throw std::runtime_error("Failed to create swapchain");
    }

    vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, nullptr);
    swapchainImages_.resize(imageCount);
    vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, swapchainImages_.data());

    // Create image views
    swapchainImageViews_.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; i++) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image    = swapchainImages_[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format   = swapchainFormat_;
        viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount     = 1;
        viewInfo.subresourceRange.layerCount     = 1;
        vkCreateImageView(device_, &viewInfo, nullptr, &swapchainImageViews_[i]);
    }
}

void VulkanParticleEngine::recreateSwapchain() {
    vkDeviceWaitIdle(device_);
    cleanupSwapchain();
    createSwapchain();
    createRenderPass();
    createFramebuffers();
    createGraphicsPipeline();
    createCommandBuffers();
}

void VulkanParticleEngine::cleanupSwapchain() {
    for (auto fb : framebuffers_)          vkDestroyFramebuffer(device_, fb, nullptr);
    for (auto iv : swapchainImageViews_)   vkDestroyImageView(device_, iv, nullptr);
    vkDestroyRenderPass(device_, renderPass_, nullptr);
    vkDestroyPipeline(device_, graphicsPipeline_, nullptr);
    vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    framebuffers_.clear();
    swapchainImageViews_.clear();
    swapchainImages_.clear();
}

// ═══════════════════════════════════════════════════════════════
// RENDER PASS + FRAMEBUFFERS
// ═══════════════════════════════════════════════════════════════

void VulkanParticleEngine::createRenderPass() {
    VkAttachmentDescription attachment{};
    attachment.format         = swapchainFormat_;
    attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &ref;

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments    = &attachment;
    rpInfo.subpassCount    = 1;
    rpInfo.pSubpasses      = &subpass;

    if (vkCreateRenderPass(device_, &rpInfo, nullptr, &renderPass_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create render pass");
    }
}

void VulkanParticleEngine::createFramebuffers() {
    framebuffers_.resize(swapchainImageViews_.size());
    for (size_t i = 0; i < swapchainImageViews_.size(); i++) {
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass      = renderPass_;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments    = &swapchainImageViews_[i];
        fbInfo.width           = swapchainExtent_.width;
        fbInfo.height          = swapchainExtent_.height;
        fbInfo.layers          = 1;
        vkCreateFramebuffer(device_, &fbInfo, nullptr, &framebuffers_[i]);
    }
}

// ═══════════════════════════════════════════════════════════════
// PARTICLE BUFFERS
// All particle data lives on GPU — zero CPU readback
// ═══════════════════════════════════════════════════════════════

void VulkanParticleEngine::createParticleBuffers() {
    VkDeviceSize posSize   = sizeof(Vec2) * numParticles_;
    VkDeviceSize colorSize = sizeof(Vec4) * numParticles_;

    createBuffer(posSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        positionBuffer_, positionMemory_);

    createBuffer(posSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        velocityBuffer_, velocityMemory_);

    createBuffer(colorSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        colorBuffer_, colorMemory_);
}

void VulkanParticleEngine::createTouchBuffers() {
    VkDeviceSize size = sizeof(Vec2) * MAX_TOUCH_POINTS;

    // Staging buffer — host visible, permanently mapped
    createBuffer(size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        touchStagingBuffer_, touchStagingMemory_);

    vkMapMemory(device_, touchStagingMemory_, 0, size, 0, (void**)&touchMapped_);

    // Device buffer — used by compute shader
    createBuffer(size,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        touchBuffer_, touchMemory_);

    // Initialize all touch points to disabled (-1, -1)
    updateTouchBuffer();
}

void VulkanParticleEngine::createBuffer(
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags props,
    VkBuffer& buffer,
    VkDeviceMemory& memory)
{
    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size  = size;
    bufInfo.usage = usage;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device_, &bufInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create buffer");
    }

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device_, buffer, &memReqs);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, props);

    if (vkAllocateMemory(device_, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate buffer memory");
    }

    vkBindBufferMemory(device_, buffer, memory, 0);
}

uint32_t VulkanParticleEngine::findMemoryType(
    uint32_t typeFilter, VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProps);

    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProps.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type");
}

// ═══════════════════════════════════════════════════════════════
// COMPUTE DESCRIPTORS
// Binds particle buffers to compute shaders
// ═══════════════════════════════════════════════════════════════

void VulkanParticleEngine::createComputeDescriptors() {
    // Layout: 4 storage buffers (position, velocity, color, touch)
    std::vector<VkDescriptorSetLayoutBinding> bindings(4);
    for (uint32_t i = 0; i < 4; i++) {
        bindings[i].binding         = i;
        bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = (uint32_t)bindings.size();
    layoutInfo.pBindings    = bindings.data();
    vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr,
        &computeDescriptorSetLayout_);

    // Descriptor pool
    VkDescriptorPoolSize poolSize{};
    poolSize.type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = 4;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets       = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes    = &poolSize;
    vkCreateDescriptorPool(device_, &poolInfo, nullptr, &computeDescriptorPool_);

    // Allocate descriptor set
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool     = computeDescriptorPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts        = &computeDescriptorSetLayout_;
    vkAllocateDescriptorSets(device_, &allocInfo, &computeDescriptorSet_);

    // Write buffer descriptors
    VkBuffer buffers[] = {positionBuffer_, velocityBuffer_, colorBuffer_, touchBuffer_};
    VkDeviceSize sizes[] = {
        sizeof(Vec2) * numParticles_,
        sizeof(Vec2) * numParticles_,
        sizeof(Vec4) * numParticles_,
        sizeof(Vec2) * MAX_TOUCH_POINTS
    };

    std::vector<VkWriteDescriptorSet> writes(4);
    std::vector<VkDescriptorBufferInfo> bufInfos(4);
    for (uint32_t i = 0; i < 4; i++) {
        bufInfos[i].buffer = buffers[i];
        bufInfos[i].offset = 0;
        bufInfos[i].range  = sizes[i];

        writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet          = computeDescriptorSet_;
        writes[i].dstBinding      = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo     = &bufInfos[i];
    }
    vkUpdateDescriptorSets(device_, 4, writes.data(), 0, nullptr);
}

// ═══════════════════════════════════════════════════════════════
// PIPELINES
// ═══════════════════════════════════════════════════════════════

VkShaderModule VulkanParticleEngine::loadShader(const std::string& path) {
    // Load precompiled SPIR-V from assets
    AAssetManager* assetMgr = assetManager_;
    AAsset* asset = AAssetManager_open(assetMgr, path.c_str(), AASSET_MODE_BUFFER);
    if (!asset) throw std::runtime_error("Failed to open shader: " + path);

    size_t size = AAsset_getLength(asset);
    std::vector<char> code(size);
    AAsset_read(asset, code.data(), size);
    AAsset_close(asset);

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = size;
    createInfo.pCode    = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device_, &createInfo, nullptr, &shaderModule)
            != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module: " + path);
    }
    return shaderModule;
}

void VulkanParticleEngine::createComputePipelines() {
    // Pipeline layout with push constants
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset     = 0;
    pushRange.size       = sizeof(ComputePushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount         = 1;
    layoutInfo.pSetLayouts            = &computeDescriptorSetLayout_;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges    = &pushRange;
    vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &computePipelineLayout_);

    auto createPipeline = [&](const std::string& shaderPath, VkPipeline& pipeline) {
        VkShaderModule shader = loadShader(shaderPath);

        VkPipelineShaderStageCreateInfo stage{};
        stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
        stage.module = shader;
        stage.pName  = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage  = stage;
        pipelineInfo.layout = computePipelineLayout_;
        vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo,
            nullptr, &pipeline);

        vkDestroyShaderModule(device_, shader, nullptr);
    };

    createPipeline("shaders/particle_init.spv",   computePipelineInit_);
    createPipeline("shaders/particle_update.spv", computePipeline_);
}

void VulkanParticleEngine::createGraphicsPipeline() {
    VkShaderModule vertShader = loadShader("shaders/particle_render.vert.spv");
    VkShaderModule fragShader = loadShader("shaders/particle_render.frag.spv");

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertShader;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragShader;
    stages[1].pName  = "main";

    // Vertex input: binding 0 = position (Vec2), binding 1 = color (vec4)
    VkVertexInputBindingDescription bindings[2]{};
    bindings[0].binding   = 0;
    bindings[0].stride    = sizeof(Vec2);
    bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    bindings[1].binding   = 1;
    bindings[1].stride    = sizeof(Vec4);
    bindings[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[2]{};
    attrs[0].binding  = 0;
    attrs[0].location = 0;
    attrs[0].format   = VK_FORMAT_R32G32_SFLOAT;
    attrs[0].offset   = 0;
    attrs[1].binding  = 1;
    attrs[1].location = 1;
    attrs[1].format   = VK_FORMAT_R32G32B32A32_SFLOAT;
    attrs[1].offset   = 0;

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount   = 2;
    vertexInput.pVertexBindingDescriptions      = bindings;
    vertexInput.vertexAttributeDescriptionCount = 2;
    vertexInput.pVertexAttributeDescriptions    = attrs;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST; // Particles are GL_POINTS

    VkViewport viewport{0, 0, (float)swapchainExtent_.width,
        (float)swapchainExtent_.height, 0.0f, 1.0f};
    VkRect2D scissor{{0,0}, swapchainExtent_};

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports    = &viewport;
    viewportState.scissorCount  = 1;
    viewportState.pScissors     = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType     = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode  = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Alpha blending for particles
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                                        | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable         = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp        = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendAttachment.alphaBlendOp        = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments    = &blendAttachment;

    // Push constants for screen size
    VkPushConstantRange gPushRange{};
    gPushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    gPushRange.offset     = 0;
    gPushRange.size       = sizeof(GraphicsPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges    = &gPushRange;
    vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr,
        &graphicsPipelineLayout_);

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount          = 2;
    pipelineInfo.pStages             = stages;
    pipelineInfo.pVertexInputState   = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState      = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState   = &multisampling;
    pipelineInfo.pColorBlendState    = &colorBlending;
    pipelineInfo.layout              = graphicsPipelineLayout_;
    pipelineInfo.renderPass          = renderPass_;
    pipelineInfo.subpass             = 0;

    vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo,
        nullptr, &graphicsPipeline_);

    vkDestroyShaderModule(device_, vertShader, nullptr);
    vkDestroyShaderModule(device_, fragShader, nullptr);
}

// ═══════════════════════════════════════════════════════════════
// COMMAND POOL + BUFFERS + SYNC
// ═══════════════════════════════════════════════════════════════

void VulkanParticleEngine::createCommandPool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = graphicsFamily_;
    poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_);
}

void VulkanParticleEngine::createCommandBuffers() {
    graphicsCommandBuffers_.resize(swapchainImages_.size());
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool        = commandPool_;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t)graphicsCommandBuffers_.size();
    vkAllocateCommandBuffers(device_, &allocInfo, graphicsCommandBuffers_.data());
}

void VulkanParticleEngine::createSyncObjects() {
    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    vkCreateSemaphore(device_, &semInfo, nullptr, &imageAvailableSemaphore_);
    vkCreateSemaphore(device_, &semInfo, nullptr, &renderFinishedSemaphore_);

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(device_, &fenceInfo, nullptr, &inFlightFence_);
}

VkCommandBuffer VulkanParticleEngine::beginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool        = commandPool_;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device_, &allocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);
    return cmd;
}

void VulkanParticleEngine::endSingleTimeCommands(VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo submitInfo{};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &cmd;
    vkQueueSubmit(graphicsQueue_, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue_);
    vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
}

// ═══════════════════════════════════════════════════════════════
// CLEANUP
// ═══════════════════════════════════════════════════════════════

void VulkanParticleEngine::cleanup() {
    if (!device_) return;
    vkDeviceWaitIdle(device_);

    if (touchMapped_) {
        vkUnmapMemory(device_, touchStagingMemory_);
        touchMapped_ = nullptr;
    }

    auto destroyBuf = [&](VkBuffer& b, VkDeviceMemory& m) {
        if (b) { vkDestroyBuffer(device_, b, nullptr); b = VK_NULL_HANDLE; }
        if (m) { vkFreeMemory(device_, m, nullptr);    m = VK_NULL_HANDLE; }
    };

    destroyBuf(positionBuffer_,    positionMemory_);
    destroyBuf(velocityBuffer_,    velocityMemory_);
    destroyBuf(colorBuffer_,       colorMemory_);
    destroyBuf(touchBuffer_,       touchMemory_);
    destroyBuf(touchStagingBuffer_, touchStagingMemory_);

    vkDestroySemaphore(device_, imageAvailableSemaphore_, nullptr);
    vkDestroySemaphore(device_, renderFinishedSemaphore_, nullptr);
    vkDestroyFence(device_, inFlightFence_, nullptr);

    vkDestroyCommandPool(device_, commandPool_, nullptr);

    if (computeDescriptorPool_)    vkDestroyDescriptorPool(device_, computeDescriptorPool_, nullptr);
    if (computeDescriptorSetLayout_) vkDestroyDescriptorSetLayout(device_, computeDescriptorSetLayout_, nullptr);
    if (computePipeline_)          vkDestroyPipeline(device_, computePipeline_, nullptr);
    if (computePipelineInit_)      vkDestroyPipeline(device_, computePipelineInit_, nullptr);
    if (computePipelineLayout_)    vkDestroyPipelineLayout(device_, computePipelineLayout_, nullptr);
    if (graphicsPipelineLayout_)   vkDestroyPipelineLayout(device_, graphicsPipelineLayout_, nullptr);

    cleanupSwapchain();

    if (surface_)  vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (device_)   vkDestroyDevice(device_, nullptr);
    if (instance_) vkDestroyInstance(instance_, nullptr);

    initialized_ = false;
    LOGI("Vulkan engine cleaned up");
}

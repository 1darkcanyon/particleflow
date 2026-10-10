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
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <fcntl.h>
#include <unistd.h>

#define LOG_TAG "ParticleFlow"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ═══════════════════════════════════════════════════════════════
// BREADCRUMB TRACE (logcat + file)
// ═══════════════════════════════════════════════════════════════

static std::mutex  g_traceMutex;
static std::string g_tracePath;

void pfSetTracePath(const char* path) {
    std::lock_guard<std::mutex> lk(g_traceMutex);
    g_tracePath = path ? path : "";
}

void pfTrace(const char* fmt, ...) {
    char buf[320];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", buf);

    std::lock_guard<std::mutex> lk(g_traceMutex);
    if (g_tracePath.empty()) return;
    int fd = open(g_tracePath.c_str(), O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd < 0) return;
    std::string line = "         N: ";
    line += buf;
    line += "\n";
    ssize_t w = write(fd, line.data(), line.size());
    (void)w;
    close(fd);
}

#define PF_STEP(call) do { pfTrace("init: %s", #call); call; } while (0)

// ═══════════════════════════════════════════════════════════════
// CONSTANTS
// ═══════════════════════════════════════════════════════════════

static const uint32_t WORKGROUP_SIZE       = 256;
static const uint32_t MAX_TOUCH_POINTS     = 16;
static const float    PI                   = 3.14159265358979f;
static const int      MAX_PARTICLES        = 4000000;

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
    params_.attract    = 100.0f;
    params_.drag       = 1.0f;   // 1.0 = no drag (matches UI default 0 → 1.0 - 0/999)
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
    pfTrace("initialize(): start");
    traceFrames_ = 0;

    try {
        PF_STEP(createInstance());
        PF_STEP(createSurface(window));
        PF_STEP(pickPhysicalDevice());
        PF_STEP(createLogicalDevice());
        PF_STEP(createSwapchain());
        PF_STEP(createRenderPass());
        PF_STEP(createFramebuffers());
        PF_STEP(createCommandPool());
        PF_STEP(createParticleBuffers());      // sized from numParticles_ (setParameters must run BEFORE initialize)
        PF_STEP(createTouchBuffers());
        PF_STEP(setDefaultAttractionPoints()); // place initial ring before first frame
        PF_STEP(createComputeDescriptors());
        PF_STEP(createComputePipelines());
        PF_STEP(createGraphicsPipeline());
        PF_STEP(createCommandBuffers());
        PF_STEP(createRenderFinishedSemaphores());
        PF_STEP(createSyncObjects());

        {
            std::lock_guard<std::mutex> lk(mutex_);
            needInitParticles_ = true;   // first frame runs the init shader
            touchDirty_        = true;   // first frame uploads the touch buffer
        }
        swapchainValid_ = true;
        initialized_    = true;
        pfTrace("initialize(): complete, particles=%d size=%ux%u", allocatedParticles_, width_, height_);
        return true;

    } catch (const std::exception& e) {
        pfTrace("initialize(): FAILED: %s", e.what());
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
    (void)w; (void)h;   // Surface size comes from the swapchain, never from the caller:
                        // Java may call this before the surface size is known (would be 0).
    std::lock_guard<std::mutex> lk(mutex_);

    int n = std::max(1, std::min(numPart, std::min(MAX_PARTICLES, maxParticlesByDevice_)));
    int t = std::max(1, std::min(numTouch, (int)MAX_TOUCH_POINTS));

    params_.slowH   = slowH;
    params_.slowS   = slowS;
    params_.slowV   = slowV;
    params_.fastH   = fastH;
    params_.fastS   = fastS;
    params_.fastV   = fastV;
    params_.hueDir  = hueDir;
    params_.attract = attract;
    params_.drag    = drag;
    params_.numPart = n;
    params_.numTouch= t;
    params_.partSize= partSize;
    params_.bgColor = bgColor;

    // GPU buffers are (re)allocated on the render thread when numParticles_ != allocatedParticles_.
    numParticles_   = n;
    numTouchPoints_ = t;
    particleSize_   = std::max(1, partSize);
}

// ═══════════════════════════════════════════════════════════════
// PARTICLE INITIALIZATION
// Replicates original RenderScript forEach_initParticles
// ═══════════════════════════════════════════════════════════════

void VulkanParticleEngine::initParticles() {
    // Safe from any thread: just raises a request, the render thread runs the init shader.
    std::lock_guard<std::mutex> lk(mutex_);
    needInitParticles_ = true;
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
    for (auto& pt : touchPositions_) pt = {-1.0f, -1.0f};

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

    // Set Tesla defaults — drag 1.0 = no drag (velocity fully preserved)
    params_.attract  = 28.0f;
    params_.drag     = 1.0f;
    numParticles_    = 432000;

    touchDirty_ = true;
    LOGI("Tesla 369 pattern initialized");
}

// ═══════════════════════════════════════════════════════════════
// TOUCH HANDLING
// Negative coordinates (-1,-1) = disabled point
// ═══════════════════════════════════════════════════════════════

void VulkanParticleEngine::setTouch(int index, float x, float y) {
    if (index < 0 || index >= (int)MAX_TOUCH_POINTS) return;
    std::lock_guard<std::mutex> lk(mutex_);
    touchPositions_[index] = {x, y};
    touchDirty_ = true;   // uploaded by the render thread at the start of the next frame
}

void VulkanParticleEngine::resetAttractionPoints() {
    // Safe from any thread: processed on the render thread (needs the surface size).
    std::lock_guard<std::mutex> lk(mutex_);
    needResetAttraction_ = true;
}

// ═══════════════════════════════════════════════════════════════
// DEFAULT ATTRACTION POINTS
// Matches original ParticlesRenderer.java resetAttractionPoints():
//   l = min(w,h)/3; points[0] at (cx, cy+l); remaining evenly on circle.
// Coordinates are in particle/OpenGL space (y=0 at bottom), matching the original
// RenderScript kernel. Vertex shader negates Y so y=0 displays at bottom of screen.
// ═══════════════════════════════════════════════════════════════

void VulkanParticleEngine::setDefaultAttractionPoints() {
    if (width_ == 0 || height_ == 0) return;
    std::lock_guard<std::mutex> lk(mutex_);

    float cx = width_  / 2.0f;
    float cy = height_ / 2.0f;
    float l  = (float)std::min(width_, height_) / 3.0f;

    touchPositions_[0] = {cx, cy + (numTouchPoints_ == 1 ? 0.0f : l)};
    for (int i = 1; i < numTouchPoints_; i++) {
        float angle = i * 2.0f * PI / (float)numTouchPoints_;
        touchPositions_[i] = { cx + l * sinf(angle), cy + l * cosf(angle) };
    }
    for (size_t i = (size_t)numTouchPoints_; i < touchPositions_.size(); i++) {
        touchPositions_[i] = {-1.0f, -1.0f};
    }
    touchDirty_ = true;
}

void VulkanParticleEngine::updateTouchBuffer() {
    // Copies CPU touch positions into the persistently mapped staging buffer.
    // The GPU copy (staging -> device buffer) is recorded in the frame command buffer.
    if (!touchMapped_) return;
    std::lock_guard<std::mutex> lk(mutex_);
    memcpy(touchMapped_, touchPositions_.data(), sizeof(Vec2) * touchPositions_.size());
}

// ═══════════════════════════════════════════════════════════════
// UPDATE PARTICLES (COMPUTE PASS)
// Replicates original forEach_updateParticles — stays on GPU
// ═══════════════════════════════════════════════════════════════

void VulkanParticleEngine::updateParticles() {
    // Intentionally empty: the compute step is recorded at the start of each frame's
    // command buffer inside render() (one submit per frame, no CPU waits).
}

// ═══════════════════════════════════════════════════════════════
// RENDER FRAME
// Renders particles directly from GPU buffers — no CPU readback
// ═══════════════════════════════════════════════════════════════

void VulkanParticleEngine::render() {
    if (!initialized_ || fatalError_) return;
    try {
        renderImpl();
    } catch (const std::exception& e) {
        LOGE("render() failed, stopping renderer: %s", e.what());
        fatalError_ = true;   // never throw across JNI
    }
}

void VulkanParticleEngine::renderImpl() {
    if (!ensureSwapchain()) return;
    if (swapchainImages_.empty()) return;

    const bool tr = traceFrames_ < 3;
    const int  frameNo = ++traceFrames_;
    if (tr) pfTrace("frame %d: begin %ux%u particles=%d allocated=%d", frameNo, width_, height_, numParticles_, allocatedParticles_);

    applyPendingRequests();   // may reallocate buffers (waits for the GPU) - do it before the fence wait

    // Wait for the previous frame to finish. The fence is only RESET right before a submit
    // that is guaranteed to happen, otherwise the next wait would block forever.
    vkWaitForFences(device_, 1, &inFlightFence_, VK_TRUE, UINT64_MAX);

    uint32_t imageIndex = 0;
    VkResult result = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
        imageAvailableSemaphore_, VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        swapchainRecreateNeeded_ = true;   // rebuilt at the start of the next frame
        return;                            // fence is still signaled -> no deadlock
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        LOGE("vkAcquireNextImageKHR failed: %d", (int)result);
        if (result == VK_ERROR_DEVICE_LOST || result == VK_ERROR_SURFACE_LOST_KHR) fatalError_ = true;
        return;
    }

    if (tr) pfTrace("frame %d: acquired image %u (result %d)", frameNo, imageIndex, (int)result);

    // Snapshot everything the frame needs under the lock (UI thread may change it any time).
    ParticleParams p;
    int  nParticles, nTouch, pointSize;
    bool doInit, uploadTouch;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        p            = params_;
        nParticles   = numParticles_;
        nTouch       = numTouchPoints_;
        pointSize    = particleSize_;
        doInit       = needInitParticles_;
        needInitParticles_ = false;
        uploadTouch  = touchDirty_;
        touchDirty_  = false;
        if (uploadTouch && touchMapped_) {
            // Safe: the previous frame (the only reader of the staging buffer) has finished.
            memcpy(touchMapped_, touchPositions_.data(), sizeof(Vec2) * touchPositions_.size());
        }
    }
    // Never touch more particles than the GPU buffers hold (a size change is applied next frame).
    nParticles = std::min(nParticles, allocatedParticles_);
    if (nParticles <= 0) return;

    vkResetFences(device_, 1, &inFlightFence_);

    float bgR = ((p.bgColor >> 16) & 0xFF) / 255.0f;
    float bgG = ((p.bgColor >>  8) & 0xFF) / 255.0f;
    float bgB = ((p.bgColor      ) & 0xFF) / 255.0f;

    VkCommandBuffer cmd = graphicsCommandBuffers_[imageIndex];

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkResetCommandBuffer(cmd, 0);
    vkBeginCommandBuffer(cmd, &beginInfo);

    // 1) physics step (touch upload + optional init + update) - same submit as the draw
    recordCompute(cmd, p, nParticles, nTouch, doInit, uploadTouch);
    if (tr) pfTrace("frame %d: compute recorded (n=%d init=%d touchUpload=%d)", frameNo, nParticles, (int)doInit, (int)uploadTouch);

    // 2) draw
    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass        = renderPass_;
    rpInfo.framebuffer       = framebuffers_[imageIndex];
    rpInfo.renderArea.extent = swapchainExtent_;

    VkClearValue clearColor{};
    clearColor.color = {{bgR, bgG, bgB, 1.0f}};
    rpInfo.clearValueCount = 1;
    rpInfo.pClearValues    = &clearColor;

    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline_);

    VkBuffer vertexBuffers[] = {positionBuffer_, colorBuffer_};
    VkDeviceSize offsets[]   = {0, 0};
    vkCmdBindVertexBuffers(cmd, 0, 2, vertexBuffers, offsets);

    // Column-major ortho matrix: particle pixels [0..w]x[0..h] -> NDC. The compiled vertex shader
    // negates Y afterwards so particle y=0 (OpenGL convention, as in the original) is at the bottom.
    GraphicsPushConstants gpc{};
    float fw = (float)swapchainExtent_.width;
    float fh = (float)swapchainExtent_.height;
    gpc.mvpMatrix[0]  = 2.0f / fw;
    gpc.mvpMatrix[5]  = 2.0f / fh;
    gpc.mvpMatrix[10] = 1.0f;
    gpc.mvpMatrix[12] = -1.0f;
    gpc.mvpMatrix[13] = -1.0f;
    gpc.mvpMatrix[15] = 1.0f;
    gpc.pointSize = largePointsSupported_ ? (float)pointSize : 1.0f;

    vkCmdPushConstants(cmd, graphicsPipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT,
        0, sizeof(GraphicsPushConstants), &gpc);

    vkCmdDraw(cmd, (uint32_t)nParticles, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);
    if (tr) pfTrace("frame %d: command buffer recorded, submitting", frameNo);

    // Submit
    VkSemaphore renderDone = renderFinishedSemaphores_[imageIndex];
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

    VkSubmitInfo submitInfo{};
    submitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount   = 1;
    submitInfo.pWaitSemaphores      = &imageAvailableSemaphore_;
    submitInfo.pWaitDstStageMask    = waitStages;
    submitInfo.commandBufferCount   = 1;
    submitInfo.pCommandBuffers      = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores    = &renderDone;

    VkResult sr = vkQueueSubmit(graphicsQueue_, 1, &submitInfo, inFlightFence_);
    if (sr != VK_SUCCESS) {
        LOGE("vkQueueSubmit failed: %d - stopping renderer", (int)sr);
        fatalError_ = true;   // the fence was reset and will never signal; do not wait on it again
        return;
    }

    if (tr) pfTrace("frame %d: submitted, presenting", frameNo);

    // Present
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores    = &renderDone;
    presentInfo.swapchainCount     = 1;
    presentInfo.pSwapchains        = &swapchain_;
    presentInfo.pImageIndices      = &imageIndex;

    result = vkQueuePresentKHR(graphicsQueue_, &presentInfo);
    if (tr) pfTrace("frame %d: present result %d", frameNo, (int)result);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        swapchainRecreateNeeded_ = true;
    } else if (result == VK_SUBOPTIMAL_KHR) {
        // Expected on Android whenever the display is rotated and we use an IDENTITY pre-transform.
        // Real size changes arrive through onSurfaceChanged(), so nothing to do here.
    } else if (result != VK_SUCCESS) {
        LOGE("vkQueuePresentKHR failed: %d", (int)result);
        if (result == VK_ERROR_DEVICE_LOST || result == VK_ERROR_SURFACE_LOST_KHR) fatalError_ = true;
    }
}

void VulkanParticleEngine::recordCompute(VkCommandBuffer cmd, const ParticleParams& p,
                                         int nParticles, int nTouch,
                                         bool doInit, bool uploadTouch)
{
    if (uploadTouch) {
        VkBufferCopy copy{};
        copy.size = sizeof(Vec2) * MAX_TOUCH_POINTS;
        vkCmdCopyBuffer(cmd, touchStagingBuffer_, touchBuffer_, 1, &copy);

        VkMemoryBarrier b{};
        b.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 1, &b, 0, nullptr, 0, nullptr);
    }

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        computePipelineLayout_, 0, 1, &computeDescriptorSet_, 0, nullptr);

    ComputePushConstants pc{};
    pc.numParticles = (uint32_t)nParticles;
    pc.numTouch     = (uint32_t)nTouch;
    pc.attract      = p.attract;
    pc.drag         = p.drag;
    pc.width        = (float)width_;
    pc.height       = (float)height_;
    pc.slowH        = p.slowH;
    pc.slowS        = p.slowS;
    pc.slowV        = p.slowV;
    pc.fastH        = p.fastH;
    pc.fastS        = p.fastS;
    pc.fastV        = p.fastV;
    pc.hueDir       = p.hueDir;

    uint32_t groups = ((uint32_t)nParticles + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;

    if (doInit) {
        pc.frameSeed = frameSeed_++;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineInit_);
        vkCmdPushConstants(cmd, computePipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT,
            0, sizeof(ComputePushConstants), &pc);
        vkCmdDispatch(cmd, groups, 1, 1);

        VkMemoryBarrier b{};
        b.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 1, &b, 0, nullptr, 0, nullptr);
    }

    pc.frameSeed = frameSeed_++;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline_);
    vkCmdPushConstants(cmd, computePipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT,
        0, sizeof(ComputePushConstants), &pc);
    vkCmdDispatch(cmd, groups, 1, 1);

    // Compute writes positions/colors -> vertex input reads them as vertex attributes.
    VkMemoryBarrier b{};
    b.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        0, 1, &b, 0, nullptr, 0, nullptr);
}

// Rebuilds the swapchain when needed. Returns false if there is nothing to draw to right now.
bool VulkanParticleEngine::ensureSwapchain() {
    bool pending;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        pending = surfaceChangedPending_;
        surfaceChangedPending_ = false;
    }

    VkSurfaceCapabilitiesKHR caps{};
    if (pending && swapchainValid_ && !swapchainRecreateNeeded_) {
        if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &caps) == VK_SUCCESS) {
            if (caps.currentExtent.width  != swapchainExtent_.width  ||
                caps.currentExtent.height != swapchainExtent_.height ||
                caps.currentTransform     != swapchainTransform_) {
                swapchainRecreateNeeded_ = true;
            }
        }
    }

    if (swapchainValid_ && !swapchainRecreateNeeded_) return true;

    // Nothing to draw to yet (e.g. zero-sized surface): try again next frame.
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &caps) != VK_SUCCESS) return false;
    if (caps.currentExtent.width == 0 || caps.currentExtent.height == 0) return false;

    try {
        recreateSwapchain();
    } catch (const std::exception& e) {
        LOGE("recreateSwapchain failed: %s", e.what());
        swapchainValid_ = false;
        return false;
    }
    return swapchainValid_;
}

// Applies requests made by other threads. Render thread only.
void VulkanParticleEngine::applyPendingRequests() {
    bool doReset = false;
    int  wantParticles = 0;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        doReset = needResetAttraction_;
        needResetAttraction_ = false;
        wantParticles = numParticles_;
    }

    if (wantParticles != allocatedParticles_) {
        pfTrace("resizing particle buffers: %d -> %d", allocatedParticles_, wantParticles);
        reallocateParticleBuffers();
    }
    if (doReset) setDefaultAttractionPoints();
}

void VulkanParticleEngine::reallocateParticleBuffers() {
    vkDeviceWaitIdle(device_);
    destroyBuffer(positionBuffer_, positionMemory_);
    destroyBuffer(velocityBuffer_, velocityMemory_);
    destroyBuffer(colorBuffer_,    colorMemory_);
    createParticleBuffers();      // sets allocatedParticles_
    writeComputeDescriptors();    // descriptors must point at the new buffers
    std::lock_guard<std::mutex> lk(mutex_);
    needInitParticles_ = true;    // fresh buffers contain garbage
}

void VulkanParticleEngine::destroyBuffer(VkBuffer& buffer, VkDeviceMemory& memory) {
    if (buffer) { vkDestroyBuffer(device_, buffer, nullptr); buffer = VK_NULL_HANDLE; }
    if (memory) { vkFreeMemory(device_, memory, nullptr);    memory = VK_NULL_HANDLE; }
}

// ═══════════════════════════════════════════════════════════════
// SURFACE CHANGED
// ═══════════════════════════════════════════════════════════════

void VulkanParticleEngine::onSurfaceChanged(int width, int height) {
    // Called from the UI thread: must not touch Vulkan objects. The render thread rebuilds the
    // swapchain at the start of its next frame if the surface really changed.
    (void)width; (void)height;
    std::lock_guard<std::mutex> lk(mutex_);
    surfaceChangedPending_ = true;
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

    // How many particles can this GPU address? (storage buffer range and dispatch size limits)
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physicalDevice_, &props);
    {
        uint64_t byRange  = (uint64_t)props.limits.maxStorageBufferRange / sizeof(Vec4);
        uint64_t byGroups = (uint64_t)props.limits.maxComputeWorkGroupCount[0] * WORKGROUP_SIZE;
        uint64_t cap = std::min<uint64_t>((uint64_t)MAX_PARTICLES, std::min(byRange, byGroups));
        std::lock_guard<std::mutex> lk(mutex_);
        maxParticlesByDevice_ = (int)cap;
    }
    pfTrace("GPU: %s, max particles on this device: %d", props.deviceName, maxParticlesByDevice_);

    uint32_t queueCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &queueCount, nullptr);
    std::vector<VkQueueFamilyProperties> queues(queueCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &queueCount, queues.data());

    // One queue family must do graphics + compute + present: everything is submitted to one queue.
    bool found = false;
    for (uint32_t i = 0; i < queueCount; i++) {
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice_, i, surface_, &present);
        if ((queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
            (queues[i].queueFlags & VK_QUEUE_COMPUTE_BIT) && present) {
            graphicsFamily_ = i;
            computeFamily_  = i;
            found = true;
            break;
        }
    }
    if (!found) throw std::runtime_error("No queue family with graphics+compute+present support");
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

    // Only request largePoints if the GPU has it (requesting an unsupported feature makes
    // vkCreateDevice fail and the app would stay black).
    VkPhysicalDeviceFeatures supported{};
    vkGetPhysicalDeviceFeatures(physicalDevice_, &supported);
    largePointsSupported_ = (supported.largePoints == VK_TRUE);

    VkPhysicalDeviceFeatures features{};
    features.largePoints = supported.largePoints; // Needed for particle size > 1

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
        // UNORM: the shader's HSV->RGB output is stored as-is, exactly like the original GLES
        // framebuffer. An *_SRGB format would gamma-encode those values again and wash the
        // colors out.
        if ((f.format == VK_FORMAT_R8G8B8A8_UNORM || f.format == VK_FORMAT_B8G8R8A8_UNORM) &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            swapchainFormat_ = f.format;
            break;
        }
    }

    swapchainExtent_ = caps.currentExtent;
    if (swapchainExtent_.width == UINT32_MAX) {
        swapchainExtent_ = {width_, height_};
    }
    if (swapchainExtent_.width == 0 || swapchainExtent_.height == 0) {
        throw std::runtime_error("Surface has zero size");
    }
    swapchainTransform_ = caps.currentTransform;
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
    // IDENTITY: the app draws "upright" into a buffer of window size and Android's compositor rotates
    // it to the display. (Using currentTransform here would require us to pre-rotate the picture
    // ourselves and swap the extent - which we do not do - and the image came out sideways.)
    createInfo.preTransform = (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
        ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : caps.currentTransform;
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
    pfTrace("recreateSwapchain: begin");
    swapchainValid_ = false;
    vkDeviceWaitIdle(device_);
    uint32_t oldW = width_, oldH = height_;

    cleanupSwapchain();
    createSwapchain();
    createRenderPass();
    createFramebuffers();
    createGraphicsPipeline();
    createCommandBuffers();
    createRenderFinishedSemaphores();

    swapchainValid_          = true;
    swapchainRecreateNeeded_ = false;
    pfTrace("recreateSwapchain: done %ux%u", width_, height_);

    if (width_ != oldW || height_ != oldH) {
        // Same as the original app: a new surface size restarts the simulation.
        std::lock_guard<std::mutex> lk(mutex_);
        needInitParticles_   = true;
        needResetAttraction_ = true;
    }
}

void VulkanParticleEngine::cleanupSwapchain() {
    // Null-safe and idempotent.
    for (auto fb : framebuffers_)        if (fb) vkDestroyFramebuffer(device_, fb, nullptr);
    for (auto iv : swapchainImageViews_) if (iv) vkDestroyImageView(device_, iv, nullptr);
    for (auto sem : renderFinishedSemaphores_) if (sem) vkDestroySemaphore(device_, sem, nullptr);
    renderFinishedSemaphores_.clear();
    framebuffers_.clear();
    swapchainImageViews_.clear();
    swapchainImages_.clear();

    if (renderPass_)       { vkDestroyRenderPass(device_, renderPass_, nullptr);       renderPass_ = VK_NULL_HANDLE; }
    if (graphicsPipeline_) { vkDestroyPipeline(device_, graphicsPipeline_, nullptr);   graphicsPipeline_ = VK_NULL_HANDLE; }
    if (swapchain_)        { vkDestroySwapchainKHR(device_, swapchain_, nullptr);      swapchain_ = VK_NULL_HANDLE; }
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

    // The image-available semaphore is waited at COLOR_ATTACHMENT_OUTPUT; the layout transition
    // must not happen before that point.
    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    rpInfo.dependencyCount = 1;
    rpInfo.pDependencies   = &dep;

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
    int n;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        n = std::min(numParticles_, maxParticlesByDevice_);
    }

    // If the GPU cannot allocate that much memory, halve the count until it fits instead of
    // failing (a saved huge setting must never make the app unable to start).
    for (;;) {
        try {
            VkDeviceSize posSize   = sizeof(Vec2) * (VkDeviceSize)n;
            VkDeviceSize colorSize = sizeof(Vec4) * (VkDeviceSize)n;

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
            break;
        } catch (const std::exception& e) {
            destroyBuffer(positionBuffer_, positionMemory_);
            destroyBuffer(velocityBuffer_, velocityMemory_);
            destroyBuffer(colorBuffer_,    colorMemory_);
            if (n <= 50000) throw;
            n = std::max(50000, n / 2);
            pfTrace("particle buffer allocation failed (%s) - retrying with %d particles", e.what(), n);
        }
    }

    {
        std::lock_guard<std::mutex> lk(mutex_);
        numParticles_     = n;      // what is really allocated
        params_.numPart   = n;
    }
    allocatedParticles_ = n;
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

    // Contents are uploaded by the first frame (touchDirty_ is raised in initialize()).
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

    writeComputeDescriptors();
}

// (Re)points the compute descriptor set at the current buffers. GPU must be idle.
void VulkanParticleEngine::writeComputeDescriptors() {
    VkBuffer buffers[] = {positionBuffer_, velocityBuffer_, colorBuffer_, touchBuffer_};
    VkDeviceSize sizes[] = {
        sizeof(Vec2) * (VkDeviceSize)allocatedParticles_,
        sizeof(Vec2) * (VkDeviceSize)allocatedParticles_,
        sizeof(Vec4) * (VkDeviceSize)allocatedParticles_,
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
    pfTrace("loadShader: %s (asset manager %p)", path.c_str(), (void*)assetMgr);
    if (assetMgr == nullptr) {
        // This used to be a null-pointer crash inside AAssetManager_open.
        throw std::runtime_error("Asset manager was never set (cannot load " + path + ")");
    }
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
    blendAttachment.blendEnable         = VK_FALSE;   // original draws opaque points
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
    if (!graphicsPipelineLayout_) {   // created once; survives swapchain rebuilds
        vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr,
            &graphicsPipelineLayout_);
    }

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
    if (!graphicsCommandBuffers_.empty()) {
        vkFreeCommandBuffers(device_, commandPool_,
            (uint32_t)graphicsCommandBuffers_.size(), graphicsCommandBuffers_.data());
        graphicsCommandBuffers_.clear();
    }
    graphicsCommandBuffers_.resize(swapchainImages_.size());
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool        = commandPool_;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t)graphicsCommandBuffers_.size();
    vkAllocateCommandBuffers(device_, &allocInfo, graphicsCommandBuffers_.data());
}

void VulkanParticleEngine::createRenderFinishedSemaphores() {
    for (auto sem : renderFinishedSemaphores_) if (sem) vkDestroySemaphore(device_, sem, nullptr);
    renderFinishedSemaphores_.assign(swapchainImages_.size(), VK_NULL_HANDLE);

    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (auto& sem : renderFinishedSemaphores_) {
        if (vkCreateSemaphore(device_, &semInfo, nullptr, &sem) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create semaphore");
        }
    }
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
    // Idempotent: every handle is reset to VK_NULL_HANDLE after it is destroyed, so calling this
    // twice (failed initialize() followed by the destructor) is harmless.
    pfTrace("cleanup: begin");
    if (device_) {
        vkDeviceWaitIdle(device_);

        if (touchMapped_) {
            vkUnmapMemory(device_, touchStagingMemory_);
            touchMapped_ = nullptr;
        }

        destroyBuffer(positionBuffer_,     positionMemory_);
        destroyBuffer(velocityBuffer_,     velocityMemory_);
        destroyBuffer(colorBuffer_,        colorMemory_);
        destroyBuffer(touchBuffer_,        touchMemory_);
        destroyBuffer(touchStagingBuffer_, touchStagingMemory_);

        if (imageAvailableSemaphore_) { vkDestroySemaphore(device_, imageAvailableSemaphore_, nullptr); imageAvailableSemaphore_ = VK_NULL_HANDLE; }
        if (renderFinishedSemaphore_) { vkDestroySemaphore(device_, renderFinishedSemaphore_, nullptr); renderFinishedSemaphore_ = VK_NULL_HANDLE; }
        if (inFlightFence_)           { vkDestroyFence(device_, inFlightFence_, nullptr);               inFlightFence_ = VK_NULL_HANDLE; }

        if (commandPool_) { vkDestroyCommandPool(device_, commandPool_, nullptr); commandPool_ = VK_NULL_HANDLE; }
        graphicsCommandBuffers_.clear();   // freed together with the pool

        if (computeDescriptorPool_)      { vkDestroyDescriptorPool(device_, computeDescriptorPool_, nullptr);           computeDescriptorPool_ = VK_NULL_HANDLE; }
        if (computeDescriptorSetLayout_) { vkDestroyDescriptorSetLayout(device_, computeDescriptorSetLayout_, nullptr); computeDescriptorSetLayout_ = VK_NULL_HANDLE; }
        if (computePipeline_)            { vkDestroyPipeline(device_, computePipeline_, nullptr);                       computePipeline_ = VK_NULL_HANDLE; }
        if (computePipelineInit_)        { vkDestroyPipeline(device_, computePipelineInit_, nullptr);                   computePipelineInit_ = VK_NULL_HANDLE; }
        if (computePipelineLayout_)      { vkDestroyPipelineLayout(device_, computePipelineLayout_, nullptr);           computePipelineLayout_ = VK_NULL_HANDLE; }
        if (graphicsPipelineLayout_)     { vkDestroyPipelineLayout(device_, graphicsPipelineLayout_, nullptr);          graphicsPipelineLayout_ = VK_NULL_HANDLE; }

        cleanupSwapchain();

        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
        graphicsQueue_ = VK_NULL_HANDLE;
        computeQueue_  = VK_NULL_HANDLE;
    }

    if (surface_ && instance_) { vkDestroySurfaceKHR(instance_, surface_, nullptr); surface_ = VK_NULL_HANDLE; }
    if (instance_)             { vkDestroyInstance(instance_, nullptr);             instance_ = VK_NULL_HANDLE; }

    initialized_    = false;
    swapchainValid_ = false;
    pfTrace("cleanup: done");
}

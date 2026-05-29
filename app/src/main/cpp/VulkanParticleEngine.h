#ifndef VULKAN_PARTICLE_ENGINE_H
#define VULKAN_PARTICLE_ENGINE_H

#include <vulkan/vulkan.h>
#include <android/native_window.h>
#include <android/asset_manager.h>
#include <vector>
#include <string>
#include <array>

struct Vec2 { float x = 0.0f; float y = 0.0f; };
struct Vec4 { float r = 0.0f; float g = 0.0f; float b = 0.0f; float a = 1.0f; };

struct ParticleParams {
    float slowH, slowS, slowV;
    float fastH, fastS, fastV;
    int hueDir;
    float attract;
    float drag;
    float width, height;
    int numPart, numTouch, partSize;
    int bgColor;
};

struct ComputePushConstants {
    uint32_t numParticles;
    uint32_t numTouch;
    float attract;
    float drag;
    float width, height;
    float slowH, slowS, slowV;
    float fastH, fastS, fastV;
    int hueDir;
    uint32_t frameSeed;
};

// Must match the compiled particle_render.vert.spv push constant block:
//   layout(push_constant) uniform PushConstants { mat4 mvpMatrix; float pointSize; }
struct GraphicsPushConstants {
    float mvpMatrix[16];  // column-major orthographic matrix (64 bytes, offset 0)
    float pointSize;      // offset 64
};

class VulkanParticleEngine {
public:
    VulkanParticleEngine();
    ~VulkanParticleEngine();

    bool initialize(ANativeWindow* window);
    void setAssetManager(AAssetManager* mgr) { assetManager_ = mgr; }
    void setParameters(float slowH, float slowS, float slowV,
                       float fastH, float fastS, float fastV,
                       int hueDir, float attract, float drag,
                       float w, float h, int numPart, int numTouch,
                       int partSize, int bgColor);
    void initParticles();
    void initTesla369Pattern();
    void updateParticles();
    void setTouch(int index, float x, float y);
    void resetAttractionPoints();
    void render();
    void onSurfaceChanged(int width, int height);
    void cleanup();

private:
    AAssetManager*           assetManager_     = nullptr;
    VkInstance               instance_         = VK_NULL_HANDLE;
    VkPhysicalDevice         physicalDevice_   = VK_NULL_HANDLE;
    VkDevice                 device_           = VK_NULL_HANDLE;
    VkQueue                  graphicsQueue_    = VK_NULL_HANDLE;
    VkQueue                  computeQueue_     = VK_NULL_HANDLE;
    VkSurfaceKHR             surface_          = VK_NULL_HANDLE;
    VkSwapchainKHR           swapchain_        = VK_NULL_HANDLE;
    VkRenderPass             renderPass_       = VK_NULL_HANDLE;
    VkPipeline               computePipeline_     = VK_NULL_HANDLE;
    VkPipeline               computePipelineInit_ = VK_NULL_HANDLE;
    VkPipeline               graphicsPipeline_    = VK_NULL_HANDLE;
    VkPipelineLayout         computePipelineLayout_      = VK_NULL_HANDLE;
    VkPipelineLayout         graphicsPipelineLayout_     = VK_NULL_HANDLE;
    VkDescriptorSetLayout    computeDescriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool         computeDescriptorPool_      = VK_NULL_HANDLE;
    VkDescriptorSet          computeDescriptorSet_       = VK_NULL_HANDLE;
    VkCommandPool            commandPool_      = VK_NULL_HANDLE;
    VkBuffer                 positionBuffer_   = VK_NULL_HANDLE;
    VkDeviceMemory           positionMemory_   = VK_NULL_HANDLE;
    VkBuffer                 velocityBuffer_   = VK_NULL_HANDLE;
    VkDeviceMemory           velocityMemory_   = VK_NULL_HANDLE;
    VkBuffer                 colorBuffer_      = VK_NULL_HANDLE;
    VkDeviceMemory           colorMemory_      = VK_NULL_HANDLE;
    VkBuffer                 touchBuffer_      = VK_NULL_HANDLE;
    VkDeviceMemory           touchMemory_      = VK_NULL_HANDLE;
    VkBuffer                 touchStagingBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory           touchStagingMemory_ = VK_NULL_HANDLE;
    Vec2*                    touchMapped_      = nullptr;
    VkSemaphore              imageAvailableSemaphore_ = VK_NULL_HANDLE;
    VkSemaphore              renderFinishedSemaphore_ = VK_NULL_HANDLE;
    VkFence                  inFlightFence_    = VK_NULL_HANDLE;

    std::vector<VkImage>         swapchainImages_;
    std::vector<VkImageView>     swapchainImageViews_;
    std::vector<VkFramebuffer>   framebuffers_;
    std::vector<VkCommandBuffer> graphicsCommandBuffers_;
    VkFormat    swapchainFormat_;
    VkExtent2D  swapchainExtent_;

    uint32_t    graphicsFamily_ = 0;
    uint32_t    computeFamily_  = 0;
    bool        initialized_    = false;
    uint32_t    width_          = 0;
    uint32_t    height_         = 0;
    int         numParticles_   = 50000;
    int         numTouchPoints_ = 5;
    int         particleSize_   = 1;
    uint32_t    frameSeed_      = 0;

    ParticleParams         params_;
    std::vector<Vec2>      touchPositions_;

    void createInstance();
    void createSurface(ANativeWindow* window);
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createSwapchain();
    void recreateSwapchain();
    void cleanupSwapchain();
    void createRenderPass();
    void createFramebuffers();
    void createParticleBuffers();
    void createTouchBuffers();
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags props,
                      VkBuffer& buffer, VkDeviceMemory& memory);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props);
    void createComputeDescriptors();
    void createComputePipelines();
    void createGraphicsPipeline();
    void createCommandPool();
    void createCommandBuffers();
    void createSyncObjects();
    void updateTouchBuffer();
    void setDefaultAttractionPoints();
    VkShaderModule loadShader(const std::string& path);
    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer cmd);
};

#endif

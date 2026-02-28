#ifndef VULKAN_PARTICLE_ENGINE_H
#define VULKAN_PARTICLE_ENGINE_H

#include "VulkanHelpers.h"
#include <android/native_window.h>
#include <vector>

// Parameters matching RenderScript version
struct ParticleParams {
    float slowHue;
    float slowSaturation;
    float slowValue;
    float fastHue;
    float fastSaturation;
    float fastValue;
    int hueDirection;
    float f01AttractionCoef;
    float f01DragCoef;
    float width;
    float height;
    uint32_t numParticles;
    uint32_t numTouchPoints;
    uint32_t particleSize;
    uint32_t bgColor;
};

class VulkanParticleEngine {
public:
    VulkanParticleEngine();
    ~VulkanParticleEngine();
    
    // Initialize Vulkan with surface
    bool initialize(ANativeWindow* window);
    
    // Cleanup
    void cleanup();
    
    // Set parameters
    void setParameters(const ParticleParams& params);
    
    // Initialize particles (equivalent to initParticles())
    void initParticles();
    
    // Update particles (equivalent to updateParticles())
    void updateParticles();
    
    // Set touch point position
    void setTouch(uint32_t index, float x, float y);
    
    // Render frame
    void render();
    
    // Handle surface changes
    void onSurfaceChanged(int width, int height);
    
private:
    // Vulkan instance and device
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue computeQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    
    // Surface and swapchain
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    std::vector<VkFramebuffer> swapchainFramebuffers;
    VkFormat swapchainImageFormat;
    VkExtent2D swapchainExtent;
    
    // Render pass
    VkRenderPass renderPass = VK_NULL_HANDLE;
    
    // Command pools and buffers
    VkCommandPool computeCommandPool = VK_NULL_HANDLE;
    VkCommandPool graphicsCommandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;
    
    // Compute resources
    VkDescriptorSetLayout computeDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool computeDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet computeInitDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet computeUpdateDescriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout computePipelineLayout = VK_NULL_HANDLE;
    VkPipeline initPipeline = VK_NULL_HANDLE;
    VkPipeline updatePipeline = VK_NULL_HANDLE;
    
    // Graphics resources
    VkDescriptorSetLayout graphicsDescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout graphicsPipelineLayout = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;
    
    // Particle buffers
    VkBuffer touchBuffer = VK_NULL_HANDLE;
    VkDeviceMemory touchBufferMemory = VK_NULL_HANDLE;
    VkBuffer positionBuffer = VK_NULL_HANDLE;
    VkDeviceMemory positionBufferMemory = VK_NULL_HANDLE;
    VkBuffer velocityBuffer = VK_NULL_HANDLE;
    VkDeviceMemory velocityBufferMemory = VK_NULL_HANDLE;
    VkBuffer colorBuffer = VK_NULL_HANDLE;
    VkDeviceMemory colorBufferMemory = VK_NULL_HANDLE;
    
    // Staging buffers for touch updates
    VkBuffer touchStagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory touchStagingMemory = VK_NULL_HANDLE;
    float* touchMapped = nullptr;
    
    // Synchronization
    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    uint32_t currentFrame = 0;
    static const int MAX_FRAMES_IN_FLIGHT = 2;
    
    // Parameters
    ParticleParams params;
    uint32_t randomSeed;
    
    // Initialization helpers
    bool createInstance();
    bool pickPhysicalDevice();
    bool createLogicalDevice();
    bool createSurface(ANativeWindow* window);
    bool createSwapchain();
    bool createImageViews();
    bool createRenderPass();
    bool createFramebuffers();
    bool createCommandPools();
    bool createBuffers();
    bool createComputeDescriptorSetLayout();
    bool createComputeDescriptorPool();
    bool createComputeDescriptorSets();
    bool createComputePipelines();
    bool createGraphicsPipeline();
    bool createCommandBuffers();
    void createOrthoMatrix(float* matrix, float left, float right, float bottom, float top, float near, float far);
    bool createSyncObjects();
    
    // Shader loading
    std::vector<uint32_t> loadShader(const char* name);
    
    // Helper methods
    void updateTouchBuffer();
    void recreateSwapchain();
    void cleanupSwapchain();
};

#endif // VULKAN_PARTICLE_ENGINE_H

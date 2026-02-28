#ifndef VULKAN_HELPERS_H
#define VULKAN_HELPERS_H

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>
#include <android/log.h>
#include <vector>
#include <string>

// Logging macros
#define LOG_TAG "VulkanParticles"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

// Vulkan error checking macro
#define VK_CHECK(result) \
    if (result != VK_SUCCESS) { \
        LOGE("Vulkan error at %s:%d - Result: %d", __FILE__, __LINE__, result); \
    }

namespace VulkanHelpers {

// Find memory type with required properties
uint32_t findMemoryType(VkPhysicalDevice physicalDevice, 
                       uint32_t typeFilter, 
                       VkMemoryPropertyFlags properties);

// Create buffer with memory
bool createBuffer(VkDevice device,
                 VkPhysicalDevice physicalDevice,
                 VkDeviceSize size,
                 VkBufferUsageFlags usage,
                 VkMemoryPropertyFlags properties,
                 VkBuffer& buffer,
                 VkDeviceMemory& bufferMemory);

// Destroy buffer and free memory
void destroyBuffer(VkDevice device, VkBuffer buffer, VkDeviceMemory memory);

// Create shader module from SPIR-V bytecode
VkShaderModule createShaderModule(VkDevice device, 
                                 const std::vector<uint32_t>& code);

// Load shader SPIR-V from file (for testing)
std::vector<uint32_t> loadShaderFromFile(const char* filename);

// Create compute pipeline
bool createComputePipeline(VkDevice device,
                          VkShaderModule shaderModule,
                          VkPipelineLayout pipelineLayout,
                          VkPipeline& pipeline);

// Create graphics pipeline for rendering points
bool createGraphicsPipeline(VkDevice device,
                           VkShaderModule vertShader,
                           VkShaderModule fragShader,
                           VkPipelineLayout pipelineLayout,
                           VkRenderPass renderPass,
                           VkPipeline& pipeline);

// Find suitable queue family indices
struct QueueFamilyIndices {
    uint32_t graphicsFamily = UINT32_MAX;
    uint32_t computeFamily = UINT32_MAX;
    uint32_t presentFamily = UINT32_MAX;
    
    bool isComplete() const {
        return graphicsFamily != UINT32_MAX && 
               computeFamily != UINT32_MAX;
    }
};

QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface);

// Check if physical device is suitable
bool isDeviceSuitable(VkPhysicalDevice device, VkSurfaceKHR surface);

// Create command buffer
VkCommandBuffer beginSingleTimeCommands(VkDevice device, VkCommandPool commandPool);
void endSingleTimeCommands(VkDevice device, VkQueue queue, 
                          VkCommandPool commandPool, VkCommandBuffer commandBuffer);

// Copy buffer utility
void copyBuffer(VkDevice device, VkQueue queue, VkCommandPool commandPool,
               VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

} // namespace VulkanHelpers

#endif // VULKAN_HELPERS_H

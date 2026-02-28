#include "VulkanParticleEngine.h"
#include <cstring>
#include <cmath>
#include <random>
#include <algorithm>
#include <set>

// Embedded SPIR-V shader bytecode
// In production, you'd load these from assets or compile at build time
// For now, we'll declare them as external and load them properly

VulkanParticleEngine::VulkanParticleEngine() {
    // Initialize random seed
    std::random_device rd;
    randomSeed = rd();
    
    // Set default parameters
    params.numParticles = 50000;
    params.numTouchPoints = 5;
    params.particleSize = 2;
    params.slowHue = 0.66f;  // Blue
    params.slowSaturation = 1.0f;
    params.slowValue = 1.0f;
    params.fastHue = 0.0f;   // Red
    params.fastSaturation = 1.0f;
    params.fastValue = 1.0f;
    params.hueDirection = 0;
    params.f01AttractionCoef = 100.0f;
    params.f01DragCoef = 0.96f;
    params.width = 1080.0f;
    params.height = 1920.0f;
    params.bgColor = 0xFF000000;
}

VulkanParticleEngine::~VulkanParticleEngine() {
    cleanup();
}

bool VulkanParticleEngine::initialize(ANativeWindow* window) {
    LOGI("Initializing Vulkan Particle Engine...");
    
    if (!createInstance()) {
        LOGE("Failed to create Vulkan instance");
        return false;
    }
    
    if (!createSurface(window)) {
        LOGE("Failed to create surface");
        return false;
    }
    
    if (!pickPhysicalDevice()) {
        LOGE("Failed to pick physical device");
        return false;
    }
    
    if (!createLogicalDevice()) {
        LOGE("Failed to create logical device");
        return false;
    }
    
    if (!createSwapchain()) {
        LOGE("Failed to create swapchain");
        return false;
    }
    
    if (!createImageViews()) {
        LOGE("Failed to create image views");
        return false;
    }
    
    if (!createRenderPass()) {
        LOGE("Failed to create render pass");
        return false;
    }
    
    if (!createFramebuffers()) {
        LOGE("Failed to create framebuffers");
        return false;
    }
    
    if (!createCommandPools()) {
        LOGE("Failed to create command pools");
        return false;
    }
    
    if (!createBuffers()) {
        LOGE("Failed to create buffers");
        return false;
    }
    
    if (!createComputeDescriptorSetLayout()) {
        LOGE("Failed to create compute descriptor set layout");
        return false;
    }
    
    if (!createComputeDescriptorPool()) {
        LOGE("Failed to create compute descriptor pool");
        return false;
    }
    
    if (!createComputeDescriptorSets()) {
        LOGE("Failed to create compute descriptor sets");
        return false;
    }
    
    if (!createComputePipelines()) {
        LOGE("Failed to create compute pipelines");
        return false;
    }
    
    if (!createGraphicsPipeline()) {
        LOGE("Failed to create graphics pipeline");
        return false;
    }
    
    if (!createCommandBuffers()) {
        LOGE("Failed to create command buffers");
        return false;
    }
    
    if (!createSyncObjects()) {
        LOGE("Failed to create sync objects");
        return false;
    }
    
    LOGI("Vulkan initialization complete!");
    return true;
}

bool VulkanParticleEngine::createInstance() {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Particle Flow";
    appInfo.applicationVersion = VK_MAKE_VERSION(2, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;
    
    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    
    // Required extensions for Android
    const char* extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_ANDROID_SURFACE_EXTENSION_NAME
    };
    createInfo.enabledExtensionCount = 2;
    createInfo.ppEnabledExtensionNames = extensions;
    createInfo.enabledLayerCount = 0;
    
    VkResult result = vkCreateInstance(&createInfo, nullptr, &instance);
    VK_CHECK(result);
    return result == VK_SUCCESS;
}

bool VulkanParticleEngine::createSurface(ANativeWindow* window) {
    VkAndroidSurfaceCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    createInfo.window = window;
    
    VkResult result = vkCreateAndroidSurfaceKHR(instance, &createInfo, nullptr, &surface);
    VK_CHECK(result);
    return result == VK_SUCCESS;
}

bool VulkanParticleEngine::pickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    
    if (deviceCount == 0) {
        LOGE("Failed to find GPUs with Vulkan support!");
        return false;
    }
    
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
    
    // Pick the first suitable device
    for (const auto& device : devices) {
        if (VulkanHelpers::isDeviceSuitable(device, surface)) {
            physicalDevice = device;
            
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(physicalDevice, &props);
            LOGI("Selected GPU: %s", props.deviceName);
            return true;
        }
    }
    
    LOGE("Failed to find suitable GPU!");
    return false;
}

bool VulkanParticleEngine::createLogicalDevice() {
    VulkanHelpers::QueueFamilyIndices indices = 
        VulkanHelpers::findQueueFamilies(physicalDevice, surface);
    
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {
        indices.graphicsFamily, 
        indices.computeFamily,
        indices.presentFamily
    };
    
    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }
    
    VkPhysicalDeviceFeatures deviceFeatures{};
    
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    
    const char* deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    createInfo.enabledExtensionCount = 1;
    createInfo.ppEnabledExtensionNames = deviceExtensions;
    createInfo.enabledLayerCount = 0;
    
    VkResult result = vkCreateDevice(physicalDevice, &createInfo, nullptr, &device);
    VK_CHECK(result);
    
    if (result == VK_SUCCESS) {
        vkGetDeviceQueue(device, indices.graphicsFamily, 0, &graphicsQueue);
        vkGetDeviceQueue(device, indices.computeFamily, 0, &computeQueue);
        vkGetDeviceQueue(device, indices.presentFamily, 0, &presentQueue);
        return true;
    }
    
    return false;
}

bool VulkanParticleEngine::createSwapchain() {
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);
    
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());
    
    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, presentModes.data());
    
    // Choose surface format
    VkSurfaceFormatKHR surfaceFormat = formats[0];
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB && 
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            surfaceFormat = format;
            break;
        }
    }
    
    // Choose present mode (prefer mailbox for lower latency)
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    for (const auto& mode : presentModes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            presentMode = mode;
            break;
        }
    }
    
    // Choose swap extent
    swapchainExtent = capabilities.currentExtent;
    
    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }
    
    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = swapchainExtent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    
    VulkanHelpers::QueueFamilyIndices indices = 
        VulkanHelpers::findQueueFamilies(physicalDevice, surface);
    uint32_t queueFamilyIndices[] = {indices.graphicsFamily, indices.presentFamily};
    
    if (indices.graphicsFamily != indices.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;
    
    VkResult result = vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain);
    VK_CHECK(result);
    
    if (result == VK_SUCCESS) {
        swapchainImageFormat = surfaceFormat.format;
        
        vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
        swapchainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data());
        
        LOGI("Created swapchain with %d images, extent: %dx%d", 
             imageCount, swapchainExtent.width, swapchainExtent.height);
        
        return true;
    }
    
    return false;
}

bool VulkanParticleEngine::createImageViews() {
    swapchainImageViews.resize(swapchainImages.size());
    
    for (size_t i = 0; i < swapchainImages.size(); i++) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = swapchainImages[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = swapchainImageFormat;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;
        
        VkResult result = vkCreateImageView(device, &createInfo, nullptr, &swapchainImageViews[i]);
        VK_CHECK(result);
        
        if (result != VK_SUCCESS) {
            return false;
        }
    }
    
    return true;
}


bool VulkanParticleEngine::createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapchainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    
    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;
    
    VkResult result = vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass);
    VK_CHECK(result);
    return result == VK_SUCCESS;
}

bool VulkanParticleEngine::createFramebuffers() {
    swapchainFramebuffers.resize(swapchainImageViews.size());
    
    for (size_t i = 0; i < swapchainImageViews.size(); i++) {
        VkImageView attachments[] = { swapchainImageViews[i] };
        
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = swapchainExtent.width;
        framebufferInfo.height = swapchainExtent.height;
        framebufferInfo.layers = 1;
        
        VkResult result = vkCreateFramebuffer(device, &framebufferInfo, nullptr, 
                                             &swapchainFramebuffers[i]);
        VK_CHECK(result);
        
        if (result != VK_SUCCESS) {
            return false;
        }
    }
    
    return true;
}

bool VulkanParticleEngine::createCommandPools() {
    VulkanHelpers::QueueFamilyIndices queueFamilyIndices = 
        VulkanHelpers::findQueueFamilies(physicalDevice, surface);
    
    // Graphics command pool
    VkCommandPoolCreateInfo graphicsPoolInfo{};
    graphicsPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    graphicsPoolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily;
    graphicsPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    
    VkResult result = vkCreateCommandPool(device, &graphicsPoolInfo, nullptr, &graphicsCommandPool);
    VK_CHECK(result);
    if (result != VK_SUCCESS) return false;
    
    // Compute command pool
    VkCommandPoolCreateInfo computePoolInfo{};
    computePoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    computePoolInfo.queueFamilyIndex = queueFamilyIndices.computeFamily;
    computePoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    
    result = vkCreateCommandPool(device, &computePoolInfo, nullptr, &computeCommandPool);
    VK_CHECK(result);
    return result == VK_SUCCESS;
}

bool VulkanParticleEngine::createBuffers() {
    LOGI("Creating particle buffers for %d particles, %d touch points", 
         params.numParticles, params.numTouchPoints);
    
    // Touch buffer (staging + device)
    VkDeviceSize touchBufferSize = sizeof(float) * 2 * params.numTouchPoints;
    
    if (!VulkanHelpers::createBuffer(device, physicalDevice, touchBufferSize,
                                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                    touchStagingBuffer, touchStagingMemory)) {
        return false;
    }
    
    // Map staging buffer permanently
    vkMapMemory(device, touchStagingMemory, 0, touchBufferSize, 0, (void**)&touchMapped);
    
    // Initialize touch buffer with negative values (disabled)
    for (uint32_t i = 0; i < params.numTouchPoints * 2; i++) {
        touchMapped[i] = -1.0f;
    }
    
    if (!VulkanHelpers::createBuffer(device, physicalDevice, touchBufferSize,
                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                    touchBuffer, touchBufferMemory)) {
        return false;
    }
    
    // Position buffer
    VkDeviceSize positionBufferSize = sizeof(float) * 2 * params.numParticles;
    if (!VulkanHelpers::createBuffer(device, physicalDevice, positionBufferSize,
                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                    positionBuffer, positionBufferMemory)) {
        return false;
    }
    
    // Velocity buffer
    VkDeviceSize velocityBufferSize = sizeof(float) * 2 * params.numParticles;
    if (!VulkanHelpers::createBuffer(device, physicalDevice, velocityBufferSize,
                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                    velocityBuffer, velocityBufferMemory)) {
        return false;
    }
    
    // Color buffer
    VkDeviceSize colorBufferSize = sizeof(float) * 4 * params.numParticles;
    if (!VulkanHelpers::createBuffer(device, physicalDevice, colorBufferSize,
                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                    colorBuffer, colorBufferMemory)) {
        return false;
    }
    
    LOGI("Buffers created successfully");
    return true;
}

bool VulkanParticleEngine::createComputeDescriptorSetLayout() {
    // Descriptor set layout for compute shaders
    // Binding 0: Touch buffer
    // Binding 1: Position buffer
    // Binding 2: Velocity buffer
    // Binding 3: Color buffer
    
    VkDescriptorSetLayoutBinding bindings[4] = {};
    
    // Touch buffer
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    // Position buffer
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    // Velocity buffer
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    // Color buffer
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 4;
    layoutInfo.pBindings = bindings;
    
    VkResult result = vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, 
                                                  &computeDescriptorSetLayout);
    VK_CHECK(result);
    return result == VK_SUCCESS;
}

bool VulkanParticleEngine::createComputeDescriptorPool() {
    // Create descriptor pool for compute shaders
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = 8; // 4 bindings × 2 descriptor sets (init + update)
    
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 2;
    
    VkResult result = vkCreateDescriptorPool(device, &poolInfo, nullptr, &computeDescriptorPool);
    VK_CHECK(result);
    return result == VK_SUCCESS;
}

bool VulkanParticleEngine::createComputeDescriptorSets() {
    // Allocate descriptor sets
    VkDescriptorSetLayout layouts[] = { computeDescriptorSetLayout, computeDescriptorSetLayout };
    
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = computeDescriptorPool;
    allocInfo.descriptorSetCount = 2;
    allocInfo.pSetLayouts = layouts;
    
    VkDescriptorSet sets[2];
    VkResult result = vkAllocateDescriptorSets(device, &allocInfo, sets);
    VK_CHECK(result);
    
    if (result != VK_SUCCESS) {
        return false;
    }
    
    computeInitDescriptorSet = sets[0];
    computeUpdateDescriptorSet = sets[1];
    
    // Update descriptor sets
    VkDescriptorBufferInfo touchBufferInfo{};
    touchBufferInfo.buffer = touchBuffer;
    touchBufferInfo.offset = 0;
    touchBufferInfo.range = VK_WHOLE_SIZE;
    
    VkDescriptorBufferInfo positionBufferInfo{};
    positionBufferInfo.buffer = positionBuffer;
    positionBufferInfo.offset = 0;
    positionBufferInfo.range = VK_WHOLE_SIZE;
    
    VkDescriptorBufferInfo velocityBufferInfo{};
    velocityBufferInfo.buffer = velocityBuffer;
    velocityBufferInfo.offset = 0;
    velocityBufferInfo.range = VK_WHOLE_SIZE;
    
    VkDescriptorBufferInfo colorBufferInfo{};
    colorBufferInfo.buffer = colorBuffer;
    colorBufferInfo.offset = 0;
    colorBufferInfo.range = VK_WHOLE_SIZE;
    
    // Update both descriptor sets (init and update use same buffers)
    for (int i = 0; i < 2; i++) {
        VkWriteDescriptorSet descriptorWrites[4] = {};
        
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = sets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &touchBufferInfo;
        
        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = sets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pBufferInfo = &positionBufferInfo;
        
        descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[2].dstSet = sets[i];
        descriptorWrites[2].dstBinding = 2;
        descriptorWrites[2].dstArrayElement = 0;
        descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[2].descriptorCount = 1;
        descriptorWrites[2].pBufferInfo = &velocityBufferInfo;
        
        descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[3].dstSet = sets[i];
        descriptorWrites[3].dstBinding = 3;
        descriptorWrites[3].dstArrayElement = 0;
        descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[3].descriptorCount = 1;
        descriptorWrites[3].pBufferInfo = &colorBufferInfo;
        
        vkUpdateDescriptorSets(device, 4, descriptorWrites, 0, nullptr);
    }
    
    return true;
}


#include "VulkanParticleEngine.h"
#include <cstring>
#include <cmath>
#include <random>
#include <algorithm>
#include <set>
#include <set>

// ============================================================================
// CONSTRUCTOR & DESTRUCTOR
// ============================================================================

bool VulkanParticleEngine::createComputePipelines() {
    // Load shader modules from compiled SPIR-V
    std::vector<uint32_t> initShaderCode = loadShader("particle_init.spv");
    std::vector<uint32_t> updateShaderCode = loadShader("particle_update.spv");
    
    VkShaderModule initShader = VulkanHelpers::createShaderModule(device, initShaderCode);
    VkShaderModule updateShader = VulkanHelpers::createShaderModule(device, updateShaderCode);
    
    // Push constant range for compute parameters
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(ParticleParams);
    
    // Create pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &computeDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    
    vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &computePipelineLayout);
    
    // Create init pipeline
    VulkanHelpers::createComputePipeline(device, initShader, computePipelineLayout, initPipeline);
    
    // Create update pipeline
    VulkanHelpers::createComputePipeline(device, updateShader, computePipelineLayout, updatePipeline);
    
    vkDestroyShaderModule(device, initShader, nullptr);
    vkDestroyShaderModule(device, updateShader, nullptr);
    
    return true;
}

bool VulkanParticleEngine::createGraphicsPipeline() {
    std::vector<uint32_t> vertShaderCode = loadShader("particle_render_vert.spv");
    std::vector<uint32_t> fragShaderCode = loadShader("particle_render_frag.spv");
    
    VkShaderModule vertShader = VulkanHelpers::createShaderModule(device, vertShaderCode);
    VkShaderModule fragShader = VulkanHelpers::createShaderModule(device, fragShaderCode);
    
    // Push constant for MVP matrix + point size
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(float) * 16 + sizeof(float); // mat4 + float
    
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    
    vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &graphicsPipelineLayout);
    
    VulkanHelpers::createGraphicsPipeline(device, vertShader, fragShader, 
                                         graphicsPipelineLayout, renderPass, graphicsPipeline);
    
    vkDestroyShaderModule(device, vertShader, nullptr);
    vkDestroyShaderModule(device, fragShader, nullptr);
    
    return true;
}

void VulkanParticleEngine::initParticles() {
    // Record compute command to initialize particles
    VkCommandBuffer commandBuffer = VulkanHelpers::beginSingleTimeCommands(device, computeCommandPool);
    
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, initPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, 
                           computePipelineLayout, 0, 1, &computeInitDescriptorSet, 0, nullptr);
    
    // Update push constants with current parameters
    vkCmdPushConstants(commandBuffer, computePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                      0, sizeof(ParticleParams), &params);
    
    // Dispatch compute shader (256 threads per workgroup)
    uint32_t workgroupCount = (params.numParticles + 255) / 256;
    vkCmdDispatch(commandBuffer, workgroupCount, 1, 1);
    
    VulkanHelpers::endSingleTimeCommands(device, computeQueue, computeCommandPool, commandBuffer);
    
    LOGI("Particles initialized: %d particles", params.numParticles);
}

void VulkanParticleEngine::updateParticles() {
    // Update touch buffer
    updateTouchBuffer();
    
    // Record compute command to update particles
    VkCommandBuffer commandBuffer = VulkanHelpers::beginSingleTimeCommands(device, computeCommandPool);
    
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, updatePipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                           computePipelineLayout, 0, 1, &computeUpdateDescriptorSet, 0, nullptr);
    
    vkCmdPushConstants(commandBuffer, computePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                      0, sizeof(ParticleParams), &params);
    
    uint32_t workgroupCount = (params.numParticles + 255) / 256;
    vkCmdDispatch(commandBuffer, workgroupCount, 1, 1);
    
    VulkanHelpers::endSingleTimeCommands(device, computeQueue, computeCommandPool, commandBuffer);
}

void VulkanParticleEngine::updateTouchBuffer() {
    // Touch data is already in touchMapped (permanently mapped staging buffer)
    // Copy from staging to device-local buffer
    VulkanHelpers::copyBuffer(device, graphicsQueue, graphicsCommandPool,
                             touchStagingBuffer, touchBuffer, 
                             sizeof(float) * 2 * params.numTouchPoints);
}

void VulkanParticleEngine::setTouch(uint32_t index, float x, float y) {
    if (index >= params.numTouchPoints || !touchMapped) {
        return;
    }
    
    touchMapped[index * 2] = x;
    touchMapped[index * 2 + 1] = y;
}

void VulkanParticleEngine::render() {
    // Wait for previous frame
    vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
    
    // Acquire image from swapchain
    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                                           imageAvailableSemaphores[currentFrame], 
                                           VK_NULL_HANDLE, &imageIndex);
    
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return;
    }
    
    vkResetFences(device, 1, &inFlightFences[currentFrame]);
    
    // Record command buffer
    VkCommandBuffer commandBuffer = commandBuffers[currentFrame];
    vkResetCommandBuffer(commandBuffer, 0);
    
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    
    // Begin render pass
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = swapchainFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapchainExtent;
    
    VkClearValue clearColor = {{{
        (params.bgColor >> 16 & 0xFF) / 255.0f,
        (params.bgColor >> 8 & 0xFF) / 255.0f,
        (params.bgColor & 0xFF) / 255.0f,
        1.0f
    }}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;
    
    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    
    // Bind graphics pipeline
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
    
    // Set viewport and scissor
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)swapchainExtent.width;
    viewport.height = (float)swapchainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchainExtent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    
    // Create orthographic projection matrix
    float mvpMatrix[16];
    createOrthoMatrix(mvpMatrix, 0, params.width, 0, params.height, -1, 1);
    
    // Push constants (MVP + point size)
    struct {
        float mvp[16];
        float pointSize;
    } pushData;
    memcpy(pushData.mvp, mvpMatrix, sizeof(float) * 16);
    pushData.pointSize = (float)params.particleSize;
    
    vkCmdPushConstants(commandBuffer, graphicsPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                      0, sizeof(pushData), &pushData);
    
    // Bind vertex buffers
    VkBuffer vertexBuffers[] = { positionBuffer, colorBuffer };
    VkDeviceSize offsets[] = { 0, 0 };
    vkCmdBindVertexBuffers(commandBuffer, 0, 2, vertexBuffers, offsets);
    
    // Draw particles
    vkCmdDraw(commandBuffer, params.numParticles, 1, 0, 0);
    
    vkCmdEndRenderPass(commandBuffer);
    vkEndCommandBuffer(commandBuffer);
    
    // Submit command buffer
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    
    VkSemaphore waitSemaphores[] = { imageAvailableSemaphores[currentFrame] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    
    VkSemaphore signalSemaphores[] = { renderFinishedSemaphores[currentFrame] };
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;
    
    vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]);
    
    // Present
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    
    VkSwapchainKHR swapchains[] = { swapchain };
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;
    
    vkQueuePresentKHR(presentQueue, &presentInfo);
    
    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void VulkanParticleEngine::createOrthoMatrix(float* matrix, float left, float right, 
                                            float bottom, float top, float near, float far) {
    memset(matrix, 0, sizeof(float) * 16);
    matrix[0] = 2.0f / (right - left);
    matrix[5] = 2.0f / (top - bottom);
    matrix[10] = -2.0f / (far - near);
    matrix[12] = -(right + left) / (right - left);
    matrix[13] = -(top + bottom) / (top - bottom);
    matrix[14] = -(far + near) / (far - near);
    matrix[15] = 1.0f;
}

void VulkanParticleEngine::cleanup() {
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
        
        // Cleanup all Vulkan resources...
        // (Destroy buffers, pipelines, descriptors, swapchain, etc.)
        
        LOGI("Vulkan cleanup complete");
    }
}

bool VulkanParticleEngine::createCommandBuffers() {
    commandBuffers.resize(swapchainFramebuffers.size());

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = graphicsCommandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t)commandBuffers.size();

    if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
        LOGE("Failed to allocate command buffers");
        return false;
    }
    return true;
}

bool VulkanParticleEngine::createSyncObjects() {
    imageAvailableSemaphores.resize(2);
    renderFinishedSemaphores.resize(2);
    inFlightFences.resize(2);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < 2; i++) {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS) {
            LOGE("Failed to create sync objects");
            return false;
        }
    }
    return true;
}

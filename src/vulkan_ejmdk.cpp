#include "vulkan_ejmdk.h"

#include "embedded_ejmdk_shader.h"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace ejmdk {
namespace {

[[nodiscard]] std::runtime_error VkError(const VkResult result, const char* message) {
    std::ostringstream stream;
    stream << message << " (VkResult=" << static_cast<int>(result) << ")";
    return std::runtime_error(stream.str());
}

void ThrowIfFailed(const VkResult result, const char* message) {
    if (result != VK_SUCCESS) {
        throw VkError(result, message);
    }
}

struct BufferResource {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    void* mapped = nullptr;
};

struct ImageResource {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct PushConstants {
    std::int32_t srcWidth = 0;
    std::int32_t srcHeight = 0;
    std::int32_t dstWidth = 0;
    std::int32_t dstHeight = 0;
    float scale = 1.0f;
    float time = 1.0f;
    std::int32_t hasPrev = 0;
    float sharpen = 1.0f;
};

struct LayoutInfo {
    VkPipelineStageFlags stageMask;
    VkAccessFlags accessMask;
};

[[nodiscard]] LayoutInfo DescribeLayout(const VkImageLayout layout) {
    switch (layout) {
    case VK_IMAGE_LAYOUT_UNDEFINED:
        return {VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0};
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        return {VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT};
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        return {VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT};
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        return {VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT};
    case VK_IMAGE_LAYOUT_GENERAL:
        return {VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
    default:
        return {VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT};
    }
}

}  // namespace

struct VulkanEjmdkProcessor::Impl {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties physicalDeviceProperties{};
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    VkDevice device = VK_NULL_HANDLE;
    std::uint32_t queueFamilyIndex = 0;
    VkQueue queue = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;

    std::uint32_t sourceWidth = 0;
    std::uint32_t sourceHeight = 0;
    std::uint32_t outputWidth = 0;
    std::uint32_t outputHeight = 0;
    std::uint32_t scaleFactor = 1;

    BufferResource prevUploadBuffer;
    BufferResource currUploadBuffer;
    BufferResource nodeUploadBuffer;
    BufferResource readbackBuffer;

    ImageResource prevImage;
    ImageResource currImage;
    ImageResource nodeImage;
    ImageResource outputImage;

    Impl() {
        CreateInstance();
        PickPhysicalDevice();
        CreateDevice();
        CreateCommandPool();
        CreateCommandBuffer();
        CreateDescriptorSetLayout();
        CreatePipeline();
        CreateDescriptorPool();
        AllocateDescriptorSet();
        CreateSampler();
    }

    ~Impl() {
        ResetFrameResources();
        if (sampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, sampler, nullptr);
        }
        if (descriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        }
        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, pipeline, nullptr);
        }
        if (pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        }
        if (descriptorSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
        }
        if (commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device, commandPool, nullptr);
        }
        if (device != VK_NULL_HANDLE) {
            vkDestroyDevice(device, nullptr);
        }
        if (instance != VK_NULL_HANDLE) {
            vkDestroyInstance(instance, nullptr);
        }
    }

    void CreateInstance() {
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "EJMKD";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "EJMKD";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_2;

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        ThrowIfFailed(vkCreateInstance(&createInfo, nullptr, &instance), "Failed to create Vulkan instance");
    }

    void PickPhysicalDevice() {
        std::uint32_t deviceCount = 0;
        ThrowIfFailed(vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr), "Failed to enumerate Vulkan physical devices");
        if (deviceCount == 0U) {
            throw std::runtime_error("No Vulkan-capable GPU was found.");
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        ThrowIfFailed(vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data()), "Failed to enumerate Vulkan device handles");

        for (const VkPhysicalDevice candidate : devices) {
            std::uint32_t queueCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, nullptr);
            std::vector<VkQueueFamilyProperties> queueProperties(queueCount);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, queueProperties.data());

            for (std::uint32_t index = 0; index < queueCount; ++index) {
                if ((queueProperties[index].queueFlags & VK_QUEUE_COMPUTE_BIT) == 0U) {
                    continue;
                }

                VkFormatProperties rgba8Properties{};
                VkFormatProperties nodeMapProperties{};
                vkGetPhysicalDeviceFormatProperties(candidate, VK_FORMAT_R8G8B8A8_UNORM, &rgba8Properties);
                vkGetPhysicalDeviceFormatProperties(candidate, VK_FORMAT_R32G32B32A32_SFLOAT, &nodeMapProperties);

                const bool supportsRgbaSampling = (rgba8Properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0U;
                const bool supportsRgbaStorage = (rgba8Properties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) != 0U;
                const bool supportsNodeSampling = (nodeMapProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0U;
                if (!supportsRgbaSampling || !supportsRgbaStorage || !supportsNodeSampling) {
                    continue;
                }

                physicalDevice = candidate;
                queueFamilyIndex = index;
                vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceProperties);
                vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
                return;
            }
        }

        throw std::runtime_error("Failed to find a Vulkan device that supports compute, sampled RGBA8 images, and storage RGBA8 images.");
    }

    void CreateDevice() {
        const float queuePriority = 1.0f;
        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = queueFamilyIndex;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &queuePriority;

        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = 1;
        createInfo.pQueueCreateInfos = &queueInfo;

        ThrowIfFailed(vkCreateDevice(physicalDevice, &createInfo, nullptr, &device), "Failed to create Vulkan logical device");
        vkGetDeviceQueue(device, queueFamilyIndex, 0, &queue);
    }

    void CreateCommandPool() {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = queueFamilyIndex;
        ThrowIfFailed(vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool), "Failed to create Vulkan command pool");
    }

    void CreateCommandBuffer() {
        VkCommandBufferAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocateInfo.commandPool = commandPool;
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = 1;
        ThrowIfFailed(vkAllocateCommandBuffers(device, &allocateInfo, &commandBuffer), "Failed to allocate Vulkan command buffer");
    }

    void CreateDescriptorSetLayout() {
        const std::array<VkDescriptorSetLayoutBinding, 4> bindings = {{
            VkDescriptorSetLayoutBinding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            VkDescriptorSetLayoutBinding{1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            VkDescriptorSetLayoutBinding{2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            VkDescriptorSetLayoutBinding{3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        }};

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        ThrowIfFailed(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout), "Failed to create descriptor set layout");
    }

    void CreatePipeline() {
        VkShaderModuleCreateInfo shaderInfo{};
        shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shaderInfo.codeSize = generated::kEjmdkShaderSpvSize;
        shaderInfo.pCode = reinterpret_cast<const std::uint32_t*>(generated::kEjmdkShaderSpv);

        VkShaderModule shaderModule = VK_NULL_HANDLE;
        ThrowIfFailed(vkCreateShaderModule(device, &shaderInfo, nullptr, &shaderModule), "Failed to create compute shader module");

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(PushConstants);

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
        ThrowIfFailed(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout), "Failed to create compute pipeline layout");

        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = shaderModule;
        stageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = pipelineLayout;
        ThrowIfFailed(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline), "Failed to create compute pipeline");

        vkDestroyShaderModule(device, shaderModule, nullptr);
    }

    void CreateDescriptorPool() {
        const std::array<VkDescriptorPoolSize, 2> poolSizes = {{
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
        }};

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = 1;
        ThrowIfFailed(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool), "Failed to create descriptor pool");
    }

    void AllocateDescriptorSet() {
        VkDescriptorSetAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateInfo.descriptorPool = descriptorPool;
        allocateInfo.descriptorSetCount = 1;
        allocateInfo.pSetLayouts = &descriptorSetLayout;
        ThrowIfFailed(vkAllocateDescriptorSets(device, &allocateInfo, &descriptorSet), "Failed to allocate descriptor set");
    }

    void CreateSampler() {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.maxLod = 0.0f;
        ThrowIfFailed(vkCreateSampler(device, &samplerInfo, nullptr, &sampler), "Failed to create Vulkan sampler");
    }

    [[nodiscard]] std::uint32_t FindMemoryType(const std::uint32_t typeBits, const VkMemoryPropertyFlags requiredProperties) const {
        for (std::uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index) {
            const bool typeMatch = (typeBits & (1U << index)) != 0U;
            const bool propertyMatch = (memoryProperties.memoryTypes[index].propertyFlags & requiredProperties) == requiredProperties;
            if (typeMatch && propertyMatch) {
                return index;
            }
        }
        throw std::runtime_error("Failed to find a compatible Vulkan memory type.");
    }

    [[nodiscard]] BufferResource CreateBuffer(const VkDeviceSize size,
                                              const VkBufferUsageFlags usage,
                                              const VkMemoryPropertyFlags properties,
                                              const bool persistentMap) {
        BufferResource resource;
        resource.size = size;

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ThrowIfFailed(vkCreateBuffer(device, &bufferInfo, nullptr, &resource.buffer), "Failed to create Vulkan buffer");

        VkMemoryRequirements memoryRequirements{};
        vkGetBufferMemoryRequirements(device, resource.buffer, &memoryRequirements);

        VkMemoryAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocateInfo.allocationSize = memoryRequirements.size;
        allocateInfo.memoryTypeIndex = FindMemoryType(memoryRequirements.memoryTypeBits, properties);
        ThrowIfFailed(vkAllocateMemory(device, &allocateInfo, nullptr, &resource.memory), "Failed to allocate Vulkan buffer memory");
        ThrowIfFailed(vkBindBufferMemory(device, resource.buffer, resource.memory, 0), "Failed to bind Vulkan buffer memory");

        if (persistentMap) {
            ThrowIfFailed(vkMapMemory(device, resource.memory, 0, size, 0, &resource.mapped), "Failed to map Vulkan buffer memory");
        }

        return resource;
    }

    [[nodiscard]] ImageResource CreateImage(const std::uint32_t width,
                                            const std::uint32_t height,
                                            const VkFormat format,
                                            const VkImageUsageFlags usage) {
        ImageResource resource;
        resource.width = width;
        resource.height = height;
        resource.format = format;
        resource.layout = VK_IMAGE_LAYOUT_UNDEFINED;

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = width;
        imageInfo.extent.height = height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = format;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = usage;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ThrowIfFailed(vkCreateImage(device, &imageInfo, nullptr, &resource.image), "Failed to create Vulkan image");

        VkMemoryRequirements memoryRequirements{};
        vkGetImageMemoryRequirements(device, resource.image, &memoryRequirements);

        VkMemoryAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocateInfo.allocationSize = memoryRequirements.size;
        allocateInfo.memoryTypeIndex = FindMemoryType(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        ThrowIfFailed(vkAllocateMemory(device, &allocateInfo, nullptr, &resource.memory), "Failed to allocate Vulkan image memory");
        ThrowIfFailed(vkBindImageMemory(device, resource.image, resource.memory, 0), "Failed to bind Vulkan image memory");

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = resource.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        ThrowIfFailed(vkCreateImageView(device, &viewInfo, nullptr, &resource.view), "Failed to create Vulkan image view");

        return resource;
    }

    void DestroyBuffer(BufferResource& resource) {
        if (resource.mapped != nullptr) {
            vkUnmapMemory(device, resource.memory);
            resource.mapped = nullptr;
        }
        if (resource.buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, resource.buffer, nullptr);
            resource.buffer = VK_NULL_HANDLE;
        }
        if (resource.memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, resource.memory, nullptr);
            resource.memory = VK_NULL_HANDLE;
        }
        resource.size = 0;
    }

    void DestroyImage(ImageResource& resource) {
        if (resource.view != VK_NULL_HANDLE) {
            vkDestroyImageView(device, resource.view, nullptr);
            resource.view = VK_NULL_HANDLE;
        }
        if (resource.image != VK_NULL_HANDLE) {
            vkDestroyImage(device, resource.image, nullptr);
            resource.image = VK_NULL_HANDLE;
        }
        if (resource.memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, resource.memory, nullptr);
            resource.memory = VK_NULL_HANDLE;
        }
        resource.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    void ResetFrameResources() {
        vkDeviceWaitIdle(device);
        DestroyBuffer(prevUploadBuffer);
        DestroyBuffer(currUploadBuffer);
        DestroyBuffer(nodeUploadBuffer);
        DestroyBuffer(readbackBuffer);
        DestroyImage(prevImage);
        DestroyImage(currImage);
        DestroyImage(nodeImage);
        DestroyImage(outputImage);
    }

    void Configure(const std::uint32_t newSourceWidth,
                   const std::uint32_t newSourceHeight,
                   const std::uint32_t newScaleFactor) {
        if (newSourceWidth == sourceWidth && newSourceHeight == sourceHeight && newScaleFactor == scaleFactor) {
            return;
        }

        ResetFrameResources();
        sourceWidth = newSourceWidth;
        sourceHeight = newSourceHeight;
        scaleFactor = newScaleFactor;
        outputWidth = sourceWidth * scaleFactor;
        outputHeight = sourceHeight * scaleFactor;

        const VkDeviceSize rgbaBytes = static_cast<VkDeviceSize>(sourceWidth) * sourceHeight * 4ULL;
        const VkDeviceSize nodeBytes = static_cast<VkDeviceSize>(sourceWidth) * sourceHeight * sizeof(float) * 4ULL;
        const VkDeviceSize outputBytes = static_cast<VkDeviceSize>(outputWidth) * outputHeight * 4ULL;

        prevUploadBuffer = CreateBuffer(rgbaBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
        currUploadBuffer = CreateBuffer(rgbaBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
        nodeUploadBuffer = CreateBuffer(nodeBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
        readbackBuffer = CreateBuffer(outputBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);

        prevImage = CreateImage(sourceWidth, sourceHeight, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        currImage = CreateImage(sourceWidth, sourceHeight, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        nodeImage = CreateImage(sourceWidth, sourceHeight, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        outputImage = CreateImage(outputWidth, outputHeight, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);

        UpdateDescriptorSet();
    }

    void UpdateDescriptorSet() {
        const VkDescriptorImageInfo prevInfo{sampler, prevImage.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        const VkDescriptorImageInfo currInfo{sampler, currImage.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        const VkDescriptorImageInfo nodeInfo{sampler, nodeImage.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        const VkDescriptorImageInfo outputInfo{VK_NULL_HANDLE, outputImage.view, VK_IMAGE_LAYOUT_GENERAL};

        std::array<VkWriteDescriptorSet, 4> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = descriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].pImageInfo = &prevInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = descriptorSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &currInfo;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = descriptorSet;
        writes[2].dstBinding = 2;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[2].pImageInfo = &nodeInfo;

        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = descriptorSet;
        writes[3].dstBinding = 3;
        writes[3].descriptorCount = 1;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[3].pImageInfo = &outputInfo;

        vkUpdateDescriptorSets(device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    void WriteMappedBuffer(BufferResource& buffer, const void* data, const std::size_t size) {
        if (size > static_cast<std::size_t>(buffer.size)) {
            throw std::runtime_error("Attempted to upload more data than the Vulkan staging buffer can hold.");
        }
        std::memcpy(buffer.mapped, data, size);
    }

    void TransitionImage(VkCommandBuffer cmd, ImageResource& image, const VkImageLayout newLayout) {
        if (image.layout == newLayout) {
            return;
        }

        const LayoutInfo sourceInfo = DescribeLayout(image.layout);
        const LayoutInfo destinationInfo = DescribeLayout(newLayout);

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = image.layout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image.image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = sourceInfo.accessMask;
        barrier.dstAccessMask = destinationInfo.accessMask;

        vkCmdPipelineBarrier(cmd,
                             sourceInfo.stageMask,
                             destinationInfo.stageMask,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &barrier);
        image.layout = newLayout;
    }

    void CopyBufferToImage(VkCommandBuffer cmd, const BufferResource& buffer, const ImageResource& image) const {
        VkBufferImageCopy copyRegion{};
        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.mipLevel = 0;
        copyRegion.imageSubresource.baseArrayLayer = 0;
        copyRegion.imageSubresource.layerCount = 1;
        copyRegion.imageExtent.width = image.width;
        copyRegion.imageExtent.height = image.height;
        copyRegion.imageExtent.depth = 1;
        vkCmdCopyBufferToImage(cmd, buffer.buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
    }

    void CopyImageToBuffer(VkCommandBuffer cmd, const ImageResource& image, const BufferResource& buffer) const {
        VkBufferImageCopy copyRegion{};
        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.mipLevel = 0;
        copyRegion.imageSubresource.baseArrayLayer = 0;
        copyRegion.imageSubresource.layerCount = 1;
        copyRegion.imageExtent.width = image.width;
        copyRegion.imageExtent.height = image.height;
        copyRegion.imageExtent.depth = 1;
        vkCmdCopyImageToBuffer(cmd, image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer.buffer, 1, &copyRegion);
    }

    [[nodiscard]] RgbaFrame Process(const RgbaFrame* previousFrame,
                                    const RgbaFrame& currentFrame,
                                    const NodeMap& nodeMap,
                                    const float interpolationTime) {
        if (currentFrame.width != sourceWidth || currentFrame.height != sourceHeight) {
            throw std::runtime_error("Current frame dimensions do not match the configured Vulkan pipeline size.");
        }
        if (nodeMap.width != sourceWidth || nodeMap.height != sourceHeight) {
            throw std::runtime_error("Node map dimensions do not match the configured Vulkan pipeline size.");
        }

        const RgbaFrame& previousOrCurrent = (previousFrame != nullptr && !previousFrame->empty()) ? *previousFrame : currentFrame;
        if (previousOrCurrent.width != sourceWidth || previousOrCurrent.height != sourceHeight) {
            throw std::runtime_error("Previous frame dimensions do not match the configured Vulkan pipeline size.");
        }

        WriteMappedBuffer(prevUploadBuffer, previousOrCurrent.pixels.data(), previousOrCurrent.pixels.size());
        WriteMappedBuffer(currUploadBuffer, currentFrame.pixels.data(), currentFrame.pixels.size());
        WriteMappedBuffer(nodeUploadBuffer, nodeMap.texels.data(), nodeMap.texels.size() * sizeof(float));

        ThrowIfFailed(vkResetCommandBuffer(commandBuffer, 0), "Failed to reset Vulkan command buffer");

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        ThrowIfFailed(vkBeginCommandBuffer(commandBuffer, &beginInfo), "Failed to begin Vulkan command buffer");

        TransitionImage(commandBuffer, prevImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        TransitionImage(commandBuffer, currImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        TransitionImage(commandBuffer, nodeImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        CopyBufferToImage(commandBuffer, prevUploadBuffer, prevImage);
        CopyBufferToImage(commandBuffer, currUploadBuffer, currImage);
        CopyBufferToImage(commandBuffer, nodeUploadBuffer, nodeImage);
        TransitionImage(commandBuffer, prevImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        TransitionImage(commandBuffer, currImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        TransitionImage(commandBuffer, nodeImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        TransitionImage(commandBuffer, outputImage, VK_IMAGE_LAYOUT_GENERAL);

        const PushConstants pushConstants{
            static_cast<std::int32_t>(sourceWidth),
            static_cast<std::int32_t>(sourceHeight),
            static_cast<std::int32_t>(outputWidth),
            static_cast<std::int32_t>(outputHeight),
            static_cast<float>(scaleFactor),
            interpolationTime,
            previousFrame != nullptr ? 1 : 0,
            1.0f,
        };

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushConstants), &pushConstants);

        const std::uint32_t groupCountX = (outputWidth + 15U) / 16U;
        const std::uint32_t groupCountY = (outputHeight + 15U) / 16U;
        vkCmdDispatch(commandBuffer, groupCountX, groupCountY, 1);

        TransitionImage(commandBuffer, outputImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        CopyImageToBuffer(commandBuffer, outputImage, readbackBuffer);

        ThrowIfFailed(vkEndCommandBuffer(commandBuffer), "Failed to end Vulkan command buffer");

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        ThrowIfFailed(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE), "Failed to submit Vulkan command buffer");
        ThrowIfFailed(vkQueueWaitIdle(queue), "Failed to wait for Vulkan queue completion");

        RgbaFrame outputFrame;
        outputFrame.width = outputWidth;
        outputFrame.height = outputHeight;
        outputFrame.pts100ns = currentFrame.pts100ns;
        outputFrame.pixels.resize(static_cast<std::size_t>(outputWidth) * outputHeight * 4U);
        std::memcpy(outputFrame.pixels.data(), readbackBuffer.mapped, outputFrame.pixels.size());
        return outputFrame;
    }
};

VulkanEjmdkProcessor::VulkanEjmdkProcessor()
    : impl_(std::make_unique<Impl>()) {
}

VulkanEjmdkProcessor::~VulkanEjmdkProcessor() = default;

void VulkanEjmdkProcessor::Configure(const std::uint32_t sourceWidth,
                                     const std::uint32_t sourceHeight,
                                     const std::uint32_t scaleFactor) {
    impl_->Configure(sourceWidth, sourceHeight, scaleFactor);
}

std::string VulkanEjmdkProcessor::DeviceName() const {
    return impl_->physicalDeviceProperties.deviceName;
}

RgbaFrame VulkanEjmdkProcessor::Process(const RgbaFrame* previousFrame,
                                        const RgbaFrame& currentFrame,
                                        const NodeMap& nodeMap,
                                        const float interpolationTime) {
    return impl_->Process(previousFrame, currentFrame, nodeMap, interpolationTime);
}

}  // namespace ejmdk

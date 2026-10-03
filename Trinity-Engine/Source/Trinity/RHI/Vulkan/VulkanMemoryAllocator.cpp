// VulkanMemoryAllocator's implementation. Every Vulkan function it calls comes from volk, through vmaImportVulkanFunctionsFromVolk
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#define VMA_IMPLEMENTATION
#include "Trinity/RHI/Vulkan/VulkanHeaders.hpp"
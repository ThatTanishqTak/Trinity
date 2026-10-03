#pragma once

// volk loads Vulkan at runtime and declares every vk* function, so nothing links the Vulkan loader. Only the Vulkan backend includes this
#include <volk.h>

// After volk, so VulkanMemoryAllocator can take its function pointers from volk
#include <vk_mem_alloc.h>
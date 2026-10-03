#pragma once

// volk loads Vulkan at runtime and declares every vk* function, so nothing links the Vulkan loader. Only the Vulkan backend includes this
#include <volk.h>
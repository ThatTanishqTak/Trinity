#pragma once

#include "Trinity/Core/Assert.hpp"

#include <volk.h>

namespace Trinity
{
	const char* VkResultToString(VkResult result);

	VkDebugUtilsMessengerCreateInfoEXT GetVulkanDebugMessengerCreateInfo();

	namespace Vulkan
	{
		bool CheckVKResult(VkResult result, const char* expression, const char* file, int line);
	}
}

#if defined(TR_ENABLE_ASSERTS)
#define TR_VK_CHECK(expression) (::Trinity::Vulkan::CheckVKResult((expression), #expression, __FILE__, __LINE__) || (TR_DEBUGBREAK(), false))
#else
#define TR_VK_CHECK(expression) ::Trinity::Vulkan::CheckVKResult((expression), #expression, __FILE__, __LINE__)
#endif
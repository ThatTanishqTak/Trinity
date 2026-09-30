#include "Trinity/Renderer/Vulkan/VulkanCheck.hpp"

#include "Trinity/Core/Log.hpp"

#include <string_view>

namespace Trinity
{
	namespace
	{
		std::string_view GetMessageTypeName(VkDebugUtilsMessageTypeFlagsEXT types)
		{
			if (types & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)
			{
				return "Validation";
			}

			if (types & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)
			{
				return "Performance";
			}

			return "General";
		}

		VKAPI_ATTR VkBool32 VKAPI_CALL OnVulkanDebugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT types, const VkDebugUtilsMessengerCallbackDataEXT* callbackData, void* /*userData*/)
		{
			const std::string_view l_Type = GetMessageTypeName(types);
			const char* l_Message = callbackData && callbackData->pMessage ? callbackData->pMessage : "(no message)";

			if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
			{
				TR_CORE_ERROR("[Vulkan {}] {}", l_Type, l_Message);

#if defined(TR_ENABLE_ASSERTS)
				if (types & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)
				{
					TR_DEBUGBREAK();
				}
#endif
			}
			else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
			{
				TR_CORE_WARN("[Vulkan {}] {}", l_Type, l_Message);
			}
			else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
			{
				TR_CORE_INFO("[Vulkan {}] {}", l_Type, l_Message);
			}
			else
			{
				TR_CORE_TRACE("[Vulkan {}] {}", l_Type, l_Message);
			}

			return VK_FALSE;
		}
	}

	const char* VkResultToString(VkResult result)
	{
		switch (result)
		{
			case VK_SUCCESS:
			{
				return "VK_SUCCESS";
			}
			case VK_NOT_READY:
			{
				return "VK_NOT_READY";
			}
			case VK_TIMEOUT:
			{
				return "VK_TIMEOUT";
			}
			case VK_EVENT_SET:
			{
				return "VK_EVENT_SET";
			}
			case VK_EVENT_RESET:
			{
				return "VK_EVENT_RESET";
			}
			case VK_INCOMPLETE:
			{
				return "VK_INCOMPLETE";
			}
			case VK_SUBOPTIMAL_KHR:
			{
				return "VK_SUBOPTIMAL_KHR";
			}
			case VK_ERROR_OUT_OF_HOST_MEMORY:
			{
				return "VK_ERROR_OUT_OF_HOST_MEMORY";
			}
			case VK_ERROR_OUT_OF_DEVICE_MEMORY:
			{
				return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
			}
			case VK_ERROR_INITIALIZATION_FAILED:
			{
				return "VK_ERROR_INITIALIZATION_FAILED";
			}
			case VK_ERROR_DEVICE_LOST:
			{
				return "VK_ERROR_DEVICE_LOST";
			}
			case VK_ERROR_MEMORY_MAP_FAILED:
			{
				return "VK_ERROR_MEMORY_MAP_FAILED";
			}
			case VK_ERROR_LAYER_NOT_PRESENT:
			{
				return "VK_ERROR_LAYER_NOT_PRESENT";
			}
			case VK_ERROR_EXTENSION_NOT_PRESENT:
			{
				return "VK_ERROR_EXTENSION_NOT_PRESENT";
			}
			case VK_ERROR_FEATURE_NOT_PRESENT:
			{
				return "VK_ERROR_FEATURE_NOT_PRESENT";
			}
			case VK_ERROR_INCOMPATIBLE_DRIVER:
			{
				return "VK_ERROR_INCOMPATIBLE_DRIVER";
			}
			case VK_ERROR_TOO_MANY_OBJECTS:
			{
				return "VK_ERROR_TOO_MANY_OBJECTS";
			}
			case VK_ERROR_FORMAT_NOT_SUPPORTED:
			{
				return "VK_ERROR_FORMAT_NOT_SUPPORTED";
			}
			case VK_ERROR_FRAGMENTED_POOL:
			{
				return "VK_ERROR_FRAGMENTED_POOL";
			}
			case VK_ERROR_UNKNOWN:
			{
				return "VK_ERROR_UNKNOWN";
			}
			case VK_ERROR_OUT_OF_POOL_MEMORY:
			{
				return "VK_ERROR_OUT_OF_POOL_MEMORY";
			}
			case VK_ERROR_INVALID_EXTERNAL_HANDLE:
			{
				return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
			}
			case VK_ERROR_FRAGMENTATION:
			{
				return "VK_ERROR_FRAGMENTATION";
			}
			case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS:
			{
				return "VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS";
			}
			case VK_ERROR_SURFACE_LOST_KHR:
			{
				return "VK_ERROR_SURFACE_LOST_KHR";
			}
			case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
			{
				return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
			}
			case VK_ERROR_OUT_OF_DATE_KHR:
			{
				return "VK_ERROR_OUT_OF_DATE_KHR";
			}
			case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR:
			{
				return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
			}
			case VK_ERROR_VALIDATION_FAILED_EXT:
			{
				return "VK_ERROR_VALIDATION_FAILED_EXT";
			}
			case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT:
			{
				return "VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT";
			}
			default:
			{
				return "VkResult(unknown)";
			}
		}
	}

	VkDebugUtilsMessengerCreateInfoEXT GetVulkanDebugMessengerCreateInfo()
	{
		VkDebugUtilsMessengerCreateInfoEXT l_CreateInfo{};
		l_CreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		l_CreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		l_CreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		l_CreateInfo.pfnUserCallback = &OnVulkanDebugMessage;

		return l_CreateInfo;
	}

	namespace Vulkan
	{
		bool CheckVKResult(VkResult result, const char* expression, const char* file, int line)
		{
			if (result >= VK_SUCCESS)
			{
				return true;
			}

			TR_CORE_ERROR("{} failed with {} ({}:{})", expression, VkResultToString(result), file, line);

			return false;
		}
	}
}
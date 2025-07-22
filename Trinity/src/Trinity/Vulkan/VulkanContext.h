#pragma once

#include "vulkan/vulkan.h"
#include <vector>

namespace Trinity
{
	class VulkanContext
	{
	public:
		VulkanContext() = default;
		~VulkanContext() = default;

		bool Initialize();
		void Shutdown();

		VkInstance GetVulkanInstance() const { return m_Instance; }

	private:
		void CreateInstance();
		void SetupDebugMessenger();
		bool CheckValidationLayerSupport();

		static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType,
			const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData);

	private:
#ifdef NDEBUG
		static constexpr bool s_EnableValidationLayers = false;
#else
		static constexpr bool s_EnableValidationLayers = true;
#endif

		std::vector<const char*> m_ValidationLayers{ "VK_LAYER_KHRONOS_validation" };
		VkInstance m_Instance{};
		VkDebugUtilsMessengerEXT m_DebugMessenger{};
	};
}
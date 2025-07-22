#pragma once

#include "vulkan/vulkan.h"
#include <vector>

#if _DEBUG
static constexpr bool s_EnableValidationLayers = true;
#else
static constexpr bool s_EnableValidationLayers = false;
#endif

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
		void PickPhysicalDevice();

		//----------------------------------------------------------------------------------------------------------------------------------------------------//
		// Helper functions
		bool CheckValidationLayerSupport();
		bool IsDeviceSuitable(VkPhysicalDevice physicalDevice);

		static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType,
			const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData);

	private:
		VkInstance m_Instance = VK_NULL_HANDLE;
		VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;
		VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
		
		std::vector<const char*> m_ValidationLayers{ "VK_LAYER_KHRONOS_validation" };
	};
}
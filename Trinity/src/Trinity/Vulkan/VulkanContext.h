#pragma once

#include "vulkan/vulkan.h"
#include <GLFW/glfw3.h>

#include <vector>
#include <cstdint>
#include <optional>

#include "Trinity/Window/Window.h"

#if _DEBUG
static constexpr bool s_EnableValidationLayers = true;
#else
static constexpr bool s_EnableValidationLayers = false;
#endif

namespace Trinity
{
	struct QueueFamilyIndices
	{
		std::optional<uint32_t> GraphicsFamily;
		std::optional<uint32_t> PresentFamily;

		bool IsComplete() const { return GraphicsFamily.has_value() && PresentFamily.has_value(); }
	};

	class VulkanContext
	{
	public:
		VulkanContext() = default;
		VulkanContext(GLFWwindow* window);
		~VulkanContext() = default;

		bool Initialize();
		void Shutdown();

	private:
		void CreateInstance();
		void SetupDebugMessenger();
		void CreateSurface();
		void PickPhysicalDevice();
		void CreateLogicalDevice();

		//----------------------------------------------------------------------------------------------------------------------------------------------------//
		// Helper functions
		bool CheckValidationLayerSupport();
		int IsDeviceSuitable(VkPhysicalDevice physicalDevice);
		QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device);

		static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType,
			const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData);

	private:
		VkInstance m_Instance = VK_NULL_HANDLE;
		VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;
		VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
		QueueFamilyIndices m_QueueFamilyIndices{};
		VkDevice m_Device = VK_NULL_HANDLE;
		VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
		VkQueue m_PresentQueue = VK_NULL_HANDLE;
		VkSurfaceKHR m_Surface = VK_NULL_HANDLE;

		std::vector<const char*> m_ValidationLayers{ "VK_LAYER_KHRONOS_validation" };

		GLFWwindow* m_Window;
	};
}
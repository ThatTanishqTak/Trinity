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

		VkDevice GetDevice() const { return m_Device; }
		VkExtent2D GetSwapChainExtent() const { return m_SwapChainExtent; }
		VkFormat GetSwapChainImageFormat() const { return m_SwapChainImageFormat; }
		std::vector<VkImageView> GetSwapChainImages() const { return m_SwapChainImageViews; }
		VkPhysicalDevice GetPhysicalDivice() const { return m_PhysicalDevice; }
		VkSwapchainKHR GetSwapChain() const { return m_SwapChain; }
		VkQueue GetGraphicsQueue() const { return m_GraphicsQueue; }
		VkQueue GetPresentQueue() const { return m_PresentQueue; }
		
		//----------------------------------------------------------------------------------------------------------------------------------------------------//
		// Public Helper functions
		QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice physicalDevice);

	private:
		void CreateInstance();
		void SetupDebugMessenger();
		void CreateSurface();
		void PickPhysicalDevice();
		void CreateLogicalDevice();
		void CreateSwapChain();
		void CreateImageViews();

		//----------------------------------------------------------------------------------------------------------------------------------------------------//
		// Helper functions
		bool CheckValidationLayerSupport();
		int IsDeviceSuitable(VkPhysicalDevice physicalDevice);
		bool CheckDeviceExtensionSupport(VkPhysicalDevice physicalDevice);
		struct SwapChainSupportDetails
		{
			VkSurfaceCapabilitiesKHR Capabilities;
			std::vector<VkSurfaceFormatKHR> Formats;
			std::vector<VkPresentModeKHR> PresentModes;
		};
		SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice physicalDevice);
		VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
		VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
		VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);

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

		VkSwapchainKHR m_SwapChain = VK_NULL_HANDLE;
		std::vector<VkImage> m_SwapChainImages{};
		VkFormat m_SwapChainImageFormat = VK_FORMAT_UNDEFINED;
		VkExtent2D m_SwapChainExtent{};

		std::vector<VkImageView> m_SwapChainImageViews{};

		std::vector<const char*> m_ValidationLayers{ "VK_LAYER_KHRONOS_validation" };
		std::vector<const char*> m_DeviceExtensions{ VK_KHR_SWAPCHAIN_EXTENSION_NAME };

		GLFWwindow* m_Window;
	};
}
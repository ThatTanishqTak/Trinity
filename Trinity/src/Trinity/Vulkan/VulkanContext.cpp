#include "Trinity/trpch.h"

#include "Trinity/Vulkan/VulkanContext.h"
#include "Trinity/Utilities/Utilities.h"

namespace Trinity
{
	VulkanContext::VulkanContext(GLFWwindow* window) : m_Window(window)
	{

	}

	bool VulkanContext::Initialize()
	{
		TR_CORE_INFO("-------INITIALIZING VULKAN-------");

		CreateInstance();
		SetupDebugMessenger();
		CreateSurface();
		PickPhysicalDevice();
		CreateLogicalDevice();
		CreateSwapChain();
		CreateImageViews();

		TR_CORE_INFO("-------VULKAN INITIALIZED-------");

		return true;
	}

	void VulkanContext::Shutdown()
	{
		TR_CORE_INFO("-------SHUTTING DOWN VULKAN-------");

		for (auto it_ImageView : m_SwapChainImageViews)
		{
			vkDestroyImageView(m_Device, it_ImageView, nullptr);
		}
		TR_CORE_TRACE("Image views destroyed");

		vkDestroySwapchainKHR(m_Device, m_SwapChain, nullptr);
		TR_CORE_TRACE("Swap chain destroyed");

		vkDestroyDevice(m_Device, nullptr);
		TR_CORE_TRACE("Logical device destroyed");

		if (s_EnableValidationLayers)
		{
			auto a_Function = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_Instance, "vkDestroyDebugUtilsMessengerEXT");
			if (a_Function)
			{
				a_Function(m_Instance, m_DebugMessenger, nullptr);
				TR_CORE_TRACE("Debug messenger destroyed");
			}
		}

		vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
		TR_CORE_TRACE("window surface destroyed");

		vkDestroyInstance(m_Instance, nullptr);
		TR_CORE_TRACE("Vulkan instance destroyed");

		TR_CORE_INFO("-------VULKAN SHUTDOWN COMPLETE-------");
	}

	//----------------------------------------------------------------------------------------------------------------------------------------------------//

	void VulkanContext::CreateInstance()
	{
		TR_CORE_TRACE("Creating Vulkan Instance");

		VkApplicationInfo l_AppInfo{};
		l_AppInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		l_AppInfo.pApplicationName = "Forge";
		l_AppInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
		l_AppInfo.pEngineName = "Trinity";
		l_AppInfo.engineVersion = VK_MAKE_VERSION(1, 3, 0);
		l_AppInfo.apiVersion = VK_API_VERSION_1_3;

		VkInstanceCreateInfo l_CreateInfo{};
		l_CreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		l_CreateInfo.pApplicationInfo = &l_AppInfo;

		uint32_t l_GLFWExtensionCount = 0;
		const char** l_GLFWExtensions;

		l_GLFWExtensions = glfwGetRequiredInstanceExtensions(&l_GLFWExtensionCount);

		std::vector<const char*> l_RequiredExtensions{};
		for (uint32_t i = 0; i < l_GLFWExtensionCount; i++)
		{
			l_RequiredExtensions.emplace_back(l_GLFWExtensions[i]);
		}

		l_RequiredExtensions.emplace_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
		if (s_EnableValidationLayers)
		{
			l_RequiredExtensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		}

		l_CreateInfo.enabledExtensionCount = (uint32_t)l_RequiredExtensions.size();
		l_CreateInfo.ppEnabledExtensionNames = l_RequiredExtensions.data();

		if (s_EnableValidationLayers)
		{
			if (!CheckValidationLayerSupport())
			{
				TR_CORE_CRITICAL("Validation layers requested but not available");
				
				return;
			}

			l_CreateInfo.enabledLayerCount = (uint32_t)m_ValidationLayers.size();
			l_CreateInfo.ppEnabledLayerNames = m_ValidationLayers.data();
		}

		else
		{
			l_CreateInfo.enabledLayerCount = 0;
		}

		VkDebugUtilsMessengerCreateInfoEXT l_DebugCreateInfo{};
		if (s_EnableValidationLayers)
		{
			l_DebugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
			l_DebugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
			l_DebugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
			l_DebugCreateInfo.pfnUserCallback = DebugCallback;
			l_DebugCreateInfo.pUserData = nullptr;

			l_CreateInfo.pNext = &l_DebugCreateInfo;
		}

		else
		{
			l_CreateInfo.pNext = nullptr;
		}

		l_CreateInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;

		if (vkCreateInstance(&l_CreateInfo, nullptr, &m_Instance) != VK_SUCCESS)
		{
			TR_CORE_CRITICAL("Failed to create vulkan instance");

			return;
		}

		uint32_t l_ExtensionCount = 0;
		vkEnumerateInstanceExtensionProperties(nullptr, &l_ExtensionCount, nullptr);

		std::vector<VkExtensionProperties> l_Extensions(l_ExtensionCount);
		vkEnumerateInstanceExtensionProperties(nullptr, &l_ExtensionCount, l_Extensions.data());

		TR_CORE_TRACE("Available extensions: {}", l_ExtensionCount);

		for (const auto& it_extension : l_Extensions)
		{
			TR_CORE_TRACE(" - {}", it_extension.extensionName);
		}

		TR_CORE_TRACE("Vulkan Instance Created");
	}

	void VulkanContext::SetupDebugMessenger()
	{
		if (!s_EnableValidationLayers)
		{
			return;
		}

		TR_CORE_TRACE("Creating debug messenger");

		VkDebugUtilsMessengerCreateInfoEXT l_CreateInfo{};
		l_CreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		l_CreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		l_CreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		l_CreateInfo.pfnUserCallback = DebugCallback;
		l_CreateInfo.pUserData = nullptr;

		auto a_Function = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_Instance, "vkCreateDebugUtilsMessengerEXT");
		if (a_Function != nullptr)
		{
			a_Function(m_Instance, &l_CreateInfo, nullptr, &m_DebugMessenger);
		}

		TR_CORE_TRACE("Debug messenger created");
	}

	void VulkanContext::CreateSurface()
	{
		TR_CORE_TRACE("Creating window surface");

		if (glfwCreateWindowSurface(m_Instance, m_Window, nullptr, &m_Surface) != VK_SUCCESS)
		{
			TR_CORE_ERROR("Failed to create window surface");
		}

		TR_CORE_TRACE("Window surface created");
	}

	void VulkanContext::PickPhysicalDevice()
	{
		TR_CORE_TRACE("Selecting suitable graphics card");

		uint32_t l_DeviceCount = 0;
		vkEnumeratePhysicalDevices(m_Instance, &l_DeviceCount, nullptr);

		if (l_DeviceCount == 0)
		{
			TR_CORE_CRITICAL("No graphics card supports vulkan");
		}

		std::vector<VkPhysicalDevice> l_PhysicalDevices(l_DeviceCount);
		vkEnumeratePhysicalDevices(m_Instance, &l_DeviceCount, l_PhysicalDevices.data());

		TR_CORE_TRACE("Available graphics cards: {}", l_DeviceCount);
		for (const auto& it_physicalDevices : l_PhysicalDevices)
		{
			VkPhysicalDeviceProperties l_Properties;
			vkGetPhysicalDeviceProperties(it_physicalDevices, &l_Properties);

			TR_CORE_TRACE(" - {}", l_Properties.deviceName);
		}

		int l_BestScore = 0;
		for (const auto& it_physicalDevices : l_PhysicalDevices)
		{
			int l_Score = IsDeviceSuitable(it_physicalDevices);
			if (l_Score > l_BestScore)
			{
				l_BestScore = l_Score;
				m_PhysicalDevice = it_physicalDevices;
			}
		}

		if (m_PhysicalDevice != VK_NULL_HANDLE)
		{
			VkPhysicalDeviceProperties l_Properties;
			vkGetPhysicalDeviceProperties(m_PhysicalDevice, &l_Properties);

			m_QueueFamilyIndices = FindQueueFamilies(m_PhysicalDevice);
			
			TR_CORE_TRACE("Picked graphics card: {}", l_Properties.deviceName);
		}

		else
		{
			TR_CORE_CRITICAL("No suitable graphics card found");
		}
	}

	void VulkanContext::CreateLogicalDevice()
	{
		TR_CORE_TRACE("Creating logical device");

		QueueFamilyIndices l_Indices = m_QueueFamilyIndices;

		float l_QueuePriority = 1.0f;
		std::vector<VkDeviceQueueCreateInfo> l_QueueCreateInfos{};
		std::vector<uint32_t> l_UniqueQueueFamilies{};
		l_UniqueQueueFamilies.push_back(l_Indices.GraphicsFamily.value());
		if (l_Indices.PresentFamily != l_Indices.GraphicsFamily)
		{
			l_UniqueQueueFamilies.push_back(l_Indices.PresentFamily.value());
		}

		for (uint32_t it_QueueFamily : l_UniqueQueueFamilies)
		{
			VkDeviceQueueCreateInfo l_QueueCreateInfo{};
			l_QueueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			l_QueueCreateInfo.queueFamilyIndex = it_QueueFamily;
			l_QueueCreateInfo.queueCount = 1;
			l_QueueCreateInfo.pQueuePriorities = &l_QueuePriority;
			l_QueueCreateInfos.push_back(l_QueueCreateInfo);
		}

		VkPhysicalDeviceFeatures l_DeviceFeatures{};

		VkDeviceCreateInfo l_CreateInfo{};
		l_CreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

		l_CreateInfo.queueCreateInfoCount = static_cast<uint32_t>(l_QueueCreateInfos.size());
		l_CreateInfo.pQueueCreateInfos = l_QueueCreateInfos.data();

		l_CreateInfo.pEnabledFeatures = &l_DeviceFeatures;

		l_CreateInfo.enabledExtensionCount = static_cast<uint32_t>(m_DeviceExtensions.size());
		l_CreateInfo.ppEnabledExtensionNames = m_DeviceExtensions.data();

		if (s_EnableValidationLayers)
		{
			l_CreateInfo.enabledLayerCount = static_cast<uint32_t>(m_ValidationLayers.size());
			l_CreateInfo.ppEnabledLayerNames = m_ValidationLayers.data();
		}

		else
		{
			l_CreateInfo.enabledLayerCount = 0;
		}

		if (vkCreateDevice(m_PhysicalDevice, &l_CreateInfo, nullptr, &m_Device) != VK_SUCCESS)
		{
			TR_CORE_CRITICAL("Failed to create logical device");
		}

		vkGetDeviceQueue(m_Device, l_Indices.GraphicsFamily.value(), 0, &m_GraphicsQueue);
		vkGetDeviceQueue(m_Device, l_Indices.PresentFamily.value(), 0, &m_PresentQueue);

		TR_CORE_TRACE("Logical device created");
	}

	void VulkanContext::CreateSwapChain()
	{
		TR_CORE_TRACE("Creating swap chain");

		SwapChainSupportDetails l_SwapChainSupport = QuerySwapChainSupport(m_PhysicalDevice);

		VkSurfaceFormatKHR l_SurfaceFormat = ChooseSwapSurfaceFormat(l_SwapChainSupport.Formats);
		VkPresentModeKHR l_PresentMode = ChooseSwapPresentMode(l_SwapChainSupport.PresentModes);
		VkExtent2D l_Extent = ChooseSwapExtent(l_SwapChainSupport.Capabilities);

		uint32_t l_ImageCount = l_SwapChainSupport.Capabilities.minImageCount + 1;
		if (l_SwapChainSupport.Capabilities.maxImageCount > 0 && l_ImageCount > l_SwapChainSupport.Capabilities.maxImageCount)
		{
			l_ImageCount = l_SwapChainSupport.Capabilities.maxImageCount;
		}

		VkSwapchainCreateInfoKHR l_CreateInfo{};
		l_CreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		l_CreateInfo.surface = m_Surface;

		l_CreateInfo.minImageCount = l_ImageCount;
		l_CreateInfo.imageFormat = l_SurfaceFormat.format;
		l_CreateInfo.imageColorSpace = l_SurfaceFormat.colorSpace;
		l_CreateInfo.imageExtent = l_Extent;
		l_CreateInfo.imageArrayLayers = 1;
		l_CreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

		QueueFamilyIndices l_Indices = m_QueueFamilyIndices;
		uint32_t l_QueueFamilyIndices[] = { l_Indices.GraphicsFamily.value(), l_Indices.PresentFamily.value() };

		if (l_Indices.GraphicsFamily != l_Indices.PresentFamily)
		{
			l_CreateInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
			l_CreateInfo.queueFamilyIndexCount = 2;
			l_CreateInfo.pQueueFamilyIndices = l_QueueFamilyIndices;
		}

		else
		{
			l_CreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
			l_CreateInfo.queueFamilyIndexCount = 0;
			l_CreateInfo.pQueueFamilyIndices = nullptr;
		}

		l_CreateInfo.preTransform = l_SwapChainSupport.Capabilities.currentTransform;
		l_CreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		l_CreateInfo.presentMode = l_PresentMode;
		l_CreateInfo.clipped = VK_TRUE;
		l_CreateInfo.oldSwapchain = VK_NULL_HANDLE;

		if (vkCreateSwapchainKHR(m_Device, &l_CreateInfo, nullptr, &m_SwapChain) != VK_SUCCESS)
		{
			TR_CORE_CRITICAL("Failed to create swap chain");
			return;
		}

		vkGetSwapchainImagesKHR(m_Device, m_SwapChain, &l_ImageCount, nullptr);
		m_SwapChainImages.resize(l_ImageCount);
		vkGetSwapchainImagesKHR(m_Device, m_SwapChain, &l_ImageCount, m_SwapChainImages.data());

		m_SwapChainImageFormat = l_SurfaceFormat.format;
		m_SwapChainExtent = l_Extent;

		TR_CORE_TRACE("Swap chain created");
	}

	void VulkanContext::CreateImageViews()
	{
		TR_CORE_TRACE("Creating image views");

		m_SwapChainImageViews.resize(m_SwapChainImages.size());

		for (size_t i = 0; i < m_SwapChainImages.size(); i++)
		{
			VkImageViewCreateInfo l_CreateInfo{};
			l_CreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			l_CreateInfo.image = m_SwapChainImages[i];
			l_CreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			l_CreateInfo.format = m_SwapChainImageFormat;
			l_CreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
			l_CreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
			l_CreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
			l_CreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
			l_CreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			l_CreateInfo.subresourceRange.baseMipLevel = 0;
			l_CreateInfo.subresourceRange.levelCount = 1;
			l_CreateInfo.subresourceRange.baseArrayLayer = 0;
			l_CreateInfo.subresourceRange.layerCount = 1;

			if (vkCreateImageView(m_Device, &l_CreateInfo, nullptr, &m_SwapChainImageViews[i]) != VK_SUCCESS)
			{
				TR_CORE_ERROR("Failed to create image views");
			}
		}

		TR_CORE_TRACE("Image views created: {}", m_SwapChainImages.size());
	}

	void VulkanContext::CleanupSwapChain()
	{
		for (auto imageView : m_SwapChainImageViews)
		{
			vkDestroyImageView(m_Device, imageView, nullptr);
		}
		m_SwapChainImageViews.clear();

		if (m_SwapChain)
		{
			vkDestroySwapchainKHR(m_Device, m_SwapChain, nullptr);
			m_SwapChain = VK_NULL_HANDLE;
		}
	}

	void VulkanContext::RecreateSwapChain()
	{
		TR_CORE_TRACE("VULKAN_CONTEXT: Recreating swapchain");

		int l_Width = 0;
		int l_Height = 0;

		glfwGetFramebufferSize(m_Window, &l_Width, &l_Height);
		while (l_Width == 0 || l_Height == 0)
		{
			glfwGetFramebufferSize(m_Window, &l_Width, &l_Height);
			glfwWaitEvents();
		}

		TR_CORE_TRACE("New window dimensions: {}X{}", l_Width, l_Height);

		vkDeviceWaitIdle(m_Device);

		CleanupSwapChain();

		CreateSwapChain();
		CreateImageViews();

		TR_CORE_TRACE("VULKAN_CONTEXT: Swapchain recreated");
	}

	//----------------------------------------------------------------------------------------------------------------------------------------------------//

	bool VulkanContext::CheckValidationLayerSupport()
	{
		uint32_t l_LayerCount = 0;
		vkEnumerateInstanceLayerProperties(&l_LayerCount, nullptr);

		std::vector<VkLayerProperties> l_AvailableLayers(l_LayerCount);
		vkEnumerateInstanceLayerProperties(&l_LayerCount, l_AvailableLayers.data());

		for (const char* it_LayerName : m_ValidationLayers)
		{
			bool l_LayerFound = false;

			for (const auto& layerProperties : l_AvailableLayers)
			{
				if (strcmp(it_LayerName, layerProperties.layerName) == 0)
				{
					l_LayerFound = true;

					break;
				}
			}

			if (!l_LayerFound)
			{
				return false;
			}
		}

		return true;
	}

	int VulkanContext::IsDeviceSuitable(VkPhysicalDevice physicalDevice)
	{
		VkPhysicalDeviceProperties l_DeviceProperties;
		VkPhysicalDeviceFeatures l_DeviceFeatures;

		vkGetPhysicalDeviceProperties(physicalDevice, &l_DeviceProperties);
		vkGetPhysicalDeviceFeatures(physicalDevice, &l_DeviceFeatures);

		QueueFamilyIndices l_Indices = FindQueueFamilies(physicalDevice);

		bool l_ExtensionsSupported = CheckDeviceExtensionSupport(physicalDevice);

		bool l_SwapChainAdequate = false;
		if (l_ExtensionsSupported)
		{
			SwapChainSupportDetails l_SwapChainSupport = QuerySwapChainSupport(physicalDevice);
			l_SwapChainAdequate = !l_SwapChainSupport.Formats.empty() && !l_SwapChainSupport.PresentModes.empty();
		}

		if (!l_Indices.IsComplete() || !l_DeviceFeatures.geometryShader || !l_ExtensionsSupported || !l_SwapChainAdequate)
		{
			return 0;
		}

		int l_Score = 0;

		if (l_DeviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
		{
			l_Score += 1000;
		}

		l_Score += l_DeviceProperties.limits.maxImageDimension2D;

		return l_Score;
	}

	bool VulkanContext::CheckDeviceExtensionSupport(VkPhysicalDevice device)
	{
		uint32_t l_ExtensionCount;
		vkEnumerateDeviceExtensionProperties(device, nullptr, &l_ExtensionCount, nullptr);

		std::vector<VkExtensionProperties> l_AvailableExtensions(l_ExtensionCount);
		vkEnumerateDeviceExtensionProperties(device, nullptr, &l_ExtensionCount, l_AvailableExtensions.data());

		std::set<std::string> l_RequiredExtensions(m_DeviceExtensions.begin(), m_DeviceExtensions.end());

		for (const auto& it_Extension : l_AvailableExtensions)
		{
			l_RequiredExtensions.erase(it_Extension.extensionName);
		}

		return l_RequiredExtensions.empty();
	}

	VulkanContext::SwapChainSupportDetails VulkanContext::QuerySwapChainSupport(VkPhysicalDevice device)
	{
		SwapChainSupportDetails l_Details;

		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_Surface, &l_Details.Capabilities);

		uint32_t l_FormatCount;
		vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_Surface, &l_FormatCount, nullptr);
		if (l_FormatCount != 0)
		{
			l_Details.Formats.resize(l_FormatCount);
			vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_Surface, &l_FormatCount, l_Details.Formats.data());
		}

		uint32_t l_PresentModeCount;
		vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_Surface, &l_PresentModeCount, nullptr);
		if (l_PresentModeCount != 0)
		{
			l_Details.PresentModes.resize(l_PresentModeCount);
			vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_Surface, &l_PresentModeCount, l_Details.PresentModes.data());
		}

		return l_Details;
	}

	VkSurfaceFormatKHR VulkanContext::ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats)
	{
		for (const auto& it_AvailableFormat : availableFormats)
		{
			if (it_AvailableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && it_AvailableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			{
				return it_AvailableFormat;
			}
		}

		return availableFormats[0];
	}

	VkPresentModeKHR VulkanContext::ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes)
	{
		for (const auto& it_AvailablePresentMode : availablePresentModes)
		{
			if (it_AvailablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
			{
				return it_AvailablePresentMode;
			}
		}

		return VK_PRESENT_MODE_FIFO_KHR;
	}

	VkExtent2D VulkanContext::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities)
	{
		if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
		{
			return capabilities.currentExtent;
		}

		else
		{
			int l_Width{};
			int l_Height{};

			glfwGetFramebufferSize(m_Window, &l_Width, &l_Height);

			VkExtent2D l_ActualExtent{};
			l_ActualExtent.width = static_cast<uint32_t>(l_Width);
			l_ActualExtent.height = static_cast<uint32_t>(l_Height);

			l_ActualExtent.width = std::clamp(l_ActualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
			l_ActualExtent.height = std::clamp(l_ActualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

			return l_ActualExtent;
		}
	}

	QueueFamilyIndices VulkanContext::FindQueueFamilies(VkPhysicalDevice physicalDevice)
	{
		QueueFamilyIndices l_Indices;

		uint32_t l_QueueFamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &l_QueueFamilyCount, nullptr);

		std::vector<VkQueueFamilyProperties> l_QueueFamilies(l_QueueFamilyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &l_QueueFamilyCount, l_QueueFamilies.data());

		int i = 0;
		for (const auto& it_QueueFamily : l_QueueFamilies)
		{
			if (it_QueueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
			{
				l_Indices.GraphicsFamily = i;
			}

			VkBool32 l_PresentSupport = VK_FALSE;
			vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, m_Surface, &l_PresentSupport);
			if (l_PresentSupport)
			{
				l_Indices.PresentFamily = i;
			}

			if (l_Indices.IsComplete())
			{
				break;
			}

			i++;
		}

		return l_Indices;
	}

	VKAPI_ATTR VkBool32 VKAPI_CALL VulkanContext::DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType,
		const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData)
	{
		if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
		{
			TR_CORE_ERROR("Validation Layer: {}\n", pCallbackData->pMessage);
		}

		return VK_FALSE;
	}
}
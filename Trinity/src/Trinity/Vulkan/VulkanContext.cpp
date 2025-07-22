#include "Trinity/trpch.h"

#include "Trinity/Vulkan/VulkanContext.h"
#include "Trinity/Utilities/Utilities.h"

#include <cstring>

#include <GLFW/glfw3.h>

namespace Trinity
{
	bool VulkanContext::Initialize()
	{
		TR_CORE_INFO("-------INITIALIZING VULKAN-------");

		CreateInstance();
		SetupDebugMessenger();

		TR_CORE_INFO("-------VULKAN INITIALIZED-------");

		return true;
	}

	void VulkanContext::Shutdown()
	{
		TR_CORE_INFO("-------SHUTTING DOWN VULKAN-------");

		if (s_EnableValidationLayers)
		{
			auto function = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_Instance, "vkDestroyDebugUtilsMessengerEXT");
			if (function)
			{
				function(m_Instance, m_DebugMessenger, nullptr);
				TR_CORE_TRACE("Debug messenger destroyed");
			}
		}

		vkDestroyInstance(m_Instance, nullptr);
		TR_CORE_TRACE("Vulkan instance destroyed");

		TR_CORE_INFO("-------VULKAN SHUTDOWN COMPLETE-------");
	}

	void VulkanContext::CreateInstance()
	{
		TR_CORE_TRACE("Creating Vulkan Instance");

		VkApplicationInfo appInfo{};
		appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pApplicationName = "Forge";
		appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
		appInfo.pEngineName = "Trinity";
		appInfo.engineVersion = VK_MAKE_VERSION(1, 4, 0);
		appInfo.apiVersion = VK_API_VERSION_1_4;

		VkInstanceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		createInfo.pApplicationInfo = &appInfo;

		uint32_t glfwExtensionCount = 0;
		const char** glfwExtensions;

		glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

		std::vector<const char*> requiredExtensions;
		for (uint32_t i = 0; i < glfwExtensionCount; i++)
		{
			requiredExtensions.emplace_back(glfwExtensions[i]);
		}

		requiredExtensions.emplace_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
		if (s_EnableValidationLayers)
		{
			requiredExtensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		}

		createInfo.enabledExtensionCount = (uint32_t)requiredExtensions.size();
		createInfo.ppEnabledExtensionNames = requiredExtensions.data();

		if (s_EnableValidationLayers)
		{
			if (!CheckValidationLayerSupport())
			{
				TR_CORE_CRITICAL("Validation layers requested but not available");
				
				return;
			}

			createInfo.enabledLayerCount = (uint32_t)m_ValidationLayers.size();
			createInfo.ppEnabledLayerNames = m_ValidationLayers.data();
		}

		else
		{
			createInfo.enabledLayerCount = 0;
		}

		VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
		if (s_EnableValidationLayers)
		{
			debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
			debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
			debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
			debugCreateInfo.pfnUserCallback = DebugCallback;
			debugCreateInfo.pUserData = nullptr;
			createInfo.pNext = &debugCreateInfo;
		}

		else
		{
			createInfo.pNext = nullptr;
		}

		if (vkCreateInstance(&createInfo, nullptr, &m_Instance) != VK_SUCCESS)
		{
			TR_CORE_CRITICAL("Failed to create vulkan instance");

			return;
		}

		createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;

		uint32_t extensionCount = 0;
		vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);

		std::vector<VkExtensionProperties> extensions(extensionCount);
		vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());

		TR_CORE_TRACE("Available extensions:");

		for (const auto& it_extension : extensions)
		{
			TR_CORE_TRACE("\t{}", it_extension.extensionName);
		}

		TR_CORE_TRACE("Vulkan Instance Created");
	}

	void VulkanContext::SetupDebugMessenger()
	{
		TR_CORE_TRACE("Creating debug messenger");

		if (!s_EnableValidationLayers)
		{
			return;
		}

		VkDebugUtilsMessengerCreateInfoEXT createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		createInfo.pfnUserCallback = DebugCallback;
		createInfo.pUserData = nullptr;

		auto function = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_Instance, "vkCreateDebugUtilsMessengerEXT");
		if (function != nullptr)
		{
			function(m_Instance, &createInfo, nullptr, &m_DebugMessenger);
		}

		TR_CORE_TRACE("Debug messenger created");
	}

	bool VulkanContext::CheckValidationLayerSupport()
	{
		uint32_t layerCount = 0;
		vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

		std::vector<VkLayerProperties> availableLayers(layerCount);
		vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

		for (const char* layerName : m_ValidationLayers)
		{
			bool layerFound = false;

			for (const auto& layerProperties : availableLayers)
			{
				if (strcmp(layerName, layerProperties.layerName) == 0)
				{
					layerFound = true;

					break;
				}
			}

			if (!layerFound)
			{
				return false;
			}
		}

		return true;
	}

	VKAPI_ATTR VkBool32 VKAPI_CALL VulkanContext::DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType,
		const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData)
	{
		if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
		{
			TR_CORE_ERROR("Validation Layer: {}", pCallbackData->pMessage);
		}

		return VK_FALSE;
	}
}
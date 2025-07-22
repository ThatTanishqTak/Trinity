#include "Trinity/trpch.h"

#include "Trinity/Vulkan/VulkanContext.h"
#include "Trinity/Utilities/Utilities.h"

#include <GLFW/glfw3.h>

namespace Trinity
{
	bool VulkanContext::Initialize()
	{
		TR_CORE_INFO("-------INITIALIZING VULKAN-------");

		CreateInstance();

		TR_CORE_INFO("-------VULKAN INITIALIZED-------");

		return true;
	}

	void VulkanContext::Shutdown()
	{
		TR_CORE_INFO("-------SHUTTING DOWN VULKAN-------");

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
		createInfo.enabledExtensionCount = glfwExtensionCount;
		createInfo.ppEnabledExtensionNames = glfwExtensions;
		createInfo.enabledLayerCount = 0;

		if (vkCreateInstance(&createInfo, nullptr, &m_Instance) != VK_SUCCESS)
		{
			TR_CORE_CRITICAL("Failed to create vulkan instance");

			return;
		}

		std::vector<const char*> requiredExtensions;
		for (uint32_t i = 0; i < glfwExtensionCount; i++)
		{
			requiredExtensions.emplace_back(glfwExtensions[i]);
		}

		requiredExtensions.emplace_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);

		createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
		createInfo.enabledExtensionCount = (uint32_t)requiredExtensions.size();
		createInfo.ppEnabledExtensionNames = requiredExtensions.data();

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
}
#pragma once

#include "vulkan/vulkan.h"

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

	private:
		VkInstance m_Instance;
	};
}
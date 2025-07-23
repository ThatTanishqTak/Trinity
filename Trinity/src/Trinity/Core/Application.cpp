#include "Application.h"

#include "Trinity/trpch.h"

namespace Trinity
{
	Application::Application(const ApplicationSpecification& specification) : m_Specification(specification)
	{
		TR_CORE_INFO("-------INITIALIZING APPLICATION-------");

		m_Window = std::make_unique<Window>(WindowSpecification{ m_Specification.Title, m_Specification.Width, m_Specification.Height });
		if (!m_Window->Initialize())
		{
			TR_CORE_ERROR("Failed to initialize window");
		}

		m_VulkanContext = std::make_unique<VulkanContext>(m_Window->GetNativeWindow());
		if (!m_VulkanContext->Initialize())
		{
			TR_CORE_ERROR("Failed to initialize vulkan");
		}

		TR_CORE_INFO("-------APPLICATION INITIALIZED-------");
	}

	Application::~Application()
	{
		TR_CORE_INFO("-------SHUTTING DOWN APPLICATION-------");

		if (m_VulkanContext)
		{
			m_VulkanContext->Shutdown();
		}

		if (m_Window)
		{
			m_Window->Shutdown();
		}

		TR_CORE_INFO("-------APPLICATION SHUTDOWN COMPLETE-------");
	}
}
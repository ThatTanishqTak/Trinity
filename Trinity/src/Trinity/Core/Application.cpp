#include "Application.h"

#include "Trinity/trpch.h"

namespace Trinity
{
	Application::Application(const ApplicationSpecification& specification) : m_Specification(specification)
	{
		m_Window = std::make_unique<Window>(WindowSpecification{ m_Specification.Title, m_Specification.Width, m_Specification.Height });
		if (!m_Window->Initialize())
		{
			TR_CORE_ERROR("Failed to initialize window");
		}
	}

	Application::~Application()
	{
		TR_CORE_INFO("Application Shuting Down");

		if (m_Window)
		{
			m_Window->Shutdown();
		}

		TR_CORE_INFO("Application Shutdown Complete");
	}
}
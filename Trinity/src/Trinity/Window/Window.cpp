#include "Trinity/trpch.h"

#include "Trinity/Utilities/Utilities.h"
#include "Trinity/Window/Window.h"

namespace Trinity
{
	Window::Window(const WindowSpecification& specification) : m_Specification(specification)
	{

	}

	bool Window::Initialize()
	{
		TR_CORE_INFO("-------INITIALIZING WINDOW-------");

		if (!glfwInit())
		{
			TR_CORE_ERROR("Failed to initialize GLFW");

			return false;
		}

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

		m_Window = glfwCreateWindow(m_Specification.Width, m_Specification.Height, m_Specification.Title.c_str(), nullptr, nullptr);
		if (!m_Window)
		{
			TR_CORE_ERROR("Failed to create window");
			glfwTerminate();

			return false;
		}

		TR_CORE_INFO("-------WINDOW INITIALIZED-------");

		return true;
	}

	void Window::Shutdown()
	{
		TR_CORE_INFO("-------SHUTTING DOWN WINDOW-------");

		if (m_Window)
		{
			glfwDestroyWindow(m_Window);
			m_Window = nullptr;

			glfwTerminate();
		}

		TR_CORE_INFO("-------WINDOW SHUTDOWN COMPLETE-------");
	}

	bool Window::ShouldWindowClose() const
	{
		return m_Window && glfwWindowShouldClose(m_Window);
	}

	void Window::PollEvents()
	{
		glfwPollEvents();
	}
}
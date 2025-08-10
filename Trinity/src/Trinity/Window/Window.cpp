#include "Trinity/trpch.h"

#include "Trinity/Utilities/Utilities.h"
#include "Trinity/Window/Window.h"
#include "Trinity/Events/ApplicationEvent.h"
#include "Trinity/Events/KeyEvent.h"
#include "Trinity/Events/MouseEvent.h"

namespace Trinity
{
    template<typename TEventConstructor>
    void Window::DispatchEvent(GLFWwindow* window, TEventConstructor&& constructor)
    {
        Window* l_Window = static_cast<Window*>(glfwGetWindowUserPointer(window));
        auto a_Event = constructor();
        if (l_Window->m_EventCallback)
        {
            l_Window->m_EventCallback(a_Event);
        }
    }

    Window::Window(const WindowSpecification& specification) : m_Specification(specification)
    {

    }

    bool Window::Initialize()
    {
        TR_CORE_INFO("-------INITIALIZING WINDOW-------");

        TR_CORE_TRACE("Initializing GLFW");

        if (!glfwInit())
        {
            TR_CORE_ERROR("Failed to initialize GLFW");

            return false;
        }

        TR_CORE_TRACE("GLFW initialized");

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        m_Window = glfwCreateWindow(m_Specification.Width, m_Specification.Height, m_Specification.Title.c_str(), nullptr, nullptr);
        if (!m_Window)
        {
            TR_CORE_ERROR("Failed to create window");
            glfwTerminate();

            return false;
        }

        TR_CORE_TRACE("Window dimensions: {}X{}, Title: {}", m_Specification.Width, m_Specification.Height, m_Specification.Title);

        glfwSetWindowUserPointer(m_Window, this);

        glfwSetWindowSizeCallback(m_Window, [](GLFWwindow* window, int width, int height)
            {
                Window* l_Window = static_cast<Window*>(glfwGetWindowUserPointer(window));
                l_Window->m_Specification.Width = width;
                l_Window->m_Specification.Height = height;
                Window::DispatchEvent(window, [=]() { return WindowResizeEvent(width, height); });
            });

        glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* window)
            {
                Window::DispatchEvent(window, []() { return WindowCloseEvent(); });
            });

        glfwSetWindowIconifyCallback(m_Window, [](GLFWwindow* window, int iconified)
            {
                if (iconified)
                {
                    Window::DispatchEvent(window, []() { return WindowMinimizeEvent(); });
                }
            });

        glfwSetWindowMaximizeCallback(m_Window, [](GLFWwindow* window, int maximized)
            {
                if (maximized)
                {
                    Window::DispatchEvent(window, []() { return WindowMaximizeEvent(); });
                }
            });

        glfwSetKeyCallback(m_Window, [](GLFWwindow* window, int key, int, int action, int)
            {
                switch (action)
                {
                case GLFW_PRESS:
                    Window::DispatchEvent(window, [=]() { return KeyPressedEvent(key, 0); });
                    break;
                case GLFW_RELEASE:
                    Window::DispatchEvent(window, [=]() { return KeyReleasedEvent(key); });
                    break;
                case GLFW_REPEAT:
                    Window::DispatchEvent(window, [=]() { return KeyPressedEvent(key, 1); });
                    break;
                }
            });

        glfwSetMouseButtonCallback(m_Window, [](GLFWwindow* window, int button, int action, int)
            {
                switch (action)
                {
                case GLFW_PRESS:
                    Window::DispatchEvent(window, [=]() { return MouseButtonPressedEvent(button); });
                    break;
                case GLFW_RELEASE:
                    Window::DispatchEvent(window, [=]() { return MouseButtonReleasedEvent(button); });
                    break;
                }
            });

        glfwSetCursorPosCallback(m_Window, [](GLFWwindow* window, double xpos, double ypos)
            {
                Window::DispatchEvent(window, [=]() { return MouseMovedEvent(static_cast<float>(xpos), static_cast<float>(ypos)); });
            });

        glfwSetScrollCallback(m_Window, [](GLFWwindow* window, double xoffset, double yoffset)
            {
                Window::DispatchEvent(window, [=]() { return MouseScrolledEvent(static_cast<float>(xoffset), static_cast<float>(yoffset)); });
            });

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
			TR_CORE_TRACE("Window handle destroyed");

			glfwTerminate();
			TR_CORE_TRACE("GLFW terminated");
		}

		TR_CORE_INFO("-------WINDOW SHUTDOWN COMPLETE-------");
	}

	//----------------------------------------------------------------------------------------------------------------------------------------------------//

	bool Window::ShouldWindowClose() const
	{
		return m_Window && glfwWindowShouldClose(m_Window);
	}

	void Window::PollEvents()
	{
		glfwPollEvents();
	}
}
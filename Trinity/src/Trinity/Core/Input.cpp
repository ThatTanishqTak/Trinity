#include "Trinity/trpch.h"

#include "Trinity/Core/Input.h"

namespace Trinity
{
	GLFWwindow* Input::s_Window = nullptr;

	void Input::Initialize(GLFWwindow* window)
	{
		s_Window = window;
	}

	bool Input::IsKeyPressed(int key)
	{
		return s_Window && glfwGetKey(s_Window, key) == GLFW_PRESS;
	}

	bool Input::IsMouseButtonPressed(int button)
	{
		return s_Window && glfwGetMouseButton(s_Window, button) == GLFW_PRESS;
	}

	glm::vec2 Input::GetMousePosition()
	{
		double l_X = 0.0;
		double l_Y = 0.0;
		if (s_Window)
		{
			glfwGetCursorPos(s_Window, &l_X, &l_Y);
		}

		return { static_cast<float>(l_X), static_cast<float>(l_Y) };
	}
}
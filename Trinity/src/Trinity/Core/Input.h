#pragma once

#include <glm/glm.hpp>
#include <GLFW/glfw3.h>

namespace Trinity
{
	class Input
	{
	public:
		static void Initialize(GLFWwindow* window);
		static bool IsKeyPressed(int key);
		static bool IsMouseButtonPressed(int button);
		static glm::vec2 GetMousePosition();

	private:
		static GLFWwindow* s_Window;
	};
}
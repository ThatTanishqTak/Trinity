#pragma once

#include <string>

#include <GLFW/glfw3.h>

namespace Trinity
{
	struct WindowSpecification
	{
		std::string Title{};
		unsigned int Width{};
		unsigned int Height{};
	};

	class Window
	{
	public:
		Window() = default;
		Window(const WindowSpecification& specification);
		~Window() = default;

		bool Initialize();
		void Shutdown();

		bool ShouldWindowClose() const;
		void PollEvents();

		GLFWwindow* GetNativeWindow() const { return m_Window; }

	private:
		WindowSpecification m_Specification;
		GLFWwindow* m_Window = nullptr;
	};
}
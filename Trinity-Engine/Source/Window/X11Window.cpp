#include "Trinity/Window/X11Window.hpp"

#include "Trinity/Core/Log.hpp"

namespace Trinity
{
	X11Window::X11Window() = default;
	X11Window::~X11Window() = default;

	bool X11Window::Initialize(const WindowSpecification& specification)
	{
		m_Title = specification.Title;

		TR_CORE_CRITICAL("X11Window is not implemented yet");

		return false;
	}

	void X11Window::Shutdown()
	{
	}

	bool X11Window::PollEvents()
	{
		return false;
	}
}
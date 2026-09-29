#include "Trinity/Window/WaylandWindow.hpp"

#include "Trinity/Core/Log.hpp"

namespace Trinity
{
	WaylandWindow::WaylandWindow() = default;
	WaylandWindow::~WaylandWindow() = default;

	bool WaylandWindow::Initialize(const WindowSpecification& specification)
	{
		m_Title = specification.Title;

		TR_CORE_CRITICAL("WaylandWindow is not implemented yet");

		return false;
	}

	void WaylandWindow::Shutdown()
	{
	}

	bool WaylandWindow::PollEvents()
	{
		return false;
	}
}
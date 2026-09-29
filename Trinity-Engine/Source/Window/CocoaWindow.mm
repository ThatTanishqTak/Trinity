#include "Trinity/Window/CocoaWindow.hpp"

#include "Trinity/Core/Log.hpp"

namespace Trinity
{
	CocoaWindow::CocoaWindow() = default;
	CocoaWindow::~CocoaWindow() = default;

	bool CocoaWindow::Initialize(const WindowSpecification& specification)
	{
		m_Title = specification.Title;

		TR_CORE_CRITICAL("CocoaWindow is not implemented yet");

		return false;
	}

	void CocoaWindow::Shutdown()
	{

	}

	bool CocoaWindow::PollEvents()
	{
		return false;
	}
}
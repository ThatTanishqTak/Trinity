#include "Application.h"

#include "Trinity/Events/ApplicationEvent.h"
#include "Trinity/Log.h"

namespace Trinity
{
	Application::Application()
	{

	}

	Application::~Application()
	{

	}

	void Application::Run()
	{
		WindowResizeEvent e(1280, 720);
		TR_CORE_TRACE(e);

		while (true);
	}
}
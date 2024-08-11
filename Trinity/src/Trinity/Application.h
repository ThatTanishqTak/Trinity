#pragma once

#include "Core.h"
#include "Events/Event.h"
#include "Trinity/Events/ApplicationEvent.h"
#include "Window.h"

namespace Trinity
{
	class TRINITY_API Application
	{
	public:
		Application();
		virtual ~Application();

		void Run();

		void OnEvent(Event& event);
	
	private:
		bool OnWindowClose(WindowCloseEvent& closeEvent);

		std::unique_ptr<Window> m_Window;
		bool m_Running = true;
	};

	Application* CreateApplication();
}
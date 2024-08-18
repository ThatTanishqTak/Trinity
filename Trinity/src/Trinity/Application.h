#pragma once

#include "Core.h"

#include "Window.h"
#include "Trinity/LayerStack.h"
#include "Trinity/Events/Event.h"
#include "Trinity/Events/ApplicationEvent.h"

#include "ImGui/ImGuiLayer.h"

namespace Trinity
{
	class TRINITY_API Application
	{
	public:
		Application();
		virtual ~Application();

		void Run();

		void OnEvent(Event& event);

		void PushLayer(Layer* layer);
		void PushOverlay(Layer* layer);

		inline Window& GetWindow() { return *m_Window; }
		inline static Application& Get() { return *s_Instance; }
	private:
		bool OnWindowClose(WindowCloseEvent& closeEvent);

		std::unique_ptr<Window> m_Window;
		
		bool m_Running = true;
		
		LayerStack m_LayerStack;
		ImGuiLayer* m_ImGuiLayer;

	private:
		static Application* s_Instance;
	};

	Application* CreateApplication();
}
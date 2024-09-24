#pragma once

#include "Core.h"

#include "Window.h"
#include "Trinity/Core/LayerStack.h"
#include "Trinity/Events/Event.h"
#include "Trinity/Events/ApplicationEvent.h"

#include "Trinity/Core/Timestep.h"

#include "Trinity/ImGui/ImGuiLayer.h"

namespace Trinity
{
	class Application
	{
	public:
		Application(const std::string& name = "Trinity Engine");
		virtual ~Application();

		void Run();

		void OnEvent(Event& event);

		void PushLayer(Layer* layer);
		void PushOverlay(Layer* layer);

		inline Window& GetWindow() { return *m_Window; }

		void Close();

		ImGuiLayer* GetImGuiLayer() { return m_ImGuiLayer; }

		inline static Application& Get() { return *s_Instance; }
	private:
		bool OnWindowClose(WindowCloseEvent& closeEvent);
		bool OnWindowResize(WindowResizeEvent& resizeEvent);

	private:
		std::unique_ptr<Window> m_Window;
		
		bool m_Running = true;
		bool m_Minimized = false;
		
		LayerStack m_LayerStack;
		ImGuiLayer* m_ImGuiLayer;

		float m_LastFrameTime;

	private:
		static Application* s_Instance;
	};

	Application* CreateApplication();
}
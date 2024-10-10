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
	struct ApplicationCommandLineArgs
	{
		int Count = 0;
		char** Args = nullptr;

		const char* operator[](int index) const
		{
			TR_CORE_ASSERT(index < Count, "");
			return Args[index];
		}
	};

	class Application
	{
	public:
		Application(const std::string& name = "Sandbox", ApplicationCommandLineArgs args = ApplicationCommandLineArgs());
		virtual ~Application();

		void Run();

		void OnEvent(Event& event);

		void PushLayer(Layer* layer);
		void PushOverlay(Layer* layer);

		inline Window& GetWindow() { return *m_Window; }

		void Close();

		ImGuiLayer* GetImGuiLayer() { return m_ImGuiLayer; }

		inline static Application& Get() { return *s_Instance; }

		ApplicationCommandLineArgs GetCommandLineArgs() const { return m_CommandLineArgs; }
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
		ApplicationCommandLineArgs m_CommandLineArgs;
		static Application* s_Instance;
	};

	Application* CreateApplication();
	Application* CreateApplication(ApplicationCommandLineArgs args);
}
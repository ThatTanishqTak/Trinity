#pragma once

#include "Trinity/Events/Event.hpp"
#include "Trinity/Layer/LayerStack.hpp"
#include "Trinity/Renderer/GraphicsAPI.hpp"
#include "Trinity/Window/Window.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Trinity
{
	struct ApplicationCommandLineArgs
	{
		int Count = 0;
		char** Args = nullptr;

		const char* operator[](int index) const { return (index >= 0 && index < Count) ? Args[index] : nullptr; }
	};

	struct ApplicationSpecification
	{
		std::string Title = "Trinity-Application";
		uint32_t Width = 1080;
		uint32_t Height = 720;

		GraphicsAPI API = GraphicsAPI::Vulkan;
		bool VSync = true;

		ApplicationCommandLineArgs Args;
	};

	class Gamepad;
	class Renderer;

	class Application
	{
	public:
		Application(const ApplicationSpecification& specification);
		virtual ~Application();

		Application(const Application&) = delete;
		Application& operator=(const Application&) = delete;
		Application(Application&&) = delete;
		Application& operator=(Application&&) = delete;

		static Application& Get() { return *s_Instance; }

		bool IsInitialized() const { return m_Initialized; }

		void Run();

		void Close();
		void RequestClose();

		void OnEvent(Event& event);

		void PushLayer(std::unique_ptr<Layer> layer);
		void PushOverlay(std::unique_ptr<Layer> overlay);
		void PopLayer(Layer* layer);
		void PopOverlay(Layer* overlay);

		Window& GetWindow() { return *m_Window; }
		Renderer* GetRenderer() { return m_Renderer.get(); }
		const ApplicationSpecification& GetApplicationSpecification() const { return m_ApplicationSpecification; }

	protected:
		virtual void OnInitialize() {}
		virtual void OnShutdown() {}

	private:
		struct PendingLayerChange
		{
			enum class Operation : uint8_t { PushLayer, PushOverlay, PopLayer, PopOverlay };

			Operation Type = Operation::PushLayer;
			std::unique_ptr<Layer> Owned;
			Layer* Target = nullptr;
		};

		bool Initialize();
		void Shutdown();

		void RunFrame();

		void QueueEvent(Event& event);
		void ProcessEventQueue();

		void ApplyPendingLayerChanges();
		void DiscardPendingLayerChanges();
		void ReleaseHeldInput();

	private:
		static Application* s_Instance;

		ApplicationSpecification m_ApplicationSpecification;
		WindowSpecification m_WindowSpecification;
		LayerStack m_LayerStack;

		std::unique_ptr<Window> m_Window;
		std::unique_ptr<Gamepad> m_Gamepad;
		std::unique_ptr<Renderer> m_Renderer;

		std::vector<std::unique_ptr<Event>> m_EventQueue;
		std::vector<std::unique_ptr<Event>> m_ProcessingEvents;
		std::vector<PendingLayerChange> m_PendingLayerChanges;

		bool m_Initialized = false;
		bool m_Running = true;
		bool m_InFrame = false;
	};

	Application* CreateApplication(ApplicationCommandLineArgs args);
}
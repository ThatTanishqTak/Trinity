#include "Trinity/Core/Application.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Events/ApplicationEvent.hpp"
#include "Trinity/Events/KeyEvent.hpp"
#include "Trinity/Events/MouseEvent.hpp"
#include "Trinity/Input/Gamepad.hpp"
#include "Trinity/Input/Input.hpp"
#include "Trinity/Renderer/Renderer.hpp"
#include "Trinity/Time/Time.hpp"

#include <algorithm>
#include <chrono>
#include <string_view>
#include <thread>
#include <utility>

namespace Trinity
{
	namespace
	{
		constexpr std::chrono::milliseconds s_MinimizedSleep{ 10 };
		constexpr std::chrono::nanoseconds s_NoRendererFrameTime{ 16666667 };

		GraphicsAPI ResolveGraphicsAPI(const ApplicationSpecification& specification)
		{
			GraphicsAPI l_API = specification.API;

			for (int l_Index = 1; l_Index < specification.Args.Count; ++l_Index)
			{
				const std::string_view l_Argument = specification.Args[l_Index];

				if (l_Argument == "--vulkan")
				{
					l_API = GraphicsAPI::Vulkan;
				}
				else if (l_Argument == "--dx12")
				{
					l_API = GraphicsAPI::DirectX12;
				}
				else if (l_Argument == "--metal")
				{
					l_API = GraphicsAPI::Metal;
				}
			}

			return l_API;
		}
	}

	Application* Application::s_Instance = nullptr;

	Application::Application(const ApplicationSpecification& specification) : m_ApplicationSpecification(specification)
	{
		TR_CORE_ASSERT(!s_Instance, "Only one Application can exist at a time");
		s_Instance = this;

		m_Initialized = Initialize();
		if (!m_Initialized)
		{
			TR_CORE_CRITICAL("Failed to initialize application");
		}
	}

	Application::~Application()
	{
		Shutdown();

		s_Instance = nullptr;
	}

	void Application::Run()
	{
		if (!m_Initialized)
		{
			TR_CORE_ERROR("Run() called on an application that failed to initialize");

			return;
		}

		OnInitialize();
		ApplyPendingLayerChanges();

		Time::Reset();

		std::chrono::steady_clock::time_point l_NextFrameTime = std::chrono::steady_clock::now();

		while (m_Running && m_Window->PollEvents())
		{
			RunFrame();

			if (m_Window->IsMinimized())
			{
				std::this_thread::sleep_for(s_MinimizedSleep);
			}
			else if (!m_Renderer)
			{
				l_NextFrameTime += s_NoRendererFrameTime;

				const std::chrono::steady_clock::time_point l_Now = std::chrono::steady_clock::now();
				if (l_NextFrameTime < l_Now)
				{
					l_NextFrameTime = l_Now;
				}
				else
				{
					std::this_thread::sleep_until(l_NextFrameTime);
				}
			}
		}

		if (m_Renderer)
		{
			m_Renderer->WaitIdle();
		}

		m_LayerStack.Clear();
		DiscardPendingLayerChanges();

		OnShutdown();
		DiscardPendingLayerChanges();
	}

	void Application::RunFrame()
	{
		if (m_InFrame)
		{
			return;
		}

		m_InFrame = true;

		Time::Tick();

		if (m_Gamepad)
		{
			m_Gamepad->Poll();
		}

		ProcessEventQueue();

		while (Time::ConsumeFixedStep())
		{
			for (const std::unique_ptr<Layer>& l_Layer : m_LayerStack)
			{
				l_Layer->OnFixedUpdate(Time::GetFixedDeltaTime());
			}
		}

		for (const std::unique_ptr<Layer>& l_Layer : m_LayerStack)
		{
			l_Layer->OnUpdate(Time::GetDeltaTime());
		}

		if (m_Renderer && !m_Window->IsMinimized() && m_Renderer->BeginFrame())
		{
			for (const std::unique_ptr<Layer>& l_Layer : m_LayerStack)
			{
				l_Layer->OnRender(Time::GetDeltaTime());
			}

			m_Renderer->EndFrame();
		}

		ApplyPendingLayerChanges();
		Input::EndFrame();

		m_InFrame = false;
	}

	void Application::OnEvent(Event& event)
	{
		Input::OnEvent(event);

		if (m_Renderer && event.GetEventType() == EventType::WindowResize)
		{
			const WindowResizeEvent& l_ResizeEvent = static_cast<const WindowResizeEvent&>(event);
			m_Renderer->RequestResize(l_ResizeEvent.GetWidth(), l_ResizeEvent.GetHeight());
		}

		for (auto l_Layer = m_LayerStack.rbegin(); l_Layer != m_LayerStack.rend(); ++l_Layer)
		{
			if (event.Handled)
			{
				break;
			}

			(*l_Layer)->OnEvent(event);
		}

		EventDispatcher l_Dispatcher(event);
		l_Dispatcher.Dispatch<WindowCloseEvent>([this](WindowCloseEvent& closeEvent)
		{
			if (closeEvent.IsCancelled())
			{
				TR_CORE_INFO("Close request cancelled by a layer");
			}
			else
			{
				Close();
			}

			return true;
		});

		if (event.GetEventType() == EventType::WindowLostFocus)
		{
			ReleaseHeldInput();
		}
	}

	void Application::Close()
	{
		m_Running = false;
	}

	void Application::RequestClose()
	{
		WindowCloseEvent l_Event;
		QueueEvent(l_Event);
	}

	void Application::PushLayer(std::unique_ptr<Layer> layer)
	{
		m_PendingLayerChanges.push_back({ PendingLayerChange::Operation::PushLayer, std::move(layer), nullptr });
	}

	void Application::PushOverlay(std::unique_ptr<Layer> overlay)
	{
		m_PendingLayerChanges.push_back({ PendingLayerChange::Operation::PushOverlay, std::move(overlay), nullptr });
	}

	void Application::PopLayer(Layer* layer)
	{
		m_PendingLayerChanges.push_back({ PendingLayerChange::Operation::PopLayer, nullptr, layer });
	}

	void Application::PopOverlay(Layer* overlay)
	{
		m_PendingLayerChanges.push_back({ PendingLayerChange::Operation::PopOverlay, nullptr, overlay });
	}

	bool Application::Initialize()
	{
		TR_CORE_INFO("------- INITIALIZING APPLICATION -------");

		m_ApplicationSpecification.API = ResolveGraphicsAPI(m_ApplicationSpecification);
		if (!IsGraphicsAPISupported(m_ApplicationSpecification.API))
		{
			TR_CORE_CRITICAL("{} is not supported on this platform", GraphicsAPIToString(m_ApplicationSpecification.API));

			return false;
		}

		m_WindowSpecification.Title = m_ApplicationSpecification.Title;
		m_WindowSpecification.Width = m_ApplicationSpecification.Width;
		m_WindowSpecification.Height = m_ApplicationSpecification.Height;

		m_Window = Window::Create(m_WindowSpecification);
		if (!m_Window)
		{
			TR_CORE_CRITICAL("Failed to create window");

			return false;
		}

		m_Window->SetEventCallback([this](Event& event) { QueueEvent(event); });
		m_Window->SetFrameCallback([this]() { RunFrame(); });

		if (!m_Window->Initialize(m_WindowSpecification))
		{
			TR_CORE_CRITICAL("Failed to initialize window");

			m_Window->SetEventCallback(nullptr);
			m_Window->SetFrameCallback(nullptr);
			m_Window.reset();

			return false;
		}

		m_Renderer = Renderer::Create(m_ApplicationSpecification.API, m_Window->GetNativeHandle(), m_ApplicationSpecification.VSync);
		if (m_Renderer)
		{
			m_Window->SetRendererAttached(true);
			m_Renderer->RequestResize(m_Window->GetWidth(), m_Window->GetHeight());
		}
		else
		{
			TR_CORE_WARN("{} renderer unavailable, running without one", GraphicsAPIToString(m_ApplicationSpecification.API));
		}

		m_Gamepad = Gamepad::Create();
		if (m_Gamepad)
		{
			m_Gamepad->SetEventCallback([this](Event& event) { QueueEvent(event); });

			if (!m_Gamepad->Initialize())
			{
				TR_CORE_WARN("Gamepad input unavailable");
				m_Gamepad.reset();
			}
		}

		TR_CORE_INFO("------- APPLICATION INITIALIZED -------");

		return true;
	}

	void Application::Shutdown()
	{
		TR_CORE_INFO("------- SHUTTING DOWN APPLICATION -------");

		if (m_Window)
		{
			m_Window->SetEventCallback(nullptr);
			m_Window->SetFrameCallback(nullptr);
		}

		if (m_Gamepad)
		{
			m_Gamepad->SetEventCallback(nullptr);
		}

		m_EventQueue.clear();
		m_ProcessingEvents.clear();
		m_PendingLayerChanges.clear();
		m_LayerStack.Clear();

		if (m_Renderer)
		{
			m_Renderer->Shutdown();
			m_Renderer.reset();

			if (m_Window)
			{
				m_Window->SetRendererAttached(false);
			}
		}

		if (m_Gamepad)
		{
			m_Gamepad->Shutdown();
			m_Gamepad.reset();
		}

		if (m_Window)
		{
			m_Window->Shutdown();
			m_Window.reset();
		}

		TR_CORE_INFO("------- APPLICATION SHUTDOWN COMPLETE -------");
	}

	void Application::QueueEvent(Event& event)
	{
		if (event.GetEventType() == EventType::WindowResize && !m_EventQueue.empty() && m_EventQueue.back()->GetEventType() == EventType::WindowResize)
		{
			m_EventQueue.back() = event.Clone();

			return;
		}

		m_EventQueue.push_back(event.Clone());
	}

	void Application::ProcessEventQueue()
	{
		m_EventQueue.swap(m_ProcessingEvents);

		for (const std::unique_ptr<Event>& l_Event : m_ProcessingEvents)
		{
			OnEvent(*l_Event);
		}

		m_ProcessingEvents.clear();
	}

	void Application::ApplyPendingLayerChanges()
	{
		while (!m_PendingLayerChanges.empty())
		{
			std::vector<PendingLayerChange> l_Changes;
			l_Changes.swap(m_PendingLayerChanges);

			const bool l_HasPop = std::any_of(l_Changes.begin(), l_Changes.end(), [](const PendingLayerChange& change)
			{
				return change.Type == PendingLayerChange::Operation::PopLayer || change.Type == PendingLayerChange::Operation::PopOverlay;
			});

			if (l_HasPop && m_Renderer)
			{
				m_Renderer->WaitIdle();
			}

			for (PendingLayerChange& l_Change : l_Changes)
			{
				switch (l_Change.Type)
				{
					case PendingLayerChange::Operation::PushLayer:
					{
						m_LayerStack.PushLayer(std::move(l_Change.Owned));

						break;
					}
					case PendingLayerChange::Operation::PushOverlay:
					{
						m_LayerStack.PushOverlay(std::move(l_Change.Owned));

						break;
					}
					case PendingLayerChange::Operation::PopLayer:
					{
						m_LayerStack.PopLayer(l_Change.Target);

						break;
					}
					case PendingLayerChange::Operation::PopOverlay:
					{
						m_LayerStack.PopOverlay(l_Change.Target);

						break;
					}
				}
			}
		}
	}

	void Application::DiscardPendingLayerChanges()
	{
		const auto l_PushCount = std::count_if(m_PendingLayerChanges.begin(), m_PendingLayerChanges.end(), [](const PendingLayerChange& change)
		{
			return change.Type == PendingLayerChange::Operation::PushLayer || change.Type == PendingLayerChange::Operation::PushOverlay;
		});

		if (l_PushCount > 0)
		{
			TR_CORE_WARN("Ignoring {} layer push(es) requested during shutdown", l_PushCount);
		}

		m_PendingLayerChanges.clear();
	}

	void Application::ReleaseHeldInput()
	{
		for (KeyCode l_Key : Input::GetHeldKeys())
		{
			KeyReleasedEvent l_Event(l_Key, Input::GetHeldKeyLabel(l_Key));
			OnEvent(l_Event);
		}

		for (MouseCode l_Button : Input::GetHeldMouseButtons())
		{
			MouseButtonReleasedEvent l_Event(l_Button);
			OnEvent(l_Event);
		}
	}
}
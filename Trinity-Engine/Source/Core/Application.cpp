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
#include <optional>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace Trinity
{
	namespace
	{
		constexpr std::chrono::milliseconds s_MinimizedSleep{ 10 };

		bool IsGPUValidationRequested(const ApplicationCommandLineArgs& args)
		{
#if defined(NDEBUG)
			bool l_Enabled = false;
#else
			bool l_Enabled = true;
#endif

			for (int l_Index = 1; l_Index < args.Count; ++l_Index)
			{
				const std::string_view l_Argument = args[l_Index];

				if (l_Argument == "--validation")
				{
					l_Enabled = true;
				}
				else if (l_Argument == "--no-validation")
				{
					l_Enabled = false;
				}
			}

			return l_Enabled;
		}

		std::optional<GraphicsAPI> GetForcedGraphicsAPI(const ApplicationCommandLineArgs& args)
		{
			std::optional<GraphicsAPI> l_API;

			for (int l_Index = 1; l_Index < args.Count; ++l_Index)
			{
				const std::string_view l_Argument = args[l_Index];

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
				else if (l_Argument == "--null-renderer")
				{
					l_API = GraphicsAPI::Null;
				}
			}

			return l_API;
		}

		std::vector<GraphicsAPI> GetGraphicsAPIFallbackOrder(GraphicsAPI preferred)
		{
			std::vector<GraphicsAPI> l_Order{ preferred };

			for (GraphicsAPI l_API : { GraphicsAPI::DirectX12, GraphicsAPI::Metal, GraphicsAPI::Vulkan })
			{
				if (l_API != preferred && IsGraphicsAPISupported(l_API))
				{
					l_Order.push_back(l_API);
				}
			}

			if (preferred != GraphicsAPI::Null)
			{
				l_Order.push_back(GraphicsAPI::Null);
			}

			return l_Order;
		}

		template<typename T>
		bool TryCoalesceEvent(std::vector<std::unique_ptr<Event>>& queue, const Event& event)
		{
			if (event.GetEventType() != T::GetStaticType() || queue.empty() || queue.back()->GetEventType() != T::GetStaticType())
			{
				return false;
			}

			static_cast<T&>(*queue.back()) = static_cast<const T&>(event);

			return true;
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

		while (m_Running && m_Window->PollEvents())
		{
			RunFrame();

			if (m_Window->IsMinimized())
			{
				std::this_thread::sleep_for(s_MinimizedSleep);
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

		const std::optional<GraphicsAPI> l_ForcedAPI = GetForcedGraphicsAPI(m_ApplicationSpecification.Args);
		if (l_ForcedAPI)
		{
			m_ApplicationSpecification.API = *l_ForcedAPI;
		}

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

		const RenderDeviceSpecification l_DeviceSpecification
		{
			.VSync = m_ApplicationSpecification.VSync,
			.Validation = IsGPUValidationRequested(m_ApplicationSpecification.Args)
		};

		if (l_DeviceSpecification.Validation)
		{
			TR_CORE_INFO("GPU validation is on (--no-validation turns it off)");
		}

		const GraphicsAPI l_PreferredAPI = m_ApplicationSpecification.API;
		const std::vector<GraphicsAPI> l_Candidates = l_ForcedAPI ? std::vector<GraphicsAPI>{ *l_ForcedAPI } : GetGraphicsAPIFallbackOrder(l_PreferredAPI);

		for (GraphicsAPI l_API : l_Candidates)
		{
			m_Renderer = Renderer::Create(l_API, m_Window->GetNativeHandle(), l_DeviceSpecification);
			if (m_Renderer)
			{
				break;
			}

			TR_CORE_WARN("{} renderer failed to start", GraphicsAPIToString(l_API));
		}

		if (!m_Renderer)
		{
			if (l_ForcedAPI)
			{
				TR_CORE_CRITICAL("{} was forced on the command line and failed to start", GraphicsAPIToString(*l_ForcedAPI));
			}
			else
			{
				TR_CORE_CRITICAL("No graphics API could be started");
			}

			return false;
		}

		if (m_Renderer->GetAPI() != l_PreferredAPI)
		{
			TR_CORE_WARN("Fell back from {} to {}", GraphicsAPIToString(l_PreferredAPI), GraphicsAPIToString(m_Renderer->GetAPI()));
		}

		if (m_Renderer->GetAPI() == GraphicsAPI::Null && !l_ForcedAPI)
		{
			TR_CORE_WARN("No GPU renderer could start, so nothing will be drawn (--null-renderer selects the Null renderer on purpose)");
		}

		m_ApplicationSpecification.API = m_Renderer->GetAPI();
		m_Renderer->RequestResize(m_Window->GetWidth(), m_Window->GetHeight());
		if (m_Renderer->GetAPI() != GraphicsAPI::Null)
		{
			m_Window->SetRendererAttached(true);
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
		if (TryCoalesceEvent<WindowResizeEvent>(m_EventQueue, event) || TryCoalesceEvent<MouseMovedEvent>(m_EventQueue, event))
		{
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
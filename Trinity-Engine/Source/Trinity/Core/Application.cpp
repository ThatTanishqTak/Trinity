#include "Trinity/Core/Application.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Platform.hpp"
#include "Trinity/Core/Timestep.hpp"
#include "Trinity/Input/Input.hpp"

#include <charconv>
#include <chrono>
#include <filesystem>
#include <ranges>

namespace Trinity
{
    namespace
    {
#if defined(TR_DEBUG)
        constexpr std::string_view c_ConfigurationName = "Debug";
#elif defined(TR_RELEASE)
        constexpr std::string_view c_ConfigurationName = "Release";
#else
        constexpr std::string_view c_ConfigurationName = "Distribution";
#endif
    }

    const char* GetVersionString()
    {
        return TR_VERSION_STRING;
    }

    std::string_view ApplicationCommandLineArgs::operator[](int index) const
    {
        TR_CORE_ASSERT(index >= 0 && index < Count);

        return Args[index];
    }

    std::optional<std::string_view> ApplicationCommandLineArgs::GetOption(std::string_view name) const
    {
        for (int it_Index = 1; it_Index < Count; ++it_Index)
        {
            std::string_view l_Argument = Args[it_Index];
            if (!l_Argument.starts_with("--"))
            {
                continue;
            }
            l_Argument.remove_prefix(2);

            if (l_Argument == name)
            {
                return std::string_view{};
            }

            if (l_Argument.starts_with(name) && l_Argument.size() > name.size() && l_Argument[name.size()] == '=')
            {
                return l_Argument.substr(name.size() + 1);
            }
        }

        return std::nullopt;
    }

    bool ApplicationCommandLineArgs::HasOption(std::string_view name) const
    {
        return GetOption(name).has_value();
    }

    Application* Application::s_Instance = nullptr;

    Application::Application(ApplicationSpecification specification) : m_Specification(std::move(specification))
    {
        TR_CORE_ASSERT(s_Instance == nullptr, "Only one Application may exist.");
        s_Instance = this;

        const ApplicationCommandLineArgs& l_Args = m_Specification.CommandLineArgs;

        if (l_Args.HasOption("headless"))
        {
            m_Specification.Window.Headless = true;
        }

        if (const auto it_Frame = l_Args.GetOption("frames"))
        {
            std::uint64_t l_Value = 0;
            const auto [a_End, a_Error] = std::from_chars(it_Frame->data(), it_Frame->data() + it_Frame->size(), l_Value);
            if (a_Error == std::errc{} && a_End == it_Frame->data() + it_Frame->size())
            {
                m_Specification.MaxFrames = l_Value;
            }
            else
            {
                TR_CORE_WARN("Ignoring --frames={}: expected a non-negative integer", *it_Frame);
            }
        }

        if (l_Args.HasOption("d3d12"))
        {
            m_Specification.Graphics = GraphicsAPI::D3D12;
        }

        if (l_Args.HasOption("vulkan"))
        {
            m_Specification.Graphics = GraphicsAPI::Vulkan;
        }

        if (m_Specification.Window.Headless)
        {
            m_Specification.Graphics = GraphicsAPI::None;
        }

        if (!IsGraphicsAPIAvailable(m_Specification.Graphics))
        {
            const GraphicsAPI l_Fallback = GetDefaultGraphicsAPI();

            TR_CORE_WARN("{} is not available in this build; using {} instead.", ToString(m_Specification.Graphics), ToString(l_Fallback));
            m_Specification.Graphics = l_Fallback;
        }

        if (m_Specification.Window.Title.empty())
        {
            m_Specification.Window.Title = m_Specification.Name;
        }

        TR_CORE_INFO("Starting '{}' (graphics: {}{})", m_Specification.Name, ToString(m_Specification.Graphics), m_Specification.Window.Headless ? ", headless" : "");

        m_Window = Window::Create(m_Specification.Window);
        m_Window->SetEventCallback(TR_BIND_EVENT_FN(OnEvent));
    }

    Application::~Application()
    {
        m_LayerStack.Clear();
        m_Window.reset();
        s_Instance = nullptr;
    }

    Application& Application::Get()
    {
        TR_CORE_ASSERT(s_Instance != nullptr, "No Application exists.");

        return *s_Instance;
    }

    void Application::Close()
    {
        m_Running = false;
    }

    void Application::Run()
    {
        using Clock = std::chrono::steady_clock;
        auto l_LastFrameTime = Clock::now();

        while (m_Running)
        {
            const auto l_Now = Clock::now();
            const Timestep l_Timestep = std::chrono::duration<float>(l_Now - l_LastFrameTime).count();
            l_LastFrameTime = l_Now;

            m_Window->PollEvents();

            if (!m_Minimized)
            {
                for (const Scope<Layer>& it_Layer : m_LayerStack)
                {
                    it_Layer->OnUpdate(l_Timestep);
                }
            }

            ++m_FrameCount;
            if (m_Specification.MaxFrames != 0 && m_FrameCount >= m_Specification.MaxFrames)
            {
                Close();
            }
        }

        TR_CORE_INFO("'{}' ran for {} frames.", m_Specification.Name, m_FrameCount);
    }

    void Application::OnEvent(Event& event)
    {
        Input::OnEvent(event);

        EventDispatcher l_Dispatcher(event);
        l_Dispatcher.Dispatch<WindowCloseEvent>(TR_BIND_EVENT_FN(OnWindowClose));
        l_Dispatcher.Dispatch<WindowResizeEvent>(TR_BIND_EVENT_FN(OnWindowResize));

        for (const Scope<Layer>& it_Layer : m_LayerStack | std::views::reverse)
        {
            if (event.Handled)
            {
                break;
            }
            
            it_Layer->OnEvent(event);
        }
    }

    bool Application::OnWindowClose(WindowCloseEvent&)
    {
        Close();

        return true;
    }

    bool Application::OnWindowResize(WindowResizeEvent& event)
    {
        m_Minimized = event.GetWidth() == 0 || event.GetHeight() == 0;

        return false;
    }

    int Main(int argc, char** argv)
    {
        Platform::Initialize();

        std::filesystem::path l_LogFile = "Logs/Trinity.log";
        if (argc > 0 && argv != nullptr && argv[0] != nullptr)
        {
            l_LogFile = std::filesystem::path("Logs") / std::filesystem::path(argv[0]).stem().concat(".log");
        }

        Log::Initialize(l_LogFile);

        TR_CORE_INFO("Trinity {} - {} {}", GetVersionString(), Platform::GetName(), c_ConfigurationName);

        const Scope<Application> l_Application{ CreateApplication({ argc, argv }) };
        TR_CORE_ASSERT(l_Application != nullptr, "CreateApplication returned null.");
        if (l_Application)
        {
            l_Application->Run();
        }

        Log::Shutdown();
        Platform::Shutdown();

        return 0;
    }
}
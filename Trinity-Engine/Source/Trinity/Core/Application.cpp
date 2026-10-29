#include "Trinity/Core/Application.hpp"

#include "Trinity/Asset/AssetManager.hpp"
#include "Trinity/Audio/AudioLoaders.hpp"
#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/ConsoleVariable.hpp"
#include "Trinity/Core/CpuFeatures.hpp"
#include "Trinity/Core/JobSystem.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/MainThread.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/Platform.hpp"
#include "Trinity/Core/Profiler.hpp"
#include "Trinity/Core/Timestep.hpp"
#include "Trinity/FileSystem/FileSystem.hpp"
#include "Trinity/Input/Input.hpp"
#include "Trinity/Input/InputActionsRuntime.hpp"
#include "Trinity/Physics/CollisionShapeLoaders.hpp"
#include "Trinity/Scene/PrefabLoaders.hpp"
#include "Trinity/Scene/SceneSerializer.hpp"
#include "Trinity/Scripting/DotNetHost.hpp"
#include "Trinity/Scripting/NativeComponentRegistry.hpp"
#include "Trinity/Scripting/ScriptBackend.hpp"
#include "Trinity/Scripting/ScriptClassRegistry.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <format>
#include <iterator>
#include <ranges>
#include <span>
#include <string>
#include <vector>

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

        ConsoleVariable<bool> s_GPUValidationVariable("renderer.gpu_validation", false, "Adds GPU-based validation to the graphics debug layer in Debug builds. Much slower", ConsoleVariableFlags::ReadOnly);
        ConsoleVariable<std::int32_t> s_FrameCapacityVariable("memory.frame_capacity", 0, "Frame allocator capacity per buffer in KiB; 0 uses the application's default", ConsoleVariableFlags::ReadOnly);

        std::size_t GetFrameAllocatorCapacity(const ApplicationSpecification& specification)
        {
            const std::int32_t l_Kibibytes = s_FrameCapacityVariable.Get();

            return l_Kibibytes > 0 ? static_cast<std::size_t>(l_Kibibytes) * 1024 : specification.FrameAllocatorCapacity;
        }

        std::optional<std::string_view> MatchOption(std::string_view argument, std::string_view name)
        {
            if (!argument.starts_with("--"))
            {
                return std::nullopt;
            }
            argument.remove_prefix(2);

            if (argument == name)
            {
                return std::string_view{};
            }

            if (argument.starts_with(name) && argument.size() > name.size() && argument[name.size()] == '=')
            {
                return argument.substr(name.size() + 1);
            }

            return std::nullopt;
        }

        // Keeps an application name usable as one directory name on every platform
        std::string ToDirectoryName(std::string_view name)
        {
            std::string l_Result;
            for (const char it_Character : name)
            {
                const bool l_Forbidden = static_cast<unsigned char>(it_Character) < 0x20 || std::string_view("\\/:*?\"<>|").find(it_Character) != std::string_view::npos;
                l_Result.push_back(l_Forbidden ? '_' : it_Character);
            }

            while (!l_Result.empty() && (l_Result.back() == '.' || l_Result.back() == ' '))
            {
                l_Result.pop_back();
            }

            return l_Result.empty() ? std::string("Application") : l_Result;
        }

        // One line per monitor, to compare with Windows' display settings
        void LogMonitors()
        {
            const std::vector<Platform::MonitorInfo> l_Monitors = Platform::GetMonitors();
            for (std::size_t it_Index = 0; it_Index < l_Monitors.size(); ++it_Index)
            {
                const Platform::MonitorInfo& l_Monitor = l_Monitors[it_Index];
                TR_CORE_INFO("Monitor {} of {}: {}{}, {}x{} at ({}, {}), work area {}x{} at ({}, {}), {:.0f}% scale", it_Index + 1, l_Monitors.size(), l_Monitor.Name, l_Monitor.Primary ? " (primary)" : "", l_Monitor.Area.Width, l_Monitor.Area.Height, l_Monitor.Area.X, l_Monitor.Area.Y, l_Monitor.WorkArea.Width, l_Monitor.WorkArea.Height, l_Monitor.WorkArea.X, l_Monitor.WorkArea.Y, l_Monitor.DpiScale * 100.0f);
            }
        }

        // Engine data, the compiled shaders and the fonts, sits beside the executable
        void MountEngineDirectory()
        {
            const std::filesystem::path l_Directory = Platform::GetExecutableDirectory() / "Engine";

            std::error_code l_Error;
            if (!std::filesystem::is_directory(l_Directory, l_Error))
            {
                TR_CORE_INFO("No engine data at '{}', so /engine is not mounted", l_Directory.string());

                return;
            }

            FileSystem::MountDirectory("/engine", l_Directory);
        }

        std::filesystem::path ToDirectoryPath(std::string_view name)
        {
            const std::string l_Name = ToDirectoryName(name);

            return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(l_Name.data()), l_Name.size()));
        }

        // Where an application keeps its own files in the user's folder: a game under its company's and product's names, as Unity's are, and Trinity's own programs under Trinity
        std::filesystem::path GetUserFolder(const std::filesystem::path& base, std::string_view company, std::string_view name)
        {
            return company.empty() ? base / "Trinity" : base / ToDirectoryPath(company) / ToDirectoryPath(name);
        }

        // Every program logs under --log-dir when given, or else in a folder of the user's own, as Unity keeps Editor.log and a game's Player.log, so its log is in one place however it was started and can be written wherever it is installed. A folder the log library cannot name, as one the system's code page cannot spell, gives way to Logs under the working directory
        std::filesystem::path GetLogDirectory(const ApplicationCommandLineArgs& args, std::string_view company = {}, std::string_view name = {})
        {
            if (const std::optional<std::string_view> l_Override = args.GetOption("log-dir"); l_Override && !l_Override->empty())
            {
                return std::filesystem::path(*l_Override);
            }

            const std::filesystem::path l_Base = Platform::GetUserDataDirectory();
            if (l_Base.empty())
            {
                return "Logs";
            }

            const std::filesystem::path l_Directory = GetUserFolder(l_Base, company, name) / "Logs";
            try
            {
                static_cast<void>(l_Directory.string());
            }
            catch (const std::exception&)
            {
                return "Logs";
            }

            return l_Directory;
        }

        // Named for the executable, so each program's is its own in a shared folder
        std::filesystem::path GetLogFile(const std::filesystem::path& directory, const ApplicationCommandLineArgs& args, std::string_view extension)
        {
            const std::filesystem::path l_Stem = args.Count > 0 && args.Args != nullptr && args.Args[0] != nullptr ? std::filesystem::path(args.Args[0]).stem() : std::filesystem::path("Trinity");

            return directory / std::filesystem::path(l_Stem).concat(extension);
        }

        void MountSaveDirectory(const ApplicationCommandLineArgs& args, std::string_view company, std::string_view applicationName)
        {
            std::filesystem::path l_Directory;
            if (const std::optional<std::string_view> l_Override = args.GetOption("saves"); l_Override && !l_Override->empty())
            {
                l_Directory = std::filesystem::path(*l_Override);
            }
            else
            {
                std::filesystem::path l_Base = Platform::GetUserDataDirectory();
                if (l_Base.empty())
                {
                    TR_CORE_WARN("No per-user data folder was found; saving under the working directory instead");
                    l_Base = "UserData";
                }

                // Trinity's own programs share its folder, so each keeps its saves under its own name
                const std::filesystem::path l_Folder = GetUserFolder(l_Base, company, applicationName);
                l_Directory = (company.empty() ? l_Folder / ToDirectoryPath(applicationName) : l_Folder) / "Saved";
            }

            std::error_code l_Error;
            std::filesystem::create_directories(l_Directory, l_Error);
            if (l_Error)
            {
                TR_CORE_ERROR("Could not create the save folder '{}': {}", l_Directory.string(), l_Error.message());

                return;
            }

            FileSystem::MountDirectory("/saves", l_Directory, MountAccess::ReadWrite);
        }

        std::optional<std::uint64_t> ParseUnsigned(std::string_view text)
        {
            std::uint64_t l_Value = 0;
            const auto [a_End, a_Error] = std::from_chars(text.data(), text.data() + text.size(), l_Value);
            if (a_Error != std::errc{} || a_End != text.data() + text.size())
            {
                return std::nullopt;
            }

            return l_Value;
        }

        ProfilerSpecification GetProfilerSpecification(const ApplicationCommandLineArgs& args, const std::filesystem::path& defaultCapturePath)
        {
            ProfilerSpecification l_Specification;
            l_Specification.LogSummary = args.HasOption("profile");

            if (const auto a_Path = args.GetOption("profile-capture"))
            {
                l_Specification.CapturePath = a_Path->empty() ? defaultCapturePath : std::filesystem::path(*a_Path);
            }

            if (const auto a_Frames = args.GetOption("profile-capture-frames"))
            {
                if (const std::optional<std::uint64_t> l_Frames = ParseUnsigned(*a_Frames))
                {
                    l_Specification.CaptureFrames = *l_Frames;
                }
                else
                {
                    TR_CORE_WARN("Ignoring --profile-capture-frames={}: expected a non-negative integer", *a_Frames);
                }
            }

            return l_Specification;
        }
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
            if (const std::optional<std::string_view> l_Value = MatchOption(Args[it_Index], name))
            {
                return l_Value;
            }
        }

        return std::nullopt;
    }

    std::vector<std::string_view> ApplicationCommandLineArgs::GetAllOptions(std::string_view name) const
    {
        std::vector<std::string_view> l_Values;
        for (int it_Index = 1; it_Index < Count; ++it_Index)
        {
            if (const std::optional<std::string_view> l_Value = MatchOption(Args[it_Index], name))
            {
                l_Values.push_back(*l_Value);
            }
        }

        return l_Values;
    }

    bool ApplicationCommandLineArgs::HasOption(std::string_view name) const
    {
        return GetOption(name).has_value();
    }

    Application* Application::s_Instance = nullptr;

    Application::Application(ApplicationSpecification specification) : m_Specification(std::move(specification)), m_FrameAllocator(GetFrameAllocatorCapacity(m_Specification))
    {
        TR_CORE_ASSERT(s_Instance == nullptr, "Only one Application may exist.");
        s_Instance = this;

        const ApplicationCommandLineArgs& l_Args = m_Specification.CommandLineArgs;

        // The log opens in the application's own folder, with every line before it first
        const std::filesystem::path l_LogFile = GetLogFile(GetLogDirectory(l_Args, m_Specification.Company, m_Specification.Name), l_Args, ".log");
        Log::OpenFile(l_LogFile);
        FileSystem::MountDirectory("/logs", l_LogFile.parent_path());

        if (l_Args.HasOption("headless"))
        {
            m_Specification.Window.Headless = true;
        }

        if (const auto it_Frame = l_Args.GetOption("frames"))
        {
            if (const std::optional<std::uint64_t> l_Value = ParseUnsigned(*it_Frame))
            {
                m_Specification.MaxFrames = *l_Value;
            }
            else
            {
                TR_CORE_WARN("Ignoring --frames={}: expected a non-negative integer", *it_Frame);
            }
        }

        // The order's first this build has, unless an option picks one, with the rest of it to fall back on
        std::vector<GraphicsAPI> l_Fallbacks;
        if (!l_Args.HasOption("d3d12") && !l_Args.HasOption("vulkan"))
        {
            std::ranges::copy_if(m_Specification.GraphicsOrder, std::back_inserter(l_Fallbacks), [](GraphicsAPI api) { return IsGraphicsAPIAvailable(api); });
            if (!l_Fallbacks.empty())
            {
                m_Specification.Graphics = l_Fallbacks.front();
                l_Fallbacks.erase(l_Fallbacks.begin());
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
            l_Fallbacks.clear();
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

        MountSaveDirectory(l_Args, m_Specification.Company, m_Specification.Name);

        if (m_Specification.StartDotNet)
        {
            static_cast<void>(DotNetHost::Start(Platform::GetExecutableDirectory() / "Scripting"));
        }

        if (!m_Specification.Window.Headless)
        {
            LogMonitors();
        }

        m_Window = Window::Create(m_Specification.Window);
        m_Window->SetEventCallback(TR_BIND_EVENT_FN(OnEvent));
        m_Window->SetRefreshCallback([this]() { RunFrame(false); });

        // Only an application with a window reads gamepads, so a headless one has none
        if (!m_Specification.Window.Headless)
        {
            Input::InitializeGamepads();
        }

        CreateDevice(l_Fallbacks);
        m_Renderer = CreateScope<Renderer>(*m_Device, *m_Window, m_Specification.Window.Title);

        if (m_Specification.StartAudio)
        {
            AudioEngineSpecification l_Audio;
            l_Audio.Output = m_Specification.Window.Headless ? AudioOutput::Silent : AudioOutput::Device;
            m_AudioEngine = CreateScope<AudioEngine>(l_Audio);
        }
    }

    Application::~Application()
    {
        m_LayerStack.Clear();
        JobSystem::Wait(m_FrameJobs);
        DotNetHost::Stop();
        m_AudioEngine.reset();
        Input::ShutdownGamepads();
        m_Renderer.reset();
        m_Device.reset();
        m_Window.reset();
        s_Instance = nullptr;
    }

    // The specification's graphics API, then each fallback in turn while a device cannot be made, and None last
    void Application::CreateDevice(std::span<const GraphicsAPI> fallbacks)
    {
        RHI::DeviceSpecification l_Specification;
#if defined(TR_DEBUG)
        l_Specification.EnableValidation = true;
        l_Specification.EnableGPUValidation = s_GPUValidationVariable.Get();
#endif

        std::vector<GraphicsAPI> l_APIs{ m_Specification.Graphics };
        for (const GraphicsAPI it_API : fallbacks)
        {
            if (std::ranges::find(l_APIs, it_API) == l_APIs.end())
            {
                l_APIs.push_back(it_API);
            }
        }

        if (std::ranges::find(l_APIs, GraphicsAPI::None) == l_APIs.end())
        {
            l_APIs.push_back(GraphicsAPI::None);
        }

        for (std::size_t it_Index = 0; it_Index < l_APIs.size(); ++it_Index)
        {
            l_Specification.API = l_APIs[it_Index];
            Expected<Scope<RHI::Device>, std::string> l_Device = RHI::CreateDevice(l_Specification);
            if (l_Device || it_Index + 1 == l_APIs.size())
            {
                m_Specification.Graphics = l_Specification.API;
                m_Device = std::move(*l_Device);

                break;
            }

            TR_CORE_ERROR("Could not create a {} device: {}. Falling back to {}", ToString(l_Specification.API), l_Device.GetError(), ToString(l_APIs[it_Index + 1]));
        }

        const RHI::DeviceInfo& l_Info = m_Device->GetInfo();
        TR_CORE_INFO("Graphics device: {} on {}", ToString(l_Info.API), l_Info.AdapterName);
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

    // Each layer may keep the application open, as an editor with unsaved changes does
    void Application::RequestClose()
    {
        for (const Scope<Layer>& it_Layer : m_LayerStack | std::views::reverse)
        {
            if (!it_Layer->OnCloseRequested())
            {
                return;
            }
        }

        Close();
    }

    void Application::Quit()
    {
        if (s_Instance == nullptr)
        {
            return;
        }

        if (s_Instance->m_QuitHandler)
        {
            s_Instance->m_QuitHandler();

            return;
        }

        TR_CORE_INFO("The game asked to quit, so '{}' closes", s_Instance->m_Specification.Name);
        s_Instance->Close();
    }

    void Application::SetQuitHandler(std::function<void()> handler)
    {
        m_QuitHandler = std::move(handler);
    }

    void Application::SubmitFrameJob(Job job)
    {
        JobSystem::Submit(std::move(job), &m_FrameJobs);
    }

    void Application::Run()
    {
        m_LastFrameTime = std::chrono::steady_clock::now();

        while (m_Running)
        {
            RunFrame(true);
        }

        JobSystem::Wait(m_FrameJobs);

        TR_CORE_INFO("'{}' ran for {} frames.", m_Specification.Name, m_FrameCount);
        TR_CORE_INFO("Frame allocator peak {} of {} per frame, {} overflowing frame(s)", Memory::FormatBytes(m_FrameAllocator.GetPeakUsed()), Memory::FormatBytes(m_FrameAllocator.GetCapacity()), m_FrameAllocator.GetOverflowFrameCount());
    }

    // The window also calls this while Win32's drag and resize loop holds the thread inside PollEvents, so those frames skip polling, and the timestep is taken after it
    void Application::RunFrame(bool pollEvents)
    {
        TR_PROFILE_FRAME();

        {
            TR_PROFILE_SCOPE("Application::WaitForFrameJobs");
            JobSystem::Wait(m_FrameJobs);
        }
        m_FrameAllocator.BeginFrame();

        if (pollEvents)
        {
            {
                TR_PROFILE_SCOPE("Window::PollEvents");
                m_Window->PollEvents();
            }

            TR_PROFILE_SCOPE("Input::PollGamepads");
            Input::PollGamepads([this](Event& event) { OnEvent(event); });
        }

        const auto l_Now = std::chrono::steady_clock::now();
        const Timestep l_Timestep(std::chrono::duration_cast<std::chrono::nanoseconds>(l_Now - m_LastFrameTime));
        m_LastFrameTime = l_Now;

        {
            TR_PROFILE_SCOPE("MainThread::ExecutePending");
            MainThread::ExecutePending();
        }

        AssetManager::Update();

        if (!m_Minimized)
        {
            TR_PROFILE_SCOPE("LayerStack::OnUpdate");
            for (const Scope<Layer>& it_Layer : m_LayerStack)
            {
                it_Layer->OnUpdate(l_Timestep);
            }

            m_Renderer->RenderFrame(m_LayerStack);
        }

        // Sound keeps its time while the window is minimized too
        if (m_AudioEngine)
        {
            m_AudioEngine->Update(l_Timestep);
        }

        Input::EndFrame();
        ++m_FrameCount;
        if (m_Specification.MaxFrames != 0 && m_FrameCount >= m_Specification.MaxFrames)
        {
            Close();
        }
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
        RequestClose();

        return true;
    }

    bool Application::OnWindowResize(WindowResizeEvent& event)
    {
        m_Minimized = event.GetWidth() == 0 || event.GetHeight() == 0;

        return false;
    }

    int Main(int argc, char** argv, CreateApplicationFunction createApplication)
    {
        Platform::Initialize();

        // The log file opens as the application starts, in its folder, and the lines before then wait for it. A profile capture goes in Trinity's
        const std::filesystem::path l_LogDirectory = GetLogDirectory({ argc, argv });
        const std::filesystem::path l_CaptureFile = GetLogFile(l_LogDirectory, { argc, argv }, ".trace.json");
        Log::Initialize();

        // The physics libraries are built for instructions some processors lack. Nothing built for them has run yet, so a processor without them stops here with the reason, rather than crashing once a scene starts running
        if (const std::string l_Missing = CpuFeatures::GetMissingPhysicsInstructions(); !l_Missing.empty())
        {
            const std::string l_Message = std::format("This processor does not support {}, which Trinity's physics is built for, so it cannot run here.", l_Missing);
            TR_CORE_CRITICAL("{}", l_Message);
            Log::OpenFile(GetLogFile(l_LogDirectory, { argc, argv }, ".log"));
            if (!ApplicationCommandLineArgs{ argc, argv }.HasOption("headless"))
            {
                Platform::ShowErrorMessage("Trinity", l_Message);
            }

            Log::Shutdown();
            Platform::Shutdown();

            return 1;
        }

        Memory::Initialize();
        LogHistory::Initialize();
        Profiler::Initialize(GetProfilerSpecification({ argc, argv }, l_CaptureFile));
        ConsoleVariables::Initialize({ argc, argv });
        MainThread::Initialize();
        FileSystem::Initialize();
        MountEngineDirectory();
        JobSystem::Initialize();
        SceneSerializer::Initialize();
        ScriptClassRegistry::Initialize();
        ScriptBackends::Initialize();
        NativeComponents::Initialize();
        AssetManager::Initialize();
        CollisionShapeLoaders::Register();
        PrefabLoaders::Register();
        AudioLoaders::Register();
        InputActionsRuntime::Initialize();

        TR_CORE_INFO("Trinity {} - {} {}", GetVersionString(), Platform::GetName(), c_ConfigurationName);

        Scope<Application> l_Application{ createApplication({ argc, argv }) };
        TR_CORE_ASSERT(l_Application != nullptr, "CreateApplication returned null.");
        if (l_Application)
        {
            l_Application->Run();
        }

        l_Application.reset();

        InputActionsRuntime::Shutdown();
        AudioLoaders::Unregister();
        PrefabLoaders::Unregister();
        CollisionShapeLoaders::Unregister();
        AssetManager::Shutdown();
        NativeComponents::Shutdown();
        ScriptBackends::Shutdown();
        ScriptClassRegistry::Shutdown();
        SceneSerializer::Shutdown();
        JobSystem::Shutdown();
        FileSystem::Shutdown();
        MainThread::Shutdown();
        ConsoleVariables::Shutdown();
        Profiler::Shutdown();
        LogHistory::Shutdown();
        Memory::Shutdown();
        Log::Shutdown();
        Platform::Shutdown();

        return 0;
    }
}
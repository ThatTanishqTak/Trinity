#pragma once

#include "Trinity/Audio/AudioEngine.hpp"
#include "Trinity/Core/Base.hpp"
#include "Trinity/Core/FrameAllocator.hpp"
#include "Trinity/Core/JobSystem.hpp"
#include "Trinity/Core/LayerStack.hpp"
#include "Trinity/Core/Window.hpp"
#include "Trinity/Events/ApplicationEvent.hpp"
#include "Trinity/Renderer/GraphicsAPI.hpp"
#include "Trinity/Renderer/Renderer.hpp"
#include "Trinity/RHI/Device.hpp"
#include "Trinity/UI/GameUI.hpp"

#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Trinity
{
    struct TRINITY_API ApplicationCommandLineArgs
    {
        int Count = 0;
        char** Args = nullptr;

        [[nodiscard]] std::string_view operator[](int index) const;
        [[nodiscard]] bool HasOption(std::string_view name) const;
        [[nodiscard]] std::optional<std::string_view> GetOption(std::string_view name) const;
        [[nodiscard]] std::vector<std::string_view> GetAllOptions(std::string_view name) const;
    };

    struct ApplicationSpecification
    {
        std::string Name = "Trinity Application";
        // Saves and logs go under Company/Name in the user's folder, or under Trinity when empty
        std::string Company;
        WindowSpecification Window;
        // Called once /saves is mounted, before the window is made
        std::function<void(WindowSpecification& window)> ConfigureWindow;

        GraphicsAPI Graphics = GetDefaultGraphicsAPI();
        // Tried in order when no --d3d12 or --vulkan is given. Empty tries Graphics alone
        std::vector<GraphicsAPI> GraphicsOrder;

        std::uint64_t MaxFrames = 0;
        // Frames a second while the window is behind another or minimized. 0 leaves it to vsync
        float BackgroundFrameRate = 0.0f;
        std::size_t FrameAllocatorCapacity = 4 * 1024 * 1024;

        bool StartDotNet = true;
        bool StartAudio = true;
        bool StartGameUI = true;

        // Mounted at /saves unless --saves names another. Empty is the application's own folder
        std::filesystem::path SavesDirectory;

        ApplicationCommandLineArgs CommandLineArgs;
    };

    class Application;

    using CreateApplicationFunction = Application * (*)(ApplicationCommandLineArgs args);

    class TRINITY_API Application
    {
    public:
        explicit Application(ApplicationSpecification specification);
        virtual ~Application();

        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;

        template<std::derived_from<Layer> T, typename... Args>
        T& PushLayer(Args&&... args)
        {
            return static_cast<T&>(m_LayerStack.PushLayer(CreateScope<T>(std::forward<Args>(args)...)));
        }

        template<std::derived_from<Layer> T, typename... Args>
        T& PushOverlay(Args&&... args)
        {
            return static_cast<T&>(m_LayerStack.PushOverlay(CreateScope<T>(std::forward<Args>(args)...)));
        }

        void Close(int exitCode = 0);
        void RequestClose();
        // Closes once the frame ends, unless a quit handler is set. Main thread only
        static void Quit();
        // Runs in place of closing. Null closes again
        void SetQuitHandler(std::function<void()> handler);
        void SubmitFrameJob(Job job);

        // Also takes events from windows other than the main one, such as ImGui's viewports
        void OnEvent(Event& event);

        [[nodiscard]] Window& GetWindow() { return *m_Window; }
        [[nodiscard]] RHI::Device& GetDevice() { return *m_Device; }
        [[nodiscard]] Renderer& GetRenderer() { return *m_Renderer; }
        // Null when the application has no audio
        [[nodiscard]] AudioEngine* GetAudioEngine() { return m_AudioEngine.get(); }
        // Null when the application has no game UI
        [[nodiscard]] GameUI* GetGameUI() { return m_GameUI.get(); }
        [[nodiscard]] FrameAllocator& GetFrameAllocator() { return m_FrameAllocator; }
        [[nodiscard]] LayerStack& GetLayerStack() { return m_LayerStack; }
        [[nodiscard]] const ApplicationSpecification& GetSpecification() const { return m_Specification; }
        [[nodiscard]] std::uint64_t GetFrameCount() const { return m_FrameCount; }

        [[nodiscard]] static Application& Get();

    private:
        void Run();
        void RunFrame(bool pollEvents);
        void CreateDevice(std::span<const GraphicsAPI> fallbacks);
        bool OnWindowClose(WindowCloseEvent& event);
        bool OnWindowResize(WindowResizeEvent& event);

        ApplicationSpecification m_Specification;
        FrameAllocator m_FrameAllocator;
        JobCounter m_FrameJobs;
        Scope<Window> m_Window;
        Scope<RHI::Device> m_Device;
        Scope<Renderer> m_Renderer;
        Scope<AudioEngine> m_AudioEngine;
        Scope<GameUI> m_GameUI;
        LayerStack m_LayerStack;
        std::function<void()> m_QuitHandler;
        std::uint64_t m_FrameCount = 0;
        std::chrono::steady_clock::time_point m_LastFrameTime;
        int m_ExitCode = 0;
        bool m_Running = true;
        bool m_Minimized = false;

        static Application* s_Instance;

        friend TRINITY_API int Main(int argc, char** argv, CreateApplicationFunction createApplication);
    };

    // Defined by each executable, and handed to Main by EntryPoint.hpp
    Application* CreateApplication(ApplicationCommandLineArgs args);

    TRINITY_API int Main(int argc, char** argv, CreateApplicationFunction createApplication);
}
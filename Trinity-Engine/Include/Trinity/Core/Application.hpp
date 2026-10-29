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

#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
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
        // A game's company. Its saves and log go under it and the name in the user's folder, as Unity's do, and without one under Trinity, as Trinity's own programs' do
        std::string Company;
        WindowSpecification Window;

        GraphicsAPI Graphics = GetDefaultGraphicsAPI();
        // Tried in this order when no --d3d12 or --vulkan picks one, skipping any this build lacks, each in turn while its device cannot be made, as a game's export lists them. Empty tries Graphics alone
        std::vector<GraphicsAPI> GraphicsOrder;

        std::uint64_t MaxFrames = 0;
        std::size_t FrameAllocatorCapacity = 4 * 1024 * 1024;

        // .NET starts as the application does, for C# scripts, from Scripting beside the executable. An application with no scripts, such as Hub, never starts it
        bool StartDotNet = true;
        // Audio starts as the application does, on the system's default output, or silent with --headless, which opens no device. An application with no sound, such as Hub, never starts it
        bool StartAudio = true;

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

        void Close();
        void RequestClose();
        // How a game ends, as a script's Application.Quit asks: the application closes once the frame ends, unless a handler is set, as Forge sets one to stop playing instead. Main thread only
        static void Quit();
        // What Quit does in place of closing, or none to close again
        void SetQuitHandler(std::function<void()> handler);
        void SubmitFrameJob(Job job);

        // Also how events from windows other than the main one, such as ImGui's viewports, reach Input and the layers
        void OnEvent(Event& event);

        [[nodiscard]] Window& GetWindow() { return *m_Window; }
        [[nodiscard]] RHI::Device& GetDevice() { return *m_Device; }
        [[nodiscard]] Renderer& GetRenderer() { return *m_Renderer; }
        // Null when the application has no audio
        [[nodiscard]] AudioEngine* GetAudioEngine() { return m_AudioEngine.get(); }
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
        LayerStack m_LayerStack;
        std::function<void()> m_QuitHandler;
        std::uint64_t m_FrameCount = 0;
        std::chrono::steady_clock::time_point m_LastFrameTime;
        bool m_Running = true;
        bool m_Minimized = false;

        static Application* s_Instance;

        friend TRINITY_API int Main(int argc, char** argv, CreateApplicationFunction createApplication);
    };

    // Defined by each executable, never by the engine, and handed to Main by EntryPoint.hpp
    Application* CreateApplication(ApplicationCommandLineArgs args);

    TRINITY_API int Main(int argc, char** argv, CreateApplicationFunction createApplication);
}
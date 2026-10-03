#pragma once

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
#include <optional>
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
        WindowSpecification Window;

        GraphicsAPI Graphics = GetDefaultGraphicsAPI();

        std::uint64_t MaxFrames = 0;
        std::size_t FrameAllocatorCapacity = 4 * 1024 * 1024;

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
        void SubmitFrameJob(Job job);

        [[nodiscard]] Window& GetWindow() { return *m_Window; }
        [[nodiscard]] RHI::Device& GetDevice() { return *m_Device; }
        [[nodiscard]] Renderer& GetRenderer() { return *m_Renderer; }
        [[nodiscard]] FrameAllocator& GetFrameAllocator() { return m_FrameAllocator; }
        [[nodiscard]] const ApplicationSpecification& GetSpecification() const { return m_Specification; }
        [[nodiscard]] std::uint64_t GetFrameCount() const { return m_FrameCount; }

        [[nodiscard]] static Application& Get();

    private:
        void Run();
        void RunFrame(bool pollEvents);
        void CreateDevice();
        void OnEvent(Event& event);
        bool OnWindowClose(WindowCloseEvent& event);
        bool OnWindowResize(WindowResizeEvent& event);

        ApplicationSpecification m_Specification;
        FrameAllocator m_FrameAllocator;
        JobCounter m_FrameJobs;
        Scope<Window> m_Window;
        Scope<RHI::Device> m_Device;
        Scope<Renderer> m_Renderer;
        LayerStack m_LayerStack;
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
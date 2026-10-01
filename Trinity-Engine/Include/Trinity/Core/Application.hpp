#pragma once

#include "Trinity/Core/Base.hpp"
#include "Trinity/Core/LayerStack.hpp"
#include "Trinity/Core/Window.hpp"
#include "Trinity/Events/ApplicationEvent.hpp"
#include "Trinity/Renderer/GraphicsAPI.hpp"

#include <concepts>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Trinity
{
    struct ApplicationCommandLineArgs
    {
        int Count = 0;
        char** Args = nullptr;

        [[nodiscard]] std::string_view operator[](int index) const;

        [[nodiscard]] bool HasOption(std::string_view name) const;

        [[nodiscard]] std::optional<std::string_view> GetOption(std::string_view name) const;
    };

    struct ApplicationSpecification
    {
        std::string Name = "Trinity Application";
        WindowSpecification Window;

        GraphicsAPI Graphics = GetDefaultGraphicsAPI();

        std::uint64_t MaxFrames = 0;

        ApplicationCommandLineArgs CommandLineArgs;
    };

    class Application
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

        [[nodiscard]] Window& GetWindow() { return *m_Window; }
        [[nodiscard]] const ApplicationSpecification& GetSpecification() const { return m_Specification; }
        [[nodiscard]] std::uint64_t GetFrameCount() const { return m_FrameCount; }

        [[nodiscard]] static Application& Get();

    private:
        void Run();
        void OnEvent(Event& event);
        bool OnWindowClose(WindowCloseEvent& event);
        bool OnWindowResize(WindowResizeEvent& event);

        ApplicationSpecification m_Specification;
        Scope<Window> m_Window;
        LayerStack m_LayerStack;
        std::uint64_t m_FrameCount = 0;
        bool m_Running = true;
        bool m_Minimized = false;

        static Application* s_Instance;

        friend int Main(int argc, char** argv);
    };

    Application* CreateApplication(ApplicationCommandLineArgs args);

    int Main(int argc, char** argv);
}
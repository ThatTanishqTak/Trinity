#pragma once

#include <string>
#include <functional>

#include <GLFW/glfw3.h>
#include "Trinity/Events/Event.h"

namespace Trinity
{
    struct WindowSpecification
    {
        std::string Title{};
        unsigned int Width{};
        unsigned int Height{};
    };

    class Window
    {
    public:
        Window() = default;
        Window(const WindowSpecification& specification);
        ~Window() = default;

        bool Initialize();
        void Shutdown();

        bool ShouldWindowClose() const;
        void PollEvents();
        void SetEventCallback(const std::function<void(Event&)>& callback) { m_EventCallback = callback; }

        GLFWwindow* GetNativeWindow() const { return m_Window; }

    private:
        template<typename TEventConstructor>
        static void DispatchEvent(GLFWwindow* window, TEventConstructor&& constructor);

        WindowSpecification m_Specification;
        GLFWwindow* m_Window = nullptr;
        std::function<void(Event&)> m_EventCallback;
    };
}
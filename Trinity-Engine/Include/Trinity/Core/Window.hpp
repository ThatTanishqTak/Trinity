#pragma once

#include "Trinity/Core/Base.hpp"
#include "Trinity/Events/Event.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace Trinity
{
    struct WindowSpecification
    {
        std::string Title;
        std::uint32_t Width = 1600;
        std::uint32_t Height = 900;
        bool Resizable = true;
        bool Headless = false;
    };

    class Window
    {
    public:
        using EventCallback = std::function<void(Event&)>;

        virtual ~Window() = default;

        virtual void PollEvents() = 0;

        [[nodiscard]] virtual std::uint32_t GetWidth() const = 0;
        [[nodiscard]] virtual std::uint32_t GetHeight() const = 0;

        virtual void SetEventCallback(EventCallback callback) = 0;
        virtual void SetTitle(std::string_view title) = 0;

        [[nodiscard]] virtual void* GetNativeHandle() const = 0;

        [[nodiscard]] static Scope<Window> Create(const WindowSpecification& specification);
    };
}
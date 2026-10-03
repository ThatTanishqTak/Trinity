#pragma once

#include "Trinity/Core/Export.hpp"
#include "Trinity/Input/KeyCodes.hpp"
#include "Trinity/Input/MouseCodes.hpp"

namespace Trinity
{
    class Event;

    struct MousePosition
    {
        float X = 0.0f;
        float Y = 0.0f;
    };

    class TRINITY_API Input
    {
    public:
        [[nodiscard]] static bool IsKeyPressed(KeyCode key);
        [[nodiscard]] static bool IsMouseButtonPressed(MouseCode button);
        [[nodiscard]] static MousePosition GetMousePosition();

    private:
        friend class Application;

        static void OnEvent(const Event& event);
    };
}
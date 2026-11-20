#pragma once

#include <cstdint>

namespace Trinity
{
    // By where they sit on an Xbox controller, so A is the bottom face button whatever a pad prints
    enum class GamepadButton : std::uint8_t
    {
        TR_A = 0, TR_B, TR_X, TR_Y,
        TR_LEFT_SHOULDER, TR_RIGHT_SHOULDER,
        TR_VIEW, TR_MENU,
        TR_LEFT_STICK, TR_RIGHT_STICK,
        TR_DPAD_UP, TR_DPAD_DOWN, TR_DPAD_LEFT, TR_DPAD_RIGHT,

        Count
    };

    // Sticks from -1 to 1, right and up positive. Triggers from 0 to 1. Raw, with no deadzone
    enum class GamepadAxis : std::uint8_t
    {
        TR_LEFT_STICK_X = 0, TR_LEFT_STICK_Y,
        TR_RIGHT_STICK_X, TR_RIGHT_STICK_Y,
        TR_LEFT_TRIGGER, TR_RIGHT_TRIGGER,

        Count
    };
}
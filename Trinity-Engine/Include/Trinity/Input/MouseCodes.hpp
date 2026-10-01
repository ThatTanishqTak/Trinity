#pragma once

#include <cstdint>

namespace Trinity
{
    enum class MouseCode : std::uint8_t
    {
        TR_BUTTON_0 = 0, TR_BUTTON_1, TR_BUTTON_2, TR_BUTTON_3, TR_BUTTON_4, TR_BUTTON_5, TR_BUTTON_6, TR_BUTTON_7,

        TR_LEFT = TR_BUTTON_0,
        TR_RIGHT = TR_BUTTON_1,
        TR_MIDDLE = TR_BUTTON_2,

        Count = 8
    };
}
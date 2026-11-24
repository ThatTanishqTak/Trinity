#pragma once

#include "Trinity/Core/Export.hpp"

#include <array>
#include <cstdint>
#include <string_view>

struct ImGuiStyle;
struct ImVec4;

namespace Trinity
{
    // What a panel draws itself is coloured from here, so it follows the theme as ImGui's widgets do
    enum class ThemeColor : std::uint8_t
    {
        // A solid fill, its two states, and the text that goes on one
        Accent = 0,
        AccentHovered,
        AccentActive,
        AccentText,

        // The wash behind a selected row, which ImGuiCol_Header is as well
        Selection,
        // A collapsing header, kept apart from ImGuiCol_Header, which selected rows share
        PanelHeader,
        PanelHeaderHovered,

        Info,
        Success,
        Warning,
        Error,
        Critical,

        Prefab,

        AxisX,
        AxisY,
        AxisZ,

        // Behind text and tools drawn over a scene's image
        Overlay,

        Count
    };

    namespace Theme
    {
        struct Entry
        {
            std::string_view Key;
            const char* Name;
        };

        inline constexpr std::string_view c_Dark = "dark";
        inline constexpr std::string_view c_Light = "light";

        // What ui.theme takes and what a settings page calls each. An unknown key is drawn dark
        inline constexpr std::array<Entry, 2> c_Themes{ { { c_Dark, "Dark" },{ c_Light, "Light" } } };

        [[nodiscard]] TRINITY_API bool IsKnown(std::string_view key);

        // Writes the theme's colours and unscaled metrics into the style, and makes it the one read below
        TRINITY_API void Apply(ImGuiStyle& style, std::string_view key);

        [[nodiscard]] TRINITY_API bool IsLight();
        [[nodiscard]] TRINITY_API const ImVec4& GetColor(ThemeColor color);
        // Packed as ImGui's draw lists take it, with the colour's own alpha times this one
        [[nodiscard]] TRINITY_API std::uint32_t GetColorU32(ThemeColor color, float alpha = 1.0f);

        // Around a collapsing header, so it is drawn as a header and not as a selected row
        TRINITY_API void PushHeaderColors();
        TRINITY_API void PopHeaderColors();
    }
}
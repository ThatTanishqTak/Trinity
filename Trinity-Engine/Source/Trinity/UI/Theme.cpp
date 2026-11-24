#include "Trinity/UI/Theme.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <utility>

namespace Trinity
{
    namespace
    {
        constexpr ImVec4 Rgb(int red, int green, int blue, float alpha = 1.0f)
        {
            return ImVec4(static_cast<float>(red) / 255.0f, static_cast<float>(green) / 255.0f, static_cast<float>(blue) / 255.0f, alpha);
        }

        constexpr ImVec4 WithAlpha(const ImVec4& color, float alpha)
        {
            return ImVec4(color.x, color.y, color.z, alpha);
        }

        constexpr ImVec4 Mix(const ImVec4& from, const ImVec4& to, float amount)
        {
            return ImVec4(from.x + (to.x - from.x) * amount, from.y + (to.y - from.y) * amount, from.z + (to.z - from.z) * amount, from.w + (to.w - from.w) * amount);
        }

        // A theme's surfaces from the furthest back to the nearest, then what is drawn on them
        struct Palette
        {
            // Behind and between docked panels: title bars, tab bars, the menu bar and splitters
            ImVec4 Gap;
            ImVec4 Panel;
            ImVec4 Popup;
            // Where text is typed and values are dragged
            ImVec4 Field;
            ImVec4 FieldHovered;
            ImVec4 FieldActive;
            // Around fields, popups and floating windows
            ImVec4 Outline;
            ImVec4 Button;
            ImVec4 ButtonHovered;
            ImVec4 Grab;
            ImVec4 GrabHovered;
            ImVec4 GrabActive;
            ImVec4 Text;
            ImVec4 TextDim;
            // Between a table's rows, and over every other one
            ImVec4 RowLine;
            ImVec4 RowAlternate;
            // Over what a modal popup or the window switcher covers
            ImVec4 Dim;
            // In ThemeColor's order
            std::array<ImVec4, std::to_underlying(ThemeColor::Count)> Colors;
        };

        // Unreal's and Hazel's grey ramp, with Hazel's amber for an accent
        constexpr Palette c_DarkPalette
        {
            .Gap = Rgb(21, 21, 21),
            .Panel = Rgb(36, 36, 36),
            .Popup = Rgb(42, 42, 42),
            .Field = Rgb(15, 15, 15),
            .FieldHovered = Rgb(24, 24, 24),
            .FieldActive = Rgb(31, 31, 31),
            .Outline = Rgb(56, 56, 56),
            .Button = Rgb(56, 56, 56),
            .ButtonHovered = Rgb(71, 71, 71),
            .Grab = Rgb(71, 71, 71),
            .GrabHovered = Rgb(87, 87, 87),
            .GrabActive = Rgb(128, 128, 128),
            .Text = Rgb(192, 192, 192),
            .TextDim = Rgb(128, 128, 128),
            .RowLine = Rgb(44, 44, 44),
            .RowAlternate = Rgb(255, 255, 255, 0.025f),
            .Dim = Rgb(0, 0, 0, 0.5f),
            .Colors
            { {
                Rgb(236, 158, 36),
                Rgb(255, 200, 100),
                Rgb(208, 138, 28),
                Rgb(26, 26, 26),
                Rgb(236, 158, 36, 0.3f),
                Rgb(47, 47, 47),
                Rgb(56, 56, 56),
                Rgb(90, 173, 242),
                Rgb(115, 217, 115),
                Rgb(255, 204, 77),
                Rgb(255, 115, 102),
                Rgb(255, 51, 51),
                Rgb(115, 179, 255),
                Rgb(219, 62, 29),
                Rgb(154, 243, 72),
                Rgb(58, 122, 248),
                Rgb(21, 21, 21, 0.82f)
            } }
        };

        // The same steps turned over. Its amber is darker, as the dark theme's is too faint on white
        constexpr Palette c_LightPalette
        {
            .Gap = Rgb(196, 196, 196),
            .Panel = Rgb(240, 240, 240),
            .Popup = Rgb(248, 248, 248),
            .Field = Rgb(255, 255, 255),
            .FieldHovered = Rgb(250, 250, 250),
            .FieldActive = Rgb(244, 244, 244),
            .Outline = Rgb(180, 180, 180),
            .Button = Rgb(220, 220, 220),
            .ButtonHovered = Rgb(206, 206, 206),
            .Grab = Rgb(176, 176, 176),
            .GrabHovered = Rgb(154, 154, 154),
            .GrabActive = Rgb(128, 128, 128),
            .Text = Rgb(30, 30, 30),
            .TextDim = Rgb(106, 106, 106),
            .RowLine = Rgb(222, 222, 222),
            .RowAlternate = Rgb(0, 0, 0, 0.03f),
            .Dim = Rgb(0, 0, 0, 0.35f),
            .Colors
            { {
                Rgb(196, 122, 0),
                Rgb(217, 139, 10),
                Rgb(168, 104, 0),
                Rgb(26, 26, 26),
                Rgb(196, 122, 0, 0.3f),
                Rgb(220, 220, 220),
                Rgb(206, 206, 206),
                Rgb(26, 95, 180),
                Rgb(26, 128, 38),
                Rgb(166, 102, 0),
                Rgb(204, 38, 26),
                Rgb(166, 0, 0),
                Rgb(26, 89, 204),
                Rgb(219, 62, 29),
                Rgb(154, 243, 72),
                Rgb(58, 122, 248),
                Rgb(240, 240, 240, 0.86f)
            } }
        };

        // How far ImGuiCol_Header's two other states are from the selection's wash: hovering is fainter than selecting
        constexpr float c_HoveredWash = 0.12f;
        constexpr float c_ActiveWash = 0.4f;
        // How much of the accent a held or pressed button takes, with its text still readable on it
        constexpr float c_PressedAccent = 0.3f;

        const Palette* s_Palette = &c_DarkPalette;

        void ApplyMetrics(ImGuiStyle& style)
        {
            style.DisabledAlpha = 0.5f;
            style.WindowRounding = 4.0f;
            // The tab list a docked node's corner button opens is one click on the tab bar away
            style.WindowMenuButtonPosition = ImGuiDir_None;
            style.PopupRounding = 4.0f;
            style.FramePadding = ImVec2(6.0f, 4.0f);
            style.FrameRounding = 4.0f;
            // Drawn in ImGuiCol_Border, so a field has an outline and a button, whose fill it matches, has none
            style.FrameBorderSize = 1.0f;
            style.ItemSpacing = ImVec2(8.0f, 5.0f);
            style.ItemInnerSpacing = ImVec2(5.0f, 4.0f);
            style.CellPadding = ImVec2(5.0f, 3.0f);
            style.IndentSpacing = 16.0f;
            style.ScrollbarSize = 12.0f;
            style.ScrollbarRounding = 6.0f;
            style.GrabMinSize = 10.0f;
            style.GrabRounding = 3.0f;
            style.TabRounding = 4.0f;
            style.TabBarOverlineSize = 2.0f;
            style.MenuItemRounding = 3.0f;
            style.SelectableRounding = 3.0f;
            style.DragDropTargetRounding = 3.0f;
            style.SeparatorTextBorderSize = 1.0f;
        }

        void ApplyColors(ImGuiStyle& style, const Palette& palette)
        {
            const auto a_Color = [&palette](ThemeColor color) -> const ImVec4& { return palette.Colors[std::to_underlying(color)]; };
            const ImVec4& l_Accent = a_Color(ThemeColor::Accent);
            const ImVec4 l_Clear = WithAlpha(palette.Panel, 0.0f);
            ImVec4* l_Colors = style.Colors;

            l_Colors[ImGuiCol_Text] = palette.Text;
            l_Colors[ImGuiCol_TextDisabled] = palette.TextDim;
            l_Colors[ImGuiCol_WindowBg] = palette.Panel;
            l_Colors[ImGuiCol_ChildBg] = l_Clear;
            l_Colors[ImGuiCol_PopupBg] = palette.Popup;
            l_Colors[ImGuiCol_Border] = palette.Outline;
            l_Colors[ImGuiCol_BorderShadow] = l_Clear;
            l_Colors[ImGuiCol_FrameBg] = palette.Field;
            l_Colors[ImGuiCol_FrameBgHovered] = palette.FieldHovered;
            l_Colors[ImGuiCol_FrameBgActive] = palette.FieldActive;
            l_Colors[ImGuiCol_TitleBg] = palette.Gap;
            l_Colors[ImGuiCol_TitleBgActive] = palette.Gap;
            l_Colors[ImGuiCol_TitleBgCollapsed] = palette.Gap;
            l_Colors[ImGuiCol_MenuBarBg] = palette.Gap;
            l_Colors[ImGuiCol_ScrollbarBg] = l_Clear;
            l_Colors[ImGuiCol_ScrollbarGrab] = palette.Grab;
            l_Colors[ImGuiCol_ScrollbarGrabHovered] = palette.GrabHovered;
            l_Colors[ImGuiCol_ScrollbarGrabActive] = palette.GrabActive;
            l_Colors[ImGuiCol_CheckMark] = l_Accent;
            l_Colors[ImGuiCol_CheckboxSelectedBg] = palette.Field;
            l_Colors[ImGuiCol_SliderGrab] = palette.GrabHovered;
            l_Colors[ImGuiCol_SliderGrabActive] = l_Accent;
            l_Colors[ImGuiCol_Button] = palette.Button;
            l_Colors[ImGuiCol_ButtonHovered] = palette.ButtonHovered;
            // Also what a toggle that is on is drawn in, so it has to stand apart from a button at rest
            l_Colors[ImGuiCol_ButtonActive] = Mix(palette.Button, l_Accent, c_PressedAccent);
            l_Colors[ImGuiCol_Header] = a_Color(ThemeColor::Selection);
            l_Colors[ImGuiCol_HeaderHovered] = WithAlpha(l_Accent, c_HoveredWash);
            l_Colors[ImGuiCol_HeaderActive] = WithAlpha(l_Accent, c_ActiveWash);
            l_Colors[ImGuiCol_Separator] = palette.Gap;
            l_Colors[ImGuiCol_SeparatorHovered] = WithAlpha(l_Accent, 0.6f);
            l_Colors[ImGuiCol_SeparatorActive] = l_Accent;
            l_Colors[ImGuiCol_ResizeGrip] = WithAlpha(palette.Grab, 0.4f);
            l_Colors[ImGuiCol_ResizeGripHovered] = WithAlpha(l_Accent, 0.6f);
            l_Colors[ImGuiCol_ResizeGripActive] = l_Accent;
            l_Colors[ImGuiCol_InputTextCursor] = palette.Text;
            l_Colors[ImGuiCol_TabHovered] = a_Color(ThemeColor::PanelHeader);
            // A tab at rest is the bar it sits on, and the selected one is the panel below it
            l_Colors[ImGuiCol_Tab] = palette.Gap;
            l_Colors[ImGuiCol_TabSelected] = palette.Panel;
            l_Colors[ImGuiCol_TabSelectedOverline] = l_Accent;
            l_Colors[ImGuiCol_TabDimmed] = palette.Gap;
            l_Colors[ImGuiCol_TabDimmedSelected] = palette.Panel;
            l_Colors[ImGuiCol_TabDimmedSelectedOverline] = l_Clear;
            l_Colors[ImGuiCol_DockingPreview] = WithAlpha(l_Accent, 0.5f);
            l_Colors[ImGuiCol_DockingEmptyBg] = palette.Gap;
            l_Colors[ImGuiCol_PlotLines] = palette.TextDim;
            l_Colors[ImGuiCol_PlotLinesHovered] = a_Color(ThemeColor::AccentHovered);
            // Progress bars are drawn in this one
            l_Colors[ImGuiCol_PlotHistogram] = l_Accent;
            l_Colors[ImGuiCol_PlotHistogramHovered] = a_Color(ThemeColor::AccentHovered);
            l_Colors[ImGuiCol_TableHeaderBg] = a_Color(ThemeColor::PanelHeader);
            l_Colors[ImGuiCol_TableBorderStrong] = palette.Gap;
            l_Colors[ImGuiCol_TableBorderLight] = palette.RowLine;
            l_Colors[ImGuiCol_TableRowBg] = l_Clear;
            l_Colors[ImGuiCol_TableRowBgAlt] = palette.RowAlternate;
            l_Colors[ImGuiCol_TextLink] = a_Color(ThemeColor::Info);
            l_Colors[ImGuiCol_TextSelectedBg] = WithAlpha(l_Accent, 0.35f);
            l_Colors[ImGuiCol_TreeLines] = palette.Outline;
            l_Colors[ImGuiCol_DragDropTarget] = l_Accent;
            l_Colors[ImGuiCol_DragDropTargetBg] = WithAlpha(l_Accent, 0.1f);
            l_Colors[ImGuiCol_UnsavedMarker] = palette.Text;
            l_Colors[ImGuiCol_NavCursor] = l_Accent;
            l_Colors[ImGuiCol_NavWindowingHighlight] = WithAlpha(palette.Text, 0.7f);
            l_Colors[ImGuiCol_NavWindowingDimBg] = palette.Dim;
            l_Colors[ImGuiCol_ModalWindowDimBg] = palette.Dim;
        }
    }

    namespace Theme
    {
        bool IsKnown(std::string_view key)
        {
            return std::ranges::find(c_Themes, key, &Entry::Key) != c_Themes.end();
        }

        void Apply(ImGuiStyle& style, std::string_view key)
        {
            s_Palette = key == c_Light ? &c_LightPalette : &c_DarkPalette;
            ApplyMetrics(style);
            ApplyColors(style, *s_Palette);
        }

        bool IsLight()
        {
            return s_Palette == &c_LightPalette;
        }

        const ImVec4& GetColor(ThemeColor color)
        {
            return s_Palette->Colors[std::min(static_cast<std::size_t>(color), s_Palette->Colors.size() - 1)];
        }

        std::uint32_t GetColorU32(ThemeColor color, float alpha)
        {
            const ImVec4& l_Color = GetColor(color);

            return ImGui::ColorConvertFloat4ToU32(ImVec4(l_Color.x, l_Color.y, l_Color.z, l_Color.w * alpha));
        }

        void PushHeaderColors()
        {
            ImGui::PushStyleColor(ImGuiCol_Header, GetColor(ThemeColor::PanelHeader));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, GetColor(ThemeColor::PanelHeaderHovered));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, GetColor(ThemeColor::PanelHeaderHovered));
        }

        void PopHeaderColors()
        {
            ImGui::PopStyleColor(3);
        }
    }
}
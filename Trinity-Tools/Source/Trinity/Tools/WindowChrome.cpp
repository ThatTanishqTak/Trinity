#include "Trinity/Tools/WindowChrome.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace
{
    // Above and below a bar's text, which makes the bar as tall as Windows' own title bar is beside text this size
    constexpr float c_PaddingInFonts = 0.65f;
    // Each of the window's buttons, as wide as Windows makes its own
    constexpr float c_ButtonWidthInFonts = 3.3f;
    // Half the side of the square a button's sign is drawn in
    constexpr float c_SignInFonts = 0.36f;
    // Windows' own close button's red, and its darker red while pressed
    constexpr ImU32 c_CloseHovered = IM_COL32(196, 43, 28, 255);
    constexpr ImU32 c_CloseActive = IM_COL32(160, 35, 22, 255);

    enum class Sign : std::uint8_t
    {
        Minimize,
        Maximize,
        Restore,
        Close
    };

    // Drawn in lines on whole pixels, so each is as crisp as the system's at any scale
    void DrawSign(ImDrawList& drawList, Sign sign, const ImVec2& centre, ImU32 color)
    {
        const float l_Half = std::round(ImGui::GetFontSize() * c_SignInFonts);
        const float l_Thickness = std::max(1.0f, std::round(ImGui::GetFontSize() / 14.0f));
        const ImVec2 l_Centre(std::floor(centre.x) + 0.5f, std::floor(centre.y) + 0.5f);
        const ImVec2 l_Min(l_Centre.x - l_Half, l_Centre.y - l_Half);
        const ImVec2 l_Max(l_Centre.x + l_Half, l_Centre.y + l_Half);
        switch (sign)
        {
            case Sign::Minimize:
            {
                drawList.AddLine(ImVec2(l_Min.x, l_Centre.y), ImVec2(l_Max.x, l_Centre.y), color, l_Thickness);

                break;
            }
            case Sign::Maximize:
            {
                drawList.AddRect(l_Min, l_Max, color, 0.0f, l_Thickness);

                break;
            }
            case Sign::Restore:
            {
                // One window in front of another: the one behind shows its top and right edges only
                const float l_Step = std::max(2.0f, std::round(l_Half * 0.4f));
                drawList.AddRect(ImVec2(l_Min.x, l_Min.y + l_Step), ImVec2(l_Max.x - l_Step, l_Max.y), color, 0.0f, l_Thickness);
                const std::array<ImVec2, 4> l_Behind{ ImVec2(l_Min.x + l_Step, l_Min.y + l_Step), ImVec2(l_Min.x + l_Step, l_Min.y), ImVec2(l_Max.x, l_Min.y), ImVec2(l_Max.x, l_Max.y - l_Step) };
                drawList.AddPolyline(l_Behind.data(), static_cast<int>(l_Behind.size()), color, l_Thickness);
                drawList.AddLine(l_Behind[3], ImVec2(l_Max.x - l_Step, l_Max.y - l_Step), color, l_Thickness);

                break;
            }
            case Sign::Close:
            {
                drawList.AddLine(l_Min, l_Max, color, l_Thickness);
                drawList.AddLine(ImVec2(l_Min.x, l_Max.y), ImVec2(l_Max.x, l_Min.y), color, l_Thickness);

                break;
            }
        }
    }

    // One of the window's buttons, as tall as the bar. Lit under the mouse as Windows lights its own, the close button in red
    bool DrawButton(const char* id, Sign sign, const ImRect& area, const char* tooltip)
    {
        ImGui::SetCursorScreenPos(area.Min);
        const bool l_Pressed = ImGui::InvisibleButton(id, area.GetSize());
        const bool l_Hovered = ImGui::IsItemHovered();
        const bool l_Active = ImGui::IsItemActive();
        ImDrawList& l_DrawList = *ImGui::GetWindowDrawList();
        if (l_Hovered || l_Active)
        {
            const ImU32 l_Lit = sign == Sign::Close ? (l_Active ? c_CloseActive : c_CloseHovered) : ImGui::GetColorU32(l_Active ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered);
            l_DrawList.AddRectFilled(area.Min, area.Max, l_Lit);
        }

        DrawSign(l_DrawList, sign, area.GetCenter(), sign == Sign::Close && (l_Hovered || l_Active) ? IM_COL32_WHITE : ImGui::GetColorU32(ImGuiCol_Text));
        ImGui::SetItemTooltip("%s", tooltip);

        return l_Pressed;
    }
}


namespace Trinity::Tools
{
    void WindowChrome::Update(Window& window, bool wanted)
    {
        if (wanted != m_Wanted)
        {
            m_Wanted = wanted;
            m_Active = window.SetCustomTitleBar(wanted);
        }

        switch (std::exchange(m_Pending, Action::None))
        {
            case Action::Minimize:
            {
                window.Minimize();

                break;
            }
            case Action::ToggleMaximized:
            {
                window.SetMaximized(!window.IsMaximized());

                break;
            }
            case Action::Drag:
            {
                window.BeginDrag();

                break;
            }
            case Action::None:
            {
                break;
            }
        }
    }

    float WindowChrome::GetPadding()
    {
        return std::round(ImGui::GetFontSize() * c_PaddingInFonts);
    }

    float WindowChrome::GetHeight()
    {
        return ImGui::GetFontSize() + GetPadding() * 2.0f;
    }

    float WindowChrome::GetButtonsWidth()
    {
        return std::round(ImGui::GetFontSize() * c_ButtonWidthInFonts) * 3.0f;
    }

    void WindowChrome::DrawMark(ImDrawList& drawList, const ImVec2& centre, float side)
    {
        const float l_Rise = side * 0.866f;
        drawList.AddTriangleFilled(ImVec2(centre.x, centre.y - l_Rise * 0.5f), ImVec2(centre.x + side * 0.5f, centre.y + l_Rise * 0.5f), ImVec2(centre.x - side * 0.5f, centre.y + l_Rise * 0.5f), Theme::GetColorU32(ThemeColor::Accent));
    }

    void WindowChrome::DrawButtons(const Window& window, const ImVec2& minimum, const ImVec2& maximum, const std::function<void()>& requestClose)
    {
        const float l_Width = (maximum.x - minimum.x) / 3.0f;
        const auto a_Area = [&minimum, &maximum, l_Width](int index) { return ImRect(ImVec2(minimum.x + l_Width * static_cast<float>(index), minimum.y), ImVec2(minimum.x + l_Width * static_cast<float>(index + 1), maximum.y)); };
        const bool l_Maximized = window.IsMaximized();
        if (DrawButton("##Minimize", Sign::Minimize, a_Area(0), "Minimize"))
        {
            m_Pending = Action::Minimize;
        }

        if (DrawButton("##Maximize", l_Maximized ? Sign::Restore : Sign::Maximize, a_Area(1), l_Maximized ? "Restore Down" : "Maximize"))
        {
            m_Pending = Action::ToggleMaximized;
        }

        if (DrawButton("##Close", Sign::Close, a_Area(2), "Close") && requestClose)
        {
            requestClose();
        }
    }

    // As on the system's own title bar. With a menu or a popup open the bar is not under the mouse to ImGui, so a click there only closes it
    void WindowChrome::DragBy(bool empty)
    {
        if (empty && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            m_Pending = Action::ToggleMaximized;
            m_Pressed = false;
        }
        else if (empty && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            m_Pressed = true;
        }
        else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            m_Pressed = false;
        }

        if (m_Pressed && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            m_Pending = Action::Drag;
            m_Pressed = false;
        }
    }
}
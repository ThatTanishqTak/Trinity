#include "TitleBar.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace
{
    // Above and below the menus' text, which makes the bar as tall as Windows' own title bar is beside text this size
    constexpr float c_PaddingInFonts = 0.65f;
    // Each of the window's buttons, as wide as Windows makes its own
    constexpr float c_ButtonWidthInFonts = 3.3f;
    // Half the side of the square a button's sign is drawn in
    constexpr float c_SignInFonts = 0.36f;
    constexpr float c_MarkInFonts = 0.9f;
    constexpr float c_TitleAlpha = 0.7f;
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

void TitleBar::Update(Trinity::Window& window, bool wanted)
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

// The padding is the bar's height and the line its menus' names sit on, so it is put back once the bar has begun and what the menus hold is as tall as ever
bool TitleBar::Begin()
{
    if (!m_Active)
    {
        return ImGui::BeginMainMenuBar();
    }

    const float l_Font = ImGui::GetFontSize();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, std::round(l_Font * c_PaddingInFonts)));
    const bool l_Open = ImGui::BeginMainMenuBar();
    ImGui::PopStyleVar();
    if (!l_Open)
    {
        return false;
    }

    // Trinity's mark: three sides, in the accent, level with the middle of the bar
    const ImGuiWindow* l_Bar = ImGui::GetCurrentWindow();
    const float l_Side = std::round(l_Font * c_MarkInFonts);
    const float l_Rise = l_Side * 0.866f;
    const ImVec2 l_Middle(ImGui::GetCursorScreenPos().x + l_Side * 0.5f, l_Bar->Pos.y + l_Bar->Size.y * 0.5f);
    ImGui::GetWindowDrawList()->AddTriangleFilled(ImVec2(l_Middle.x, l_Middle.y - l_Rise * 0.5f), ImVec2(l_Middle.x + l_Side * 0.5f, l_Middle.y + l_Rise * 0.5f), ImVec2(l_Middle.x - l_Side * 0.5f, l_Middle.y + l_Rise * 0.5f), Trinity::Theme::GetColorU32(Trinity::ThemeColor::Accent));
    ImGui::Dummy(ImVec2(l_Side, 0.0f));
    m_MenusLeft = ImGui::GetCursorScreenPos().x;

    return true;
}

void TitleBar::End(Trinity::Window& window, std::string_view title, const std::function<void()>& requestClose)
{
    if (!m_Active)
    {
        ImGui::EndMainMenuBar();

        return;
    }

    const ImGuiStyle& l_Style = ImGui::GetStyle();
    const ImGuiWindow* l_Window = ImGui::GetCurrentWindow();
    const ImRect l_Bar(l_Window->Pos, ImVec2(l_Window->Pos.x + l_Window->Size.x, l_Window->Pos.y + l_Window->Size.y));
    const float l_MenusRight = ImGui::GetCursorScreenPos().x;

    // Minimise, maximise or restore, and close, against the bar's right end as Windows has them
    const float l_ButtonWidth = std::round(ImGui::GetFontSize() * c_ButtonWidthInFonts);
    const ImRect l_Buttons(ImVec2(l_Bar.Max.x - l_ButtonWidth * 3.0f, l_Bar.Min.y), l_Bar.Max);
    const auto a_Area = [&l_Buttons, l_ButtonWidth](int index) { return ImRect(ImVec2(l_Buttons.Min.x + l_ButtonWidth * static_cast<float>(index), l_Buttons.Min.y), ImVec2(l_Buttons.Min.x + l_ButtonWidth * static_cast<float>(index + 1), l_Buttons.Max.y)); };
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

    // What is open, in the middle of the bar, or as near it as the menus and the buttons leave room for, and cut off where they do not
    const std::string l_Title(title);
    const float l_TitleWidth = ImGui::CalcTextSize(l_Title.c_str()).x;
    const float l_Left = l_MenusRight + l_Style.ItemSpacing.x * 2.0f;
    const float l_Right = l_Buttons.Min.x - l_Style.ItemSpacing.x * 2.0f;
    if (l_Right > l_Left)
    {
        const float l_At = std::clamp(std::round(l_Bar.GetCenter().x - l_TitleWidth * 0.5f), l_Left, std::max(l_Left, l_Right - l_TitleWidth));
        ImDrawList& l_DrawList = *ImGui::GetWindowDrawList();
        l_DrawList.PushClipRect(ImVec2(l_Left, l_Bar.Min.y), ImVec2(l_Right, l_Bar.Max.y), true);
        l_DrawList.AddText(ImVec2(l_At, std::round(l_Bar.GetCenter().y - ImGui::GetFontSize() * 0.5f)), ImGui::GetColorU32(ImGuiCol_Text, c_TitleAlpha), l_Title.c_str());
        l_DrawList.PopClipRect();
    }

    // The bar where it holds nothing to click is what the window is moved by, as the system's own is: a double-click maximises the window or puts it back, and a press that then moves hands the window to the system to drag. With a menu or a popup open the bar is not under the mouse to ImGui, so a click there only closes it
    const bool l_Empty = ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered();
    if (l_Empty && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
        m_Pending = Action::ToggleMaximized;
        m_Pressed = false;
    }
    else if (l_Empty && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
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

    // The menus and the buttons keep all but a sliver of the window's top edge, which elsewhere along the bar resizes it. In the window's own pixels, from its top left
    const ImGuiViewport* l_Viewport = ImGui::GetMainViewport();
    std::vector<Trinity::WindowRect> l_Controls;
    for (const ImRect& it_Area : { ImRect(ImVec2(m_MenusLeft, l_Bar.Min.y), ImVec2(l_MenusRight, l_Bar.Max.y)), l_Buttons })
    {
        l_Controls.push_back({ static_cast<std::int32_t>(std::floor(it_Area.Min.x - l_Viewport->Pos.x)), static_cast<std::int32_t>(std::floor(it_Area.Min.y - l_Viewport->Pos.y)), static_cast<std::int32_t>(std::ceil(it_Area.Max.x - l_Viewport->Pos.x)), static_cast<std::int32_t>(std::ceil(it_Area.Max.y - l_Viewport->Pos.y)) });
    }

    window.SetTitleBarControls(std::move(l_Controls));
    ImGui::EndMainMenuBar();
}
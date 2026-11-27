#include "TitleBar.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace
{
    constexpr float c_MarkInFonts = 0.9f;
    constexpr float c_TitleAlpha = 0.7f;
}

// The padding is the bar's height and the line its menus' names sit on, so it is put back once the bar has begun and what the menus hold is as tall as ever
bool TitleBar::Begin()
{
    if (!m_Chrome.IsActive())
    {
        return ImGui::BeginMainMenuBar();
    }

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, Trinity::Tools::WindowChrome::GetPadding()));
    const bool l_Open = ImGui::BeginMainMenuBar();
    ImGui::PopStyleVar();
    if (!l_Open)
    {
        return false;
    }

    // Trinity's mark, level with the middle of the bar
    const ImGuiWindow* l_Bar = ImGui::GetCurrentWindow();
    const float l_Side = std::round(ImGui::GetFontSize() * c_MarkInFonts);
    Trinity::Tools::WindowChrome::DrawMark(*ImGui::GetWindowDrawList(), ImVec2(ImGui::GetCursorScreenPos().x + l_Side * 0.5f, l_Bar->Pos.y + l_Bar->Size.y * 0.5f), l_Side);
    ImGui::Dummy(ImVec2(l_Side, 0.0f));
    m_MenusLeft = ImGui::GetCursorScreenPos().x;

    return true;
}

void TitleBar::End(Trinity::Window& window, std::string_view title, const std::function<void()>& requestClose)
{
    if (!m_Chrome.IsActive())
    {
        ImGui::EndMainMenuBar();

        return;
    }

    const ImGuiStyle& l_Style = ImGui::GetStyle();
    const ImGuiWindow* l_Window = ImGui::GetCurrentWindow();
    const ImRect l_Bar(l_Window->Pos, ImVec2(l_Window->Pos.x + l_Window->Size.x, l_Window->Pos.y + l_Window->Size.y));
    const float l_MenusRight = ImGui::GetCursorScreenPos().x;

    // Minimise, maximise or restore, and close, against the bar's right end as Windows has them
    const ImRect l_Buttons(ImVec2(l_Bar.Max.x - Trinity::Tools::WindowChrome::GetButtonsWidth(), l_Bar.Min.y), l_Bar.Max);
    m_Chrome.DrawButtons(window, l_Buttons.Min, l_Buttons.Max, requestClose);

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

    // The bar where it holds nothing to click is what the window is moved by, as the system's own is
    m_Chrome.DragBy(ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered());

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
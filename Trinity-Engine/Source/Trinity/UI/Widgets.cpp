#include "Trinity/UI/Widgets.hpp"

#include "Trinity/UI/Icons.hpp"
#include "Trinity/UI/Theme.hpp"

#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cmath>

namespace Trinity
{
    namespace
    {
        // The cross's arms, as a share of the field's height
        constexpr float c_CrossSize = 0.16f;
        // How much nearer the accent a toggle that is on goes while hovered, and how much of the accent its outline takes
        constexpr float c_ToggleHoveredAccent = 0.25f;
        constexpr float c_ToggleOutline = 0.7f;
        constexpr float c_SelectionEdgeInFonts = 0.15f;
    }

    namespace Widgets
    {
        bool SearchField(const char* id, const char* hint, std::string& text, ImGuiInputTextFlags flags, ImGuiInputTextCallback callback, void* userData)
        {
            const ImGuiStyle& l_Style = ImGui::GetStyle();
            const float l_Padding = l_Style.FramePadding.x;
            const float l_Inset = l_Padding + ImGui::CalcTextSize(Icons::c_Search).x + l_Style.ItemInnerSpacing.x;

            // The padding is the same on both sides, so the room made for the magnifier is the cross's too
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(l_Inset, l_Style.FramePadding.y));
            ImGui::SetNextItemAllowOverlap();
            const bool l_Changed = ImGui::InputTextWithHint(id, hint, &text, flags, callback, userData);
            ImGui::PopStyleVar();

            // Kept, so what the caller asks of the last item is asked of the field and not of the cross
            ImGuiContext& l_Context = *ImGui::GetCurrentContext();
            const ImGuiLastItemData l_Field = l_Context.LastItemData;
            const ImRect l_Frame = l_Field.Rect;
            ImDrawList& l_DrawList = *ImGui::GetWindowDrawList();
            l_DrawList.AddText(ImVec2(l_Frame.Min.x + l_Padding, l_Frame.Min.y + l_Style.FramePadding.y), ImGui::GetColorU32(ImGuiCol_TextDisabled), Icons::c_Search);

            bool l_Emptied = false;
            if (!text.empty())
            {
                // Added without ItemSize, so the cross takes no room in the layout and sits over the field's right end
                const ImRect l_Box(ImVec2(l_Frame.Max.x - l_Frame.GetHeight(), l_Frame.Min.y), l_Frame.Max);
                ImGui::PushID(id);
                const ImGuiID l_CrossID = ImGui::GetID("##Empty");
                ImGui::PopID();
                if (ImGui::ItemAdd(l_Box, l_CrossID))
                {
                    bool l_Hovered = false;
                    bool l_Held = false;
                    l_Emptied = ImGui::ButtonBehavior(l_Box, l_CrossID, &l_Hovered, &l_Held);
                    if (l_Hovered)
                    {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
                    }

                    const ImVec2 l_Centre = l_Box.GetCenter();
                    const float l_Arm = std::round(l_Frame.GetHeight() * c_CrossSize);
                    const ImU32 l_Color = ImGui::GetColorU32(l_Hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled);
                    const float l_Thickness = std::max(1.0f, std::round(l_Style.FrameBorderSize * 1.5f));
                    l_DrawList.AddLine(ImVec2(l_Centre.x - l_Arm, l_Centre.y - l_Arm), ImVec2(l_Centre.x + l_Arm, l_Centre.y + l_Arm), l_Color, l_Thickness);
                    l_DrawList.AddLine(ImVec2(l_Centre.x - l_Arm, l_Centre.y + l_Arm), ImVec2(l_Centre.x + l_Arm, l_Centre.y - l_Arm), l_Color, l_Thickness);
                }
            }

            l_Context.LastItemData = l_Field;
            if (l_Emptied)
            {
                // The press took the keyboard from the field, which is given back so the next search can be typed
                text.clear();
                ImGui::SetKeyboardFocusHere(-1);
            }

            return l_Changed || (l_Emptied && (flags & ImGuiInputTextFlags_EnterReturnsTrue) == 0);
        }

        bool ToggleButton(const char* label, bool on, const ImVec2& size)
        {
            if (on)
            {
                // ImGui's own hovered grey would read as the toggle going off under the mouse
                const ImVec4 l_Lit = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
                const ImVec4& l_Accent = Theme::GetColor(ThemeColor::Accent);
                ImGui::PushStyleColor(ImGuiCol_Button, l_Lit);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImLerp(l_Lit, l_Accent, c_ToggleHoveredAccent));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(l_Accent.x, l_Accent.y, l_Accent.z, c_ToggleOutline));
            }

            const bool l_Pressed = ImGui::Button(label, size);
            if (on)
            {
                ImGui::PopStyleColor(3);
            }

            return l_Pressed;
        }

        void DrawSelectionEdge()
        {
            const ImVec2 l_Min = ImGui::GetItemRectMin();
            const ImVec2 l_Max = ImGui::GetItemRectMax();
            const float l_Width = std::max(1.0f, std::round(ImGui::GetFontSize() * c_SelectionEdgeInFonts));
            ImGui::GetWindowDrawList()->AddRectFilled(l_Min, ImVec2(l_Min.x + l_Width, l_Max.y), Theme::GetColorU32(ThemeColor::Accent), ImGui::GetStyle().SelectableRounding, ImDrawFlags_RoundCornersLeft);
        }
    }
}
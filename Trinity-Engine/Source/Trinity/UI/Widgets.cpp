#include "Trinity/UI/Widgets.hpp"

#include "Trinity/UI/Icons.hpp"
#include "Trinity/UI/Theme.hpp"

#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

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
        // How far inside its square an overlay button's hovered fill is drawn, as a share of the item's height
        constexpr float c_OverlayInset = 0.1f;

        // An axis's letter and its colour, the fourth being no axis and so the dimmed text's
        constexpr std::array<const char*, 4> c_AxisLetters{ "X", "Y", "Z", "W" };
        constexpr std::array<ThemeColor, 3> c_AxisColors{ ThemeColor::AxisX, ThemeColor::AxisY, ThemeColor::AxisZ };
        constexpr float c_AxisLetterInFonts = 1.15f;
        // How much of its axis's colour a letter's cell takes over the field's, at rest and under the mouse, and how far its letter leans to the text's so it reads on that
        constexpr float c_AxisFill = 0.3f;
        constexpr float c_AxisFillHovered = 0.55f;
        constexpr float c_AxisLetterToText = 0.5f;

        // What ImGui's own DragScalarN does, with a letter's cell before each field
        bool DragAxes(const char* label, float* values, std::size_t count, float speed, float minimum, float maximum, const char* format, ImGuiSliderFlags flags, std::optional<float> reset)
        {
            if (ImGui::GetCurrentWindow()->SkipItems)
            {
                return false;
            }

            const ImGuiStyle& l_Style = ImGui::GetStyle();
            const float l_Height = ImGui::GetFrameHeight();
            const float l_Letter = std::round(ImGui::GetFontSize() * c_AxisLetterInFonts);
            ImDrawList& l_DrawList = *ImGui::GetWindowDrawList();

            bool l_Changed = false;
            ImGui::BeginGroup();
            ImGui::PushID(label);
            ImGui::PushMultiItemsWidths(static_cast<int>(count), ImGui::CalcItemWidth());
            for (std::size_t it_Axis = 0; it_Axis < count; ++it_Axis)
            {
                ImGui::PushID(static_cast<int>(it_Axis));
                if (it_Axis > 0)
                {
                    ImGui::SameLine(0.0f, l_Style.ItemInnerSpacing.x);
                }

                const float l_Width = ImGui::CalcItemWidth();
                const ImVec2 l_Min = ImGui::GetCursorScreenPos();
                bool l_Hovered = false;
                if (reset)
                {
                    if (ImGui::InvisibleButton("##Reset", ImVec2(l_Letter, l_Height)))
                    {
                        // Marked, so the group reads as edited to a caller that waits for the edit to end
                        values[it_Axis] = *reset;
                        l_Changed = true;
                        ImGui::MarkItemEdited(ImGui::GetItemID());
                    }

                    l_Hovered = ImGui::IsItemHovered();
                    ImGui::SetItemTooltip("Reset %s to %g", c_AxisLetters[it_Axis], *reset);
                }
                else
                {
                    ImGui::Dummy(ImVec2(l_Letter, l_Height));
                }

                // The cell runs on under the field's rounded corners, so the two read as one
                const ImVec4 l_Axis = it_Axis < c_AxisColors.size() ? Theme::GetColor(c_AxisColors[it_Axis]) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
                const ImVec4 l_Fill = ImLerp(ImGui::GetStyleColorVec4(ImGuiCol_FrameBg), l_Axis, l_Hovered ? c_AxisFillHovered : c_AxisFill);
                const ImVec4 l_Text = ImLerp(l_Axis, ImGui::GetStyleColorVec4(ImGuiCol_Text), c_AxisLetterToText);
                l_DrawList.AddRectFilled(l_Min, ImVec2(l_Min.x + l_Letter + l_Style.FrameRounding, l_Min.y + l_Height), ImGui::GetColorU32(l_Fill), l_Style.FrameRounding, ImDrawFlags_RoundCornersLeft);
                const ImVec2 l_Size = ImGui::CalcTextSize(c_AxisLetters[it_Axis]);
                l_DrawList.AddText(ImVec2(std::round(l_Min.x + (l_Letter - l_Size.x) * 0.5f), l_Min.y + l_Style.FramePadding.y), ImGui::GetColorU32(l_Text), c_AxisLetters[it_Axis]);

                ImGui::SameLine(0.0f, 0.0f);
                ImGui::SetNextItemWidth(std::max(1.0f, l_Width - l_Letter));
                l_Changed |= ImGui::DragScalar("", ImGuiDataType_Float, &values[it_Axis], speed, &minimum, &maximum, format, flags);
                ImGui::PopID();
                ImGui::PopItemWidth();
            }

            ImGui::PopID();

            // After the fields, as ImGui puts what a label shows before its ##
            const char* l_LabelEnd = ImGui::FindRenderedTextEnd(label);
            if (label != l_LabelEnd)
            {
                ImGui::SameLine(0.0f, l_Style.ItemInnerSpacing.x);
                ImGui::TextEx(label, l_LabelEnd);
            }

            ImGui::EndGroup();

            return l_Changed;
        }
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

        bool OverlayButton(const char* id, const char* icon, const char* tooltip)
        {
            // Added without ItemSize, as the search field's cross is, so it takes no room in the layout
            ImGuiContext& l_Context = *ImGui::GetCurrentContext();
            const ImGuiLastItemData l_Under = l_Context.LastItemData;
            const ImRect l_Frame = l_Under.Rect;
            const ImRect l_Box(ImVec2(l_Frame.Max.x - l_Frame.GetHeight(), l_Frame.Min.y), l_Frame.Max);
            const ImGuiID l_ID = ImGui::GetID(id);

            bool l_Pressed = false;
            if (ImGui::ItemAdd(l_Box, l_ID))
            {
                bool l_Hovered = false;
                bool l_Held = false;
                l_Pressed = ImGui::ButtonBehavior(l_Box, l_ID, &l_Hovered, &l_Held);

                ImDrawList& l_DrawList = *ImGui::GetWindowDrawList();
                if (l_Hovered || l_Held)
                {
                    const float l_Inset = std::round(l_Frame.GetHeight() * c_OverlayInset);
                    l_DrawList.AddRectFilled(ImVec2(l_Box.Min.x + l_Inset, l_Box.Min.y + l_Inset), ImVec2(l_Box.Max.x - l_Inset, l_Box.Max.y - l_Inset), ImGui::GetColorU32(l_Held ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered), ImGui::GetStyle().FrameRounding);
                }

                const ImVec2 l_Size = ImGui::CalcTextSize(icon);
                const ImVec2 l_Centre = l_Box.GetCenter();
                l_DrawList.AddText(ImVec2(std::round(l_Centre.x - l_Size.x * 0.5f), std::round(l_Centre.y - l_Size.y * 0.5f)), ImGui::GetColorU32(l_Hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled), icon);
                if (tooltip != nullptr)
                {
                    ImGui::SetItemTooltip("%s", tooltip);
                }
            }

            l_Context.LastItemData = l_Under;

            return l_Pressed;
        }

        bool DragFloat2(const char* label, float* values, float speed, float minimum, float maximum, const char* format, ImGuiSliderFlags flags, std::optional<float> reset)
        {
            return DragAxes(label, values, 2, speed, minimum, maximum, format, flags, reset);
        }

        bool DragFloat3(const char* label, float* values, float speed, float minimum, float maximum, const char* format, ImGuiSliderFlags flags, std::optional<float> reset)
        {
            return DragAxes(label, values, 3, speed, minimum, maximum, format, flags, reset);
        }

        bool DragFloat4(const char* label, float* values, float speed, float minimum, float maximum, const char* format, ImGuiSliderFlags flags, std::optional<float> reset)
        {
            return DragAxes(label, values, 4, speed, minimum, maximum, format, flags, reset);
        }
    }
}
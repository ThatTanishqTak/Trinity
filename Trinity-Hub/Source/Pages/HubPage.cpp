#include "Pages/HubPage.hpp"

#include <Trinity.hpp>

#include <imgui.h>

#include <algorithm>

namespace HubPages
{
    void DrawTitle(std::string_view title)
    {
        ImGui::PushFont(Trinity::ImGuiLayer::GetFont(Trinity::UIFont::Bold), ImGui::GetStyle().FontSizeBase * 1.6f);
        ImGui::TextUnformatted(title.data(), title.data() + title.size());
        ImGui::PopFont();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    }

    void DrawEmpty(std::string_view headline, std::string_view detail)
    {
        const ImVec2 l_Space = ImGui::GetContentRegionAvail();
        const float l_Wrap = std::min(l_Space.x, ImGui::GetFontSize() * 32.0f);
        ImGui::PushFont(Trinity::ImGuiLayer::GetFont(Trinity::UIFont::Bold), ImGui::GetStyle().FontSizeBase * 1.2f);
        const ImVec2 l_HeadlineSize = ImGui::CalcTextSize(headline.data(), headline.data() + headline.size());
        ImGui::PopFont();
        const ImVec2 l_DetailSize = ImGui::CalcTextSize(detail.data(), detail.data() + detail.size(), false, l_Wrap);

        const float l_Height = l_HeadlineSize.y + ImGui::GetStyle().ItemSpacing.y + l_DetailSize.y;
        const ImVec2 l_Start = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(l_Start.x + std::max(0.0f, (l_Space.x - l_HeadlineSize.x) * 0.5f), l_Start.y + std::max(0.0f, (l_Space.y - l_Height) * 0.4f)));
        ImGui::PushFont(Trinity::ImGuiLayer::GetFont(Trinity::UIFont::Bold), ImGui::GetStyle().FontSizeBase * 1.2f);
        ImGui::TextUnformatted(headline.data(), headline.data() + headline.size());
        ImGui::PopFont();

        ImGui::SetCursorPosX(l_Start.x + std::max(0.0f, (l_Space.x - l_DetailSize.x) * 0.5f));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + l_Wrap);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextUnformatted(detail.data(), detail.data() + detail.size());
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
    }

    void DrawSection(std::string_view title)
    {
        ImGui::Spacing();
        ImGui::PushFont(Trinity::ImGuiLayer::GetFont(Trinity::UIFont::Bold), 0.0f);
        ImGui::TextUnformatted(title.data(), title.data() + title.size());
        ImGui::PopFont();
        ImGui::Spacing();
    }

    void DrawLabel(std::string_view label)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label.data(), label.data() + label.size());
        ImGui::SameLine(ImGui::GetFontSize() * 10.0f);
    }

    void DrawWarning(std::string_view text)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_PlotHistogram));
        ImGui::TextWrapped("%.*s", static_cast<int>(text.size()), text.data());
        ImGui::PopStyleColor();
    }

    void DrawClipped(std::string_view text)
    {
        const float l_Available = ImGui::GetContentRegionAvail().x;
        ImGui::TextUnformatted(text.data(), text.data() + text.size());
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && ImGui::CalcTextSize(text.data(), text.data() + text.size()).x > l_Available)
        {
            ImGui::SetTooltip("%.*s", static_cast<int>(text.size()), text.data());
        }
    }
}
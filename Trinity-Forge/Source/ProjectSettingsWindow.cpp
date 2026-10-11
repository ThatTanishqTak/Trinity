#include "ProjectSettingsWindow.hpp"

#include "EditorSession.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

#include <cfloat>
#include <cstdint>
#include <format>
#include <string_view>
#include <utility>

namespace
{
    void TextLine(std::string_view text)
    {
        ImGui::TextUnformatted(text.data(), text.data() + text.size());
    }

    // Without the spaces around it, which would only look like a name
    std::string Trim(std::string_view text)
    {
        const std::size_t l_First = text.find_first_not_of(" \t");
        if (l_First == std::string_view::npos)
        {
            return {};
        }

        return std::string(text.substr(l_First, text.find_last_not_of(" \t") - l_First + 1));
    }
}

ProjectSettingsWindow::ProjectSettingsWindow(EditorSession& session) : m_Session(session), m_Title(std::format("{} Project Settings###ProjectSettings", Trinity::Icons::c_Gear))
{

}

void ProjectSettingsWindow::Open()
{
    m_Open = true;
    m_FocusRequested = true;
}

void ProjectSettingsWindow::Draw()
{
    if (!m_Open)
    {
        return;
    }

    if (std::exchange(m_FocusRequested, false))
    {
        ImGui::SetNextWindowFocus();
    }

    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 44.0f, ImGui::GetFontSize() * 40.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(m_Title.c_str(), &m_Open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking))
    {
        ImGui::End();

        return;
    }

    if (const Trinity::Project* l_Project = m_Session.GetProject())
    {
        DrawPhysics(*l_Project);
    }
    else
    {
        ImGui::TextDisabled("No project is open");
    }

    ImGui::End();
}

// The matrix has the layers down its rows and, last first, across its columns, so each pair has one box, in the triangle above the diagonal. A running scene keeps the layers it started with
void ProjectSettingsWindow::DrawPhysics(const Trinity::Project& project)
{
    Trinity::CollisionLayers l_Layers = project.GetCollisionLayers();
    bool l_Changed = false;

    ImGui::SeparatorText("Physics");
    ImGui::TextDisabled("Saved with the project as they change, and taken by a scene as it starts running");

    ImGui::SeparatorText("Collision Layers");
    for (std::uint32_t it_Layer = 0; it_Layer < Trinity::c_MaxCollisionLayers; ++it_Layer)
    {
        ImGui::PushID(static_cast<int>(it_Layer));
        ImGui::AlignTextToFramePadding();
        TextLine(std::format("{:>2}", it_Layer));
        ImGui::SameLine(ImGui::GetFontSize() * 2.5f);
        ImGui::SetNextItemWidth(-FLT_MIN);

        if (ImGui::GetActiveID() != ImGui::GetID("##Name"))
        {
            m_Names[it_Layer] = l_Layers.Names[it_Layer];
        }

        ImGui::InputTextWithHint("##Name", std::format("Layer {}", it_Layer).c_str(), &m_Names[it_Layer]);
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            l_Layers.Names[it_Layer] = Trim(m_Names[it_Layer]);
            l_Changed = true;
        }

        ImGui::PopID();
    }

    ImGui::SeparatorText("Which Layers Collide");
    constexpr int c_Columns = static_cast<int>(Trinity::c_MaxCollisionLayers) + 1;
    const ImGuiTableFlags l_Flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollX | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_HighlightHoveredColumn;
    if (ImGui::BeginTable("##CollisionMatrix", c_Columns, l_Flags))
    {
        ImGui::TableSetupScrollFreeze(1, 0);
        ImGui::TableSetupColumn("Layer", ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_NoReorder);
        for (std::uint32_t it_Column = 0; it_Column < Trinity::c_MaxCollisionLayers; ++it_Column)
        {
            ImGui::TableSetupColumn(l_Layers.GetName(Trinity::c_MaxCollisionLayers - 1 - it_Column).c_str(), ImGuiTableColumnFlags_AngledHeader | ImGuiTableColumnFlags_WidthFixed);
        }

        ImGui::TableAngledHeadersRow();
        for (std::uint32_t it_Row = 0; it_Row < Trinity::c_MaxCollisionLayers; ++it_Row)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            TextLine(l_Layers.GetName(it_Row));

            for (std::uint32_t it_Column = 0; it_Column < Trinity::c_MaxCollisionLayers; ++it_Column)
            {
                const std::uint32_t l_Other = Trinity::c_MaxCollisionLayers - 1 - it_Column;
                if (l_Other < it_Row)
                {
                    continue;
                }

                ImGui::TableSetColumnIndex(static_cast<int>(it_Column) + 1);
                ImGui::PushID(static_cast<int>(it_Row * Trinity::c_MaxCollisionLayers + l_Other));
                bool l_Collides = l_Layers.Collides(it_Row, l_Other);
                if (ImGui::Checkbox("##Collides", &l_Collides))
                {
                    l_Layers.SetCollides(it_Row, l_Other, l_Collides);
                    l_Changed = true;
                }

                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                {
                    ImGui::SetTooltip("%s", std::format("{} and {} {}", l_Layers.GetName(it_Row), l_Layers.GetName(l_Other), l_Collides ? "collide" : "pass through each other").c_str());
                }

                ImGui::PopID();
            }
        }

        ImGui::EndTable();
    }

    if (l_Changed)
    {
        m_Session.SetCollisionLayers(l_Layers);
    }
}
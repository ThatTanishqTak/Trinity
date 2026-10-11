#include "Panels/ProjectSettingsPanel.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <format>
#include <string_view>

namespace
{
    constexpr float c_LabelWidthInFonts = 8.0f;

    void TextLine(std::string_view text)
    {
        ImGui::TextUnformatted(text.data(), text.data() + text.size());
    }

    // A label on the left and the widget filling the rest of the row
    void Label(const char* label)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine(ImGui::GetFontSize() * c_LabelWidthInFonts);
        ImGui::SetNextItemWidth(-FLT_MIN);
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

ProjectSettingsPanel::ProjectSettingsPanel(EditorSession& session) : Panel("Project Settings", Trinity::Icons::c_Gear, DockSlot::Right), m_Session(session)
{

}

void ProjectSettingsPanel::OnImGuiRender()
{
    const Trinity::Project* l_Project = m_Session.GetProject();
    if (l_Project == nullptr)
    {
        ImGui::TextDisabled("Open or create a project to change its settings");

        return;
    }

    const bool l_Playing = m_Session.IsPlaying();
    ImGui::SeparatorText("Physics");
    ImGui::TextDisabled("%s", l_Playing ? "The scene playing keeps the settings it started with, so nothing here changes until it stops" : "Saved with the project as they change, and taken by a scene as it starts playing");

    ImGui::BeginDisabled(l_Playing);
    DrawWorlds(l_Project->GetPhysicsSettings());
    DrawLayerNames(l_Project->GetPhysicsSettings());
    DrawCollisionMatrix(l_Project->GetPhysicsSettings());
    ImGui::EndDisabled();

    // Nothing is being dragged or typed into, so the next change is a command of its own, whether or not the Properties panel, which does the same, is showing
    if (!ImGui::IsAnyItemActive())
    {
        m_Session.GetHistory().EndMerge();
    }
}

// Gravity in each world and the fixed steps a second both take. A drag is one command
void ProjectSettingsPanel::DrawWorlds(Trinity::PhysicsSettings settings)
{
    Label("Gravity 3D");
    if (ImGui::DragFloat3("##Gravity3D", &settings.Gravity3D.x, 0.05f, -1000.0f, 1000.0f, "%.2f m/s\xC2\xB2", ImGuiSliderFlags_AlwaysClamp))
    {
        m_Session.EditPhysicsSettings(settings, "3D gravity");
    }

    Label("Gravity 2D");
    if (ImGui::DragFloat2("##Gravity2D", &settings.Gravity2D.x, 0.05f, -1000.0f, 1000.0f, "%.2f m/s\xC2\xB2", ImGuiSliderFlags_AlwaysClamp))
    {
        m_Session.EditPhysicsSettings(settings, "2D gravity");
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        ImGui::SetTooltip("In the XY plane the 2D world lies in, apart from the 3D world's, so a 2D game can fall at its own rate");
    }

    Label("Step Rate");
    int l_Rate = static_cast<int>(settings.StepRate);
    if (ImGui::DragInt("##StepRate", &l_Rate, 0.5f, static_cast<int>(Trinity::SimulationClock::c_MinimumRate), static_cast<int>(Trinity::SimulationClock::c_MaximumRate), "%d steps a second", ImGuiSliderFlags_AlwaysClamp))
    {
        settings.StepRate = static_cast<std::uint32_t>(l_Rate);
        m_Session.EditPhysicsSettings(settings, "step rate");
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        ImGui::SetTooltip("%s", std::format("Each step {:.2f} ms. Both worlds step this often, whatever the frame rate, and at most {} times a frame", 1000.0 / std::max(settings.StepRate, 1u), Trinity::SimulationClock::c_MaxStepsPerFrame).c_str());
    }
}

// A name is one command, made as its field is left
void ProjectSettingsPanel::DrawLayerNames(Trinity::PhysicsSettings settings)
{
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
            m_Names[it_Layer] = settings.Layers.Names[it_Layer];
        }

        ImGui::InputTextWithHint("##Name", std::format("Layer {}", it_Layer).c_str(), &m_Names[it_Layer]);
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            Trinity::PhysicsSettings l_Named = settings;
            l_Named.Layers.Names[it_Layer] = Trim(m_Names[it_Layer]);
            m_Session.EditPhysicsSettings(l_Named, std::format("layer {} name", it_Layer));
        }

        ImGui::PopID();
    }
}

// The matrix has the layers down its rows and, last first, across its columns, so each pair has one box, in the triangle above the diagonal. Each box is one command
void ProjectSettingsPanel::DrawCollisionMatrix(Trinity::PhysicsSettings settings)
{
    const Trinity::CollisionLayers& l_Layers = settings.Layers;
    ImGui::SeparatorText("Which Layers Collide");
    constexpr int c_Columns = static_cast<int>(Trinity::c_MaxCollisionLayers) + 1;
    const ImGuiTableFlags l_Flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollX | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_HighlightHoveredColumn;
    if (!ImGui::BeginTable("##CollisionMatrix", c_Columns, l_Flags))
    {
        return;
    }

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
                Trinity::PhysicsSettings l_Toggled = settings;
                l_Toggled.Layers.SetCollides(it_Row, l_Other, l_Collides);
                m_Session.EditPhysicsSettings(l_Toggled, std::format("collision of {} and {}", l_Layers.GetName(it_Row), l_Layers.GetName(l_Other)));
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
#include "Trinity/trpch.h"

#include "ToolbarPanel.h"

#include "Trinity/BuildSystem/BuildSystem.h"

#include <imgui.h>
#include <thread>
#include <fstream>
#include <filesystem>

namespace {
    std::string Trim(const std::string& str)
    {
        const char* l_Whitespace = " \t\n\r\"";
        const auto l_Begin = str.find_first_not_of(l_Whitespace);
        if (l_Begin == std::string::npos)
        {
            return {};
        }
        const auto l_End = str.find_last_not_of(l_Whitespace);

        return str.substr(l_Begin, l_End - l_Begin + 1);
    }
}

void ToolbarPanel::StartBuild(const std::string& a_ConfigPath)
{
    m_IsBuilding = true;
    m_BuildStatus = "Building...";

    std::thread([this, a_ConfigPath]()
        {
            Trinity::BuildConfig l_Config{};
            std::ifstream l_File(a_ConfigPath);
            if (l_File)
            {
                std::string l_Line;
                while (std::getline(l_File, l_Line))
                {
                    if (l_Line.find("OutputDir") != std::string::npos)
                    {
                        auto l_Colon = l_Line.find(':');
                        if (l_Colon != std::string::npos)
                        {
                            std::string l_Value = Trim(l_Line.substr(l_Colon + 1));
                            if (!l_Value.empty())
                            {
                                l_Config.OutputDir = l_Value;
                            }
                        }
                    }
                }
            }

            Trinity::BuildSystem l_Builder;
            l_Builder.Build(l_Config);

            m_BuildStatus = "Build complete";
            m_IsBuilding = false;
        }).detach();
}

void ToolbarPanel::OnUIRender()
{
    ImGuiWindowFlags l_WindowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoDocking;

    ImGui::Begin("Toolbar", nullptr, l_WindowFlags);

    if (ImGui::Button("Build"))
    {
        m_FileDialogCurrentPath = std::filesystem::current_path();
        m_SelectedConfigPath.clear();
        ImGui::OpenPopup("Select Build Config");
    }

    ImGui::End();

    if (ImGui::BeginPopupModal("Select Build Config", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted(m_FileDialogCurrentPath.string().c_str());
        ImGui::Separator();

        if (m_FileDialogCurrentPath.has_parent_path())
        {
            if (ImGui::Selectable(".."))
            {
                m_FileDialogCurrentPath = m_FileDialogCurrentPath.parent_path();
            }
        }

        for (const auto& l_Entry : std::filesystem::directory_iterator(m_FileDialogCurrentPath))
        {
            const auto& l_Path = l_Entry.path();
            if (l_Entry.is_directory())
            {
                std::string l_Label = l_Path.filename().string() + "/";
                if (ImGui::Selectable(l_Label.c_str()))
                {
                    m_FileDialogCurrentPath = l_Path;
                }
            }
            else if (l_Path.extension() == ".yaml")
            {
                bool l_Selected = (m_SelectedConfigPath == l_Path.string());
                if (ImGui::Selectable(l_Path.filename().string().c_str(), l_Selected))
                {
                    m_SelectedConfigPath = l_Path.string();
                }
            }
        }

        ImGui::Separator();
        if (ImGui::Button("Select") && !m_SelectedConfigPath.empty())
        {
            StartBuild(m_SelectedConfigPath);
            m_ShowBuildPopup = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    if (m_ShowBuildPopup)
    {
        ImGui::OpenPopup("Build Progress");
    }

    if (ImGui::BeginPopupModal("Build Progress", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("%s", m_BuildStatus.c_str());

        if (!m_IsBuilding)
        {
            if (ImGui::Button("Close"))
            {
                ImGui::CloseCurrentPopup();
                m_ShowBuildPopup = false;
            }
        }

        ImGui::EndPopup();
    }
}
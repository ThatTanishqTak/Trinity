#include "Trinity/trpch.h"

#include "ToolbarPanel.h"

#include "Trinity/BuildSystem/BuildSystem.h"
#include "Trinity/ECS/SceneSerializer.h"

#include <imgui.h>
#include <thread>
#include <filesystem>

ToolbarPanel::ToolbarPanel(Trinity::Scene* p_Scene, Trinity::ResourceManager* p_ResourceManager) : m_Scene(p_Scene), m_ResourceManager(p_ResourceManager)
{

}

void ToolbarPanel::StartBuild(const std::string& a_ConfigPath)
{
    m_IsBuilding = true;
    m_BuildStatus = "Parsing config...";

    std::thread([this, a_ConfigPath]()
        {
            try
            {
                auto l_Config = Trinity::BuildSystem::ParseBuildConfig(a_ConfigPath);
                m_BuildStatus = "Building...";

                Trinity::BuildSystem l_Builder;
                l_Builder.Build(l_Config);

                m_BuildStatus = "Build complete";
            }
            catch (const std::exception& e)
            {
                m_BuildStatus = std::string("Build failed: ") + e.what();
            }

            m_IsBuilding = false;
        }).detach();
}

void ToolbarPanel::StartPackage(const std::filesystem::path& a_OutputDir)
{
    m_IsPackaging = true;
    m_PackageStatus = "Packaging...";

    std::thread([this, a_OutputDir]()
        {
            try
            {
                Trinity::BuildSystem::BuildPackage(a_OutputDir);
                m_PackageStatus = "Package complete";
            }
            catch (const std::exception& e)
            {
                m_PackageStatus = std::string("Package failed: ") + e.what();
            }

            m_IsPackaging = false;
        }).detach();
}

void ToolbarPanel::OnUIRender()
{
    ImGui::Begin("Toolbar", nullptr);

    if (ImGui::Button("Build"))
    {
        m_FileDialogCurrentPath = std::filesystem::current_path();
        m_SelectedConfigPath.clear();
        ImGui::OpenPopup("Select Build Config");
    }

    ImGui::SameLine();

    if (ImGui::Button("Package"))
    {
        m_FileDialogCurrentPath = std::filesystem::current_path();
        ImGui::OpenPopup("Select Package Directory");
    }

    ImGui::SameLine();

    if (ImGui::Button(m_IsPlaying ? "Stop" : "Play"))
    {
        Trinity::SceneSerializer l_Serializer(m_Scene, m_ResourceManager);
        if (!m_IsPlaying)
        {
            l_Serializer.Serialize("scene_play.yaml");
        }

        else
        {
            l_Serializer.Deserialize("scene_play.yaml");
        }
        m_IsPlaying = !m_IsPlaying;
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

    if (ImGui::BeginPopupModal("Select Package Directory", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
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
        }

        ImGui::Separator();
        if (ImGui::Button("Select"))
        {
            StartPackage(m_FileDialogCurrentPath);
            m_ShowPackagePopup = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    if (m_ShowPackagePopup)
    {
        ImGui::OpenPopup("Package Progress");
    }

    if (ImGui::BeginPopupModal("Package Progress", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("%s", m_PackageStatus.c_str());

        if (!m_IsPackaging)
        {
            if (ImGui::Button("Close"))
            {
                ImGui::CloseCurrentPopup();
                m_ShowPackagePopup = false;
            }
        }

        ImGui::EndPopup();
    }
}
#include "Trinity/trpch.h"

#include "ToolbarPanel.h"

#include "Trinity/BuildSystem/BuildSystem.h"
#include "Trinity/ECS/SceneSerializer.h"
#include "Trinity/Utilities/Utilities.h"

#include <imgui.h>
#include <thread>
#include <filesystem>

ToolbarPanel::ToolbarPanel(Trinity::Scene* scene, Trinity::AssetManager* assetManager) : m_Scene(scene), m_AssetManager(assetManager)
{

}

void ToolbarPanel::StartBuild(const std::string& configPath)
{
    m_IsBuilding = true;
    m_BuildStatus = "Parsing config...";

    std::thread([this, configPath]()
        {
            try
            {
                auto l_Config = Trinity::BuildSystem::ParseBuildConfig(configPath);
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

void ToolbarPanel::StartPackage(const std::filesystem::path& outputDir)
{
    m_IsPackaging = true;
    m_PackageStatus = "Packaging...";

    std::thread([this, outputDir]()
        {
            try
            {
                Trinity::BuildSystem::BuildPackage(outputDir);
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
        std::string l_Config = Trinity::Utilities::FileManagement::OpenFile("YAML Files (*.yaml)\0*.yaml\0");
        if (!l_Config.empty())
        {
            StartBuild(l_Config);
            m_ShowBuildPopup = true;
        }
    }

    ImGui::SameLine();

    if (ImGui::Button("Package"))
    {
        std::string l_OutputDir = Trinity::Utilities::FileManagement::OpenDirectory();
        if (!l_OutputDir.empty())
        {
            StartPackage(l_OutputDir);
            m_ShowPackagePopup = true;
        }
    }

    ImGui::SameLine();

    if (ImGui::Button(m_IsPlaying ? "Stop" : "Play"))
    {
        Trinity::SceneSerializer l_Serializer(m_Scene, m_AssetManager);
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
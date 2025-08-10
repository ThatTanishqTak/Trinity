#pragma once

#include "Trinity/UI/Panel.h"
#include "Trinity/ECS/Scene.h"
#include "Trinity/Core/ResourceManager.h"

#include <atomic>
#include <string>
#include <filesystem>

class ToolbarPanel : public Trinity::Panel
{
public:
    ToolbarPanel(Trinity::Scene* p_Scene, Trinity::ResourceManager* p_ResourceManager);
    void OnUIRender() override;

private:
    void StartBuild(const std::string& a_ConfigPath);
    void StartPackage(const std::filesystem::path& a_OutputDir);

    Trinity::Scene* m_Scene = nullptr;
    Trinity::ResourceManager* m_ResourceManager = nullptr;
    bool m_IsPlaying = false;

    std::atomic_bool m_IsBuilding = false;
    bool m_ShowBuildPopup = false;
    std::string m_BuildStatus;
    std::filesystem::path m_FileDialogCurrentPath;
    std::string m_SelectedConfigPath;

    std::atomic_bool m_IsPackaging = false;
    bool m_ShowPackagePopup = false;
    std::string m_PackageStatus;
};
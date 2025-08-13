#pragma once

#include "Trinity/UI/Panel.h"
#include "Trinity/ECS/Scene.h"
#include "Trinity/Core/AssetManager.h"

#include <atomic>
#include <string>
#include <filesystem>

class ToolbarPanel : public Trinity::Panel
{
public:
    ToolbarPanel(Trinity::Scene* p_Scene, Trinity::AssetManager* assetManager);
    void OnUIRender() override;

private:
    void StartBuild(const std::string& a_ConfigPath);
    void StartPackage(const std::filesystem::path& outputDir);

    Trinity::Scene* m_Scene = nullptr;
    Trinity::AssetManager* m_AssetManager = nullptr;
    bool m_IsPlaying = false;

    std::atomic_bool m_IsBuilding = false;
    bool m_ShowBuildPopup = false;
    std::string m_BuildStatus;

    std::atomic_bool m_IsPackaging = false;
    bool m_ShowPackagePopup = false;
    std::string m_PackageStatus;
};
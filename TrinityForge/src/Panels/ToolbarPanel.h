#pragma once

#include "Trinity/UI/Panel.h"

#include <atomic>
#include <string>
#include <filesystem>

class ToolbarPanel : public Trinity::Panel
{
public:
    void OnUIRender() override;

private:
    void StartBuild(const std::string& a_ConfigPath);

    std::atomic_bool m_IsBuilding = false;
    bool m_ShowBuildPopup = false;
    std::string m_BuildStatus;
    std::filesystem::path m_FileDialogCurrentPath;
    std::string m_SelectedConfigPath;
};
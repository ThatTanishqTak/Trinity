#pragma once

#include "HubPathPrompt.hpp"
#include "HubSettings.hpp"
#include "Pages/HubPage.hpp"

#include <filesystem>

// Hub's appearance and where it installs versions, which it keeps in Settings.yaml, the folder it keeps its lists in, and what Hub is
class SettingsPage final : public HubPage
{
public:
    SettingsPage(HubSettings& settings, HubPathPrompt& prompt, std::filesystem::path directory, std::filesystem::path defaultInstallsDirectory);

    [[nodiscard]] std::string_view GetName() const override { return "Settings"; }
    [[nodiscard]] const char* GetIcon() const override;
    void Draw() override;

private:
    void DrawAppearance();
    void DrawInstalls();
    void DrawFolders();
    void DrawAbout();
    void Change();

    HubSettings& m_Settings;
    HubPathPrompt& m_Prompt;
    std::filesystem::path m_Directory;
    std::filesystem::path m_DefaultInstallsDirectory;
};
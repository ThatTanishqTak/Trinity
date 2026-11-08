#pragma once

#include "HubPathPrompt.hpp"
#include "HubSettings.hpp"
#include "Pages/HubPage.hpp"

#include <filesystem>
#include <string>
#include <string_view>

// Hub's appearance, where it installs versions and where New Project makes projects, which it keeps in Settings.yaml, the folder it keeps its lists in, and what Hub is
class SettingsPage final : public HubPage
{
public:
    SettingsPage(HubSettings& settings, HubPathPrompt& prompt, std::filesystem::path directory, std::filesystem::path defaultInstallsDirectory, std::filesystem::path defaultProjectsDirectory);

    [[nodiscard]] std::string_view GetName() const override { return "Settings"; }
    [[nodiscard]] const char* GetIcon() const override;
    void Draw() override;

private:
    void DrawAppearance();
    // As Unity Hub's Installs and Projects locations: the folder a setting names now, with Change and, while it names one, Use Default for the folder Hub uses without it
    void DrawLocation(std::string_view section, std::string& setting, const std::filesystem::path& directory, const std::filesystem::path& defaultDirectory, std::string_view prompt, std::string_view note);
    void DrawFolders();
    void DrawAbout();
    void Change();

    HubSettings& m_Settings;
    HubPathPrompt& m_Prompt;
    std::filesystem::path m_Directory;
    std::filesystem::path m_DefaultInstallsDirectory;
    std::filesystem::path m_DefaultProjectsDirectory;
};
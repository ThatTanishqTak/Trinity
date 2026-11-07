#pragma once

#include "HubSettings.hpp"
#include "Pages/HubPage.hpp"

#include <filesystem>

// Hub's appearance, which it keeps in Settings.yaml, the folder it keeps its lists in, and what Hub is
class SettingsPage final : public HubPage
{
public:
    SettingsPage(HubSettings& settings, std::filesystem::path directory);

    [[nodiscard]] std::string_view GetName() const override { return "Settings"; }
    [[nodiscard]] const char* GetIcon() const override;
    void Draw() override;

private:
    void DrawAppearance();
    void DrawFolders();
    void DrawAbout();
    void Change();

    HubSettings& m_Settings;
    std::filesystem::path m_Directory;
};
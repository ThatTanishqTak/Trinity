#pragma once

#include "HubInstaller.hpp"
#include "HubInstalls.hpp"
#include "HubPathPrompt.hpp"
#include "HubReleases.hpp"
#include "HubSettings.hpp"
#include "Pages/HubPage.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// The engine versions Hub knows, newest first, from Installs.yaml: each with its number, commit, date, configurations, the platforms it exports to, its folder and its size. Locate lists a folder holding a manifest, Install from File unpacks a version's zip, and Remove takes a located version off the list, or deletes an installed one's files once asked. Beside them, in a tab of their own, Trinity's releases on GitHub, which Download installs
class InstallsPage final : public HubPage
{
public:
    InstallsPage(HubInstalls& installs, HubReleases& releases, HubSettings& settings, HubPathPrompt& prompt, std::filesystem::path defaultInstallsDirectory);

    [[nodiscard]] std::string_view GetName() const override { return "Installs"; }
    [[nodiscard]] const char* GetIcon() const override;
    void OnShow() override;
    void Update() override;
    void Draw() override;

    // What the page's buttons do once a path is chosen
    void Locate(const std::filesystem::path& directory);
    void InstallFromFile(const std::filesystem::path& zip);
    void Remove(const std::filesystem::path& directory);
    void DownloadRelease(const HubRelease& release);

private:
    void DrawToolbar();
    void DrawStatus();
    void DrawList();
    void DrawReleases();
    void DrawReleaseList(const std::vector<const HubRelease*>& releases);
    void DrawDeletePopup();
    void Report(std::string message, bool failed);
    // Installed or located here, and not missing
    [[nodiscard]] bool IsInstalled(const HubRelease& release) const;
    // Each installed version by the name its folder takes, so one installed again replaces it where it is
    [[nodiscard]] std::vector<std::pair<std::string, std::filesystem::path>> GetInstalledFolders() const;

    HubInstalls& m_Installs;
    HubReleases& m_Releases;
    HubSettings& m_Settings;
    HubPathPrompt& m_Prompt;
    std::filesystem::path m_DefaultInstallsDirectory;
    HubInstaller m_Installer;

    // What the last thing done came to, until the next is done or it is dismissed
    std::string m_Message;
    bool m_MessageFailed = false;

    std::optional<std::filesystem::path> m_ConfirmDelete;
    bool m_OpenDeletePopup = false;
    bool m_ShowPrereleases = false;
};
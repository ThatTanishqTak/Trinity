#pragma once

#include "HubInstalls.hpp"
#include "HubLauncher.hpp"
#include "HubPathPrompt.hpp"
#include "HubRequirements.hpp"
#include "HubSettings.hpp"
#include "Pages/HubPage.hpp"

#include <Trinity.hpp>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct ImTextureData;
struct ImVec2;

// Unity Hub's New project: one of the versions here, the newest at first, its templates as cards with their thumbnails and an Empty card after them, then the project's name, the folder it goes in, and its language, which must be picked and never changes, a language the version cannot build on this machine greyed with why. The folder the project is made in is shown before Create, which starts that version's Forge to make it, so the version's own code writes every file in its own formats, and a folder there already that is not empty is refused. Hub picks no platform, as the Build panel does that. Reached from the Projects page, whose place in the sidebar stays lit
class NewProjectPage final : public HubPage
{
public:
    // created is handed what Create started, or why it could not, and cancelled is told the page was left without making one
    NewProjectPage(const HubInstalls& installs, const HubSettings& settings, HubPathPrompt& prompt, HubLauncher& launcher, const HubRequirements& requirements, std::filesystem::path defaultProjectsDirectory, std::function<void(HubLauncher::Report report)> created, std::function<void()> cancelled);
    ~NewProjectPage() override;

    NewProjectPage(const NewProjectPage&) = delete;
    NewProjectPage& operator=(const NewProjectPage&) = delete;

    [[nodiscard]] std::string_view GetName() const override { return "New Project"; }
    [[nodiscard]] const char* GetIcon() const override;
    // Each time it is opened: the newest version, its 3D template, a name not taken in the location the settings give, and no language
    void OnShow() override;
    // Thumbnails ImGui has uploaded keep no pixels of their own, and those it has let go of are freed
    void Update() override;
    void Draw() override;

private:
    // One of the version's templates with its thumbnail once loaded, or the Empty card, with neither
    struct Card
    {
        std::optional<Trinity::ProjectTemplate> Template;
        ImTextureData* Thumbnail = nullptr;
    };

    void DrawVersion();
    void DrawCards();
    void DrawCard(std::size_t index, float width);
    void DrawDetails();
    void DrawSettings();
    // The two languages, each greyed with why when the version cannot build it here, and what this machine lacks for the one picked
    void DrawLanguage();
    // A card's thumbnail, scaled to fit and centred between the corners given, or its icon where it has none
    static void DrawPicture(const Card& card, const ImVec2& minimum, const ImVec2& maximum);

    // The project Create would make now, or none, with why
    [[nodiscard]] std::optional<HubLauncher::Creation> GetCreation(std::string& refusal) const;
    // Back to the Projects page once its Forge has started, or what stopped it shown here
    void Create();
    void Leave();

    // The chosen version's templates read again, each thumbnail before let go of
    void ReadTemplates();
    void ReleaseThumbnails();
    // The version chosen, or the newest one when it is gone or none was
    [[nodiscard]] const HubInstall* GetVersion() const;
    // Taken by none of the location's folders, as Unity Hub's My project, My project (1) and on
    [[nodiscard]] std::string MakeName() const;

    const HubInstalls& m_Installs;
    const HubSettings& m_Settings;
    HubPathPrompt& m_Prompt;
    HubLauncher& m_Launcher;
    const HubRequirements& m_Requirements;
    std::filesystem::path m_DefaultProjectsDirectory;
    std::function<void(HubLauncher::Report report)> m_Created;
    std::function<void()> m_Cancelled;

    std::filesystem::path m_Version;
    // The version the cards were read from, so they are read again once another is chosen
    std::optional<std::filesystem::path> m_ReadVersion;
    std::vector<Card> m_Cards;
    std::size_t m_Card = 0;
    std::string m_Name;
    std::filesystem::path m_Location;
    std::optional<Trinity::ScriptLanguage> m_Language;
    // Why the last Create started no Forge
    std::string m_Problem;

    // The textures ImGui draws the thumbnails from, which the page owns, and those let go of, kept until ImGui's renderer has destroyed them
    std::vector<std::unique_ptr<ImTextureData>> m_Thumbnails;
    std::vector<std::unique_ptr<ImTextureData>> m_Released;
};
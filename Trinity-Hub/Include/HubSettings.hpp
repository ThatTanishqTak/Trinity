#pragma once

#include <Trinity/UI/Theme.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// The person's choices, in Settings.yaml: theme, scale, folders, favorite projects and how they are shown
struct HubSettings
{
    static constexpr std::uint32_t c_FormatVersion = 1;
    static constexpr std::string_view c_Path = "/saves/Settings.yaml";

    // The sizes Forge's preferences offer over the monitor's scale
    static constexpr std::array<float, 10> c_ScalePresets{ 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.5f, 3.0f, 4.0f };

    std::string ThemeKey{ Trinity::Theme::c_Dark };
    float Scale = 1.0f;
    // Where versions are unpacked, as UTF-8. Empty for Hub's default. Installed versions stay put
    std::string InstallsDirectory;
    // Where New Project starts a project's location, as UTF-8. Empty for Trinity Projects in Documents
    std::string ProjectsDirectory;
    // The Projects page as a grid of pictures rather than a list
    bool ProjectsGrid = false;
    // The projects kept first on the Projects page, each by its .trproj as UTF-8
    std::vector<std::string> Favorites;

    [[nodiscard]] std::filesystem::path GetInstallsDirectory(const std::filesystem::path& defaultDirectory) const;
    [[nodiscard]] std::filesystem::path GetProjectsDirectory(const std::filesystem::path& defaultDirectory) const;
    [[nodiscard]] bool IsFavorite(const std::filesystem::path& file) const;
    // Changed here only, so the caller saves
    void SetFavorite(const std::filesystem::path& file, bool favorite);

    // The settings in /saves. Each one missing or unreadable keeps its default
    [[nodiscard]] static HubSettings Load();
    // Into /saves, telling why when it cannot be written
    void Save() const;
    // To the engine's UI, which draws the next frame with them
    void Apply() const;
};
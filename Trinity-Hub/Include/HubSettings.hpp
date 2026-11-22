#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

// The person's choices, in Settings.yaml: theme, scale, the installs folder and the projects folder
struct HubSettings
{
    static constexpr std::uint32_t c_FormatVersion = 1;
    static constexpr std::string_view c_Path = "/saves/Settings.yaml";

    struct Theme
    {
        std::string_view Key;
        const char* Name;
    };

    // ImGui's own three themes, and the sizes Forge's preferences offer over the monitor's scale
    static constexpr std::array<Theme, 3> c_Themes{ { { "dark", "Dark" },{ "light", "Light" },{ "classic", "Classic" } } };
    static constexpr std::array<float, 10> c_ScalePresets{ 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.5f, 3.0f, 4.0f };

    std::string ThemeKey{ c_Themes[0].Key };
    float Scale = 1.0f;
    // Where versions are unpacked, as UTF-8. Empty for Hub's default. Installed versions stay put
    std::string InstallsDirectory;
    // Where New Project starts a project's location, as UTF-8. Empty for Trinity Projects in Documents
    std::string ProjectsDirectory;

    [[nodiscard]] std::filesystem::path GetInstallsDirectory(const std::filesystem::path& defaultDirectory) const;
    [[nodiscard]] std::filesystem::path GetProjectsDirectory(const std::filesystem::path& defaultDirectory) const;

    // The settings in /saves. Each one missing or unreadable keeps its default
    [[nodiscard]] static HubSettings Load();
    // Into /saves, telling why when it cannot be written
    void Save() const;
    // To the engine's UI, which draws the next frame with them
    void Apply() const;
};
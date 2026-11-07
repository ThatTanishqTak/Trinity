#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

// What Hub keeps of the person's choices, in Settings.yaml in its folder, which /saves mounts: the theme and the scale Hub is drawn at, which go to the engine's UI through ui.theme and ui.scale, as Forge's preferences do, and the folder it installs versions in
struct HubSettings
{
    static constexpr std::uint32_t c_FormatVersion = 1;
    static constexpr std::string_view c_Path = "/saves/Settings.yaml";

    struct Theme
    {
        std::string_view Key;
        const char* Name;
    };

    // ImGui's own three, as Forge offers them, and the sizes Forge's preferences offer on top of the monitor's own scale
    static constexpr std::array<Theme, 3> c_Themes{ { { "dark", "Dark" },{ "light", "Light" },{ "classic", "Classic" } } };
    static constexpr std::array<float, 10> c_ScalePresets{ 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.5f, 3.0f, 4.0f };

    std::string ThemeKey{ c_Themes[0].Key };
    float Scale = 1.0f;
    // Where Install from File puts the versions it unpacks, each in a folder of its own name, as UTF-8. Empty for Hub's default, Versions beside Hub's own folder. A version installed already stays where it is when this changes
    std::string InstallsDirectory;

    [[nodiscard]] std::filesystem::path GetInstallsDirectory(const std::filesystem::path& defaultDirectory) const;

    // The settings in /saves, each one missing keeping its default, and each that cannot be read told once and kept at its default
    [[nodiscard]] static HubSettings Load();
    // Into /saves, telling why when it cannot be written
    void Save() const;
    // To the engine's UI, which draws the next frame with them
    void Apply() const;
};
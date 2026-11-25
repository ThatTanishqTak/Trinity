#pragma once

#include <Trinity.hpp>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Arrangements of the panels saved under a name for each person: which panels are open, and where each is docked or floats. Kept beside the preferences, a file a layout
namespace Layouts
{
    struct Layout
    {
        // The panels that were open, by the IDs imgui.ini knows them by
        std::vector<std::string> OpenPanels;
        // ImGui's own lines for its windows and its docking, as it saves them
        std::string Settings;
    };

    // By name, in the order of the alphabet
    [[nodiscard]] std::vector<std::string> List();
    // What was typed with nothing a file's name cannot hold, and no longer than a menu shows well. Empty where nothing is left
    [[nodiscard]] std::string MakeName(std::string_view typed);

    // The arrangement in ImGui's settings as they are now, over any layout of the name. False, with a warning logged, where it could not be written
    bool Save(std::string_view name, std::span<const std::string> openPanels, std::string_view settings);
    [[nodiscard]] std::optional<Layout> Read(std::string_view name);
    bool Remove(std::string_view name);

    // Only the lines for windows and docking, which are what an arrangement is
    [[nodiscard]] std::string KeepArrangement(std::string_view settings);
    // The settings as they are now with the layout's windows and docking in place of their own, so nothing else that imgui.ini keeps goes back to how it was as the layout was saved
    [[nodiscard]] std::string Merge(std::string_view current, std::string_view layout);
}
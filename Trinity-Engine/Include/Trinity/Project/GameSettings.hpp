#pragma once

#include "Trinity/Audio/AudioSettings.hpp"
#include "Trinity/Core/Expected.hpp"
#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Screen.hpp"
#include "Trinity/Core/UUID.hpp"
#include "Trinity/Core/Window.hpp"
#include "Trinity/Physics/PhysicsTypes.hpp"
#include "Trinity/Renderer/GraphicsAPI.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Trinity
{
    // What a game is, as Game.trgame in its pack holds it, which its player reads before anything else: its names, the scenes in its build, the first being the one it starts with, its C++ module, the graphics APIs it tries, the window it first opens with, and the project's physics, audio and input actions. Written as YAML, as a project's settings are
    struct GameSettings
    {
        static constexpr std::uint32_t c_FormatVersion = 1;
        static constexpr std::string_view c_FileName = "Game.trgame";

        // Its saves and log go under these, in the user's folder
        std::string Product;
        std::string Company;
        std::string Version;
        std::vector<UUID> Scenes;
        // The module a player with the shared engine loads from beside the pack, or empty for none. A Distribution player has it linked in
        std::string Module;
        // Tried in this order when the player is given no --d3d12 or --vulkan, skipping any it was built without
        std::vector<GraphicsAPI> GraphicsAPIs;
        // Until its player chooses otherwise, which DisplaySettings keeps
        WindowSettings Window;
        PhysicsSettings Physics;
        AudioSettings Audio;
        UUID InputActions;

        [[nodiscard]] bool operator==(const GameSettings&) const = default;
    };

    [[nodiscard]] TRINITY_API std::string WriteGameSettings(const GameSettings& settings);
    // Refused for text that is not YAML, of a newer format, without a product or a company, or with no scene. A scene, module or graphics API that cannot be one is left out, and reported
    [[nodiscard]] TRINITY_API Expected<GameSettings, std::string> ParseGameSettings(std::string_view text);
    // Where a player reads it: in its pack, which mounts at /cache
    [[nodiscard]] TRINITY_API std::string GetGameSettingsPath();

    // The window as its player left it, over the game's own Window, which the player keeps in Display.yaml in its saves: the window's settings, and the monitor it was on and where its client area was while windowed, so the next start opens there again
    struct DisplaySettings
    {
        static constexpr std::uint32_t c_FormatVersion = 1;
        static constexpr std::string_view c_FileName = "Display.yaml";

        WindowSettings Window;
        // By the name the system gives it, and empty for none known
        std::string Monitor;
        std::optional<WindowPosition> Position;

        [[nodiscard]] bool operator==(const DisplaySettings&) const = default;
    };

    [[nodiscard]] TRINITY_API std::string WriteDisplaySettings(const DisplaySettings& settings);
    // Over the game's window, each setting that is missing keeping the game's and each that cannot be read reported and keeping the game's. Refused for text that is not YAML or of a newer format
    [[nodiscard]] TRINITY_API Expected<DisplaySettings, std::string> ParseDisplaySettings(std::string_view text, const WindowSettings& game);
    // In the player's saves, at /saves
    [[nodiscard]] TRINITY_API std::string GetDisplaySettingsPath();
}
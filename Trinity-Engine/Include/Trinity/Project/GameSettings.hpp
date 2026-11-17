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
    // Game.trgame in the pack, which the player reads before anything else. YAML
    struct GameSettings
    {
        static constexpr std::uint32_t c_FormatVersion = 1;
        static constexpr std::string_view c_FileName = "Game.trgame";

        // Its saves and log go under these, in the user's folder
        std::string Product;
        std::string Company;
        std::string Version;
        std::vector<UUID> Scenes;
        // Loaded from beside the pack by a player with the shared engine. Distribution links it in
        std::string Module;
        // The C# assembly's name, compiled ahead of time into GameAssembly beside the pack
        std::string Scripts;
        // Tried in order when the player is given no --d3d12 or --vulkan
        std::vector<GraphicsAPI> GraphicsAPIs;
        // Until its player chooses otherwise, which DisplaySettings keeps
        WindowSettings Window;
        PhysicsSettings Physics;
        AudioSettings Audio;
        UUID InputActions;

        [[nodiscard]] bool operator==(const GameSettings&) const = default;
    };

    [[nodiscard]] TRINITY_API std::string WriteGameSettings(const GameSettings& settings);
    // Refused when not YAML, of a newer format, or without a product, a company or a scene
    [[nodiscard]] TRINITY_API Expected<GameSettings, std::string> ParseGameSettings(std::string_view text);
    // Where a player reads it: in its pack, which mounts at /cache
    [[nodiscard]] TRINITY_API std::string GetGameSettingsPath();
    // GameAssembly.dll, .so or .dylib, as Unity names its IL2CPP build's
    [[nodiscard]] TRINITY_API std::string_view GetScriptsLibraryName();

    // The window as its player left it, kept in Display.yaml in the saves
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
    // A missing or unreadable setting keeps the game's. Refused when not YAML or of a newer format
    [[nodiscard]] TRINITY_API Expected<DisplaySettings, std::string> ParseDisplaySettings(std::string_view text, const WindowSettings& game);
    // In the player's saves, at /saves
    [[nodiscard]] TRINITY_API std::string GetDisplaySettingsPath();
}
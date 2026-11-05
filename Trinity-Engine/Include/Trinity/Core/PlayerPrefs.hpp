#pragma once

#include "Trinity/Core/Export.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace Trinity
{
    // As Unity's PlayerPrefs: small values a game keeps on the player's machine from one run to the next, each an int, a float or a string under a key, held in memory under the Engine tag and written to their file on Save and as the application closes. A key holds one kind at a time, setting it as another replaces it, and reading it as another gives the default. The file is read as the values are first used, and one that cannot be read is logged and taken as empty, and left as it is until something is saved. A player keeps them in its saves, and Forge's Play in the project's Saved folder, so each project has its own. Main thread only
    class TRINITY_API PlayerPrefs
    {
    public:
        static constexpr std::string_view c_DefaultPath = "/saves/PlayerPrefs.yaml";

        PlayerPrefs() = delete;

        static void SetInt(std::string_view key, std::int32_t value);
        [[nodiscard]] static std::int32_t GetInt(std::string_view key, std::int32_t defaultValue = 0);
        static void SetFloat(std::string_view key, float value);
        [[nodiscard]] static float GetFloat(std::string_view key, float defaultValue = 0.0f);
        static void SetString(std::string_view key, std::string_view value);
        [[nodiscard]] static std::string GetString(std::string_view key, std::string_view defaultValue = {});
        // GetString's text where PlayerPrefs keeps it, or the default given, valid until the values next change, so it is read without a copy
        [[nodiscard]] static std::string_view GetStringView(std::string_view key, std::string_view defaultValue = {});
        [[nodiscard]] static bool HasKey(std::string_view key);
        static void DeleteKey(std::string_view key);
        static void DeleteAll();

        // Writes the file now when anything changed since it was read or last written. False, logged, when it cannot be written, and the values stay to be saved again
        static bool Save();
        // The file in the virtual file system the values are read from and saved to from now on, c_DefaultPath until set: what changed is saved to the last one first, and the new one is read as the values are next used
        static void SetPath(std::string_view path);
        // Saves what changed and lets every value go, with the path back at c_DefaultPath, as the application closes and Forge's Play stops, so nothing is held after and the next use reads the file again
        static void Close();
    };
}
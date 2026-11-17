#pragma once

#include "Trinity/Core/Export.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace Trinity
{
    // Read on first use, written on Save and as the application closes. Main thread only
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
        // Valid until the values next change
        [[nodiscard]] static std::string_view GetStringView(std::string_view key, std::string_view defaultValue = {});
        [[nodiscard]] static bool HasKey(std::string_view key);
        static void DeleteKey(std::string_view key);
        static void DeleteAll();

        // Writes only when something changed. False, logged, when the file cannot be written
        static bool Save();
        // Saves changes to the old path first. The new file is read on the next use
        static void SetPath(std::string_view path);
        // Saves what changed, drops every value and puts the path back to c_DefaultPath
        static void Close();
    };
}
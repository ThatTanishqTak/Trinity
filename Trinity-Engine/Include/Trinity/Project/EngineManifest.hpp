#pragma once

#include "Trinity/Core/Expected.hpp"
#include "Trinity/Core/Export.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Trinity
{
    // 0 for a file the version does not name
    struct EngineFormats
    {
        std::uint32_t Project = 0;
        std::uint32_t Scene = 0;
        std::uint32_t Prefab = 0;
        std::uint32_t InputActions = 0;
        std::uint32_t AudioMixer = 0;
        std::uint32_t SoundEvent = 0;
        std::uint32_t Meta = 0;
        std::uint32_t Pack = 0;

        [[nodiscard]] bool operator==(const EngineFormats&) const = default;
    };

    // Trinity.trversion at a version's root, written by the build and read by Hub and Forge. YAML
    struct TRINITY_API EngineManifest
    {
        static constexpr std::uint32_t c_FormatVersion = 1;
        static constexpr std::string_view c_FileName = "Trinity.trversion";
        // Programs are in <Configuration>-<System>-<Architecture>/Trinity
        static constexpr std::string_view c_ProgramFolder = "Trinity";

        std::string Version;
        // All 40 characters, or empty for a build outside a git checkout
        std::string Commit;
        bool Modified = false;
        // The commit's, in UTC, as 2026-11-06T12:53:00Z
        std::string Date;
        // As TRINITY_PLATFORM names it, such as Windows or Linux
        std::string Platform;
        // As CMake names the system, such as Windows, Linux or Darwin
        std::string System;
        std::string Architecture;
        // The platforms its players are for, by the names Platform takes
        std::vector<std::string> Exports;
        std::vector<std::string> Configurations;
        // As CMake names them. A C++ module's compiler must match in kind and major version
        std::string CompilerId;
        std::string CompilerVersion;
        // As global.json pins the SDK, or empty for a version without C#
        std::string DotNetSdk;
        std::string DotNetRollForward;
        EngineFormats Formats;
        std::vector<std::string> Templates;

        [[nodiscard]] bool HasConfiguration(std::string_view configuration) const;
        [[nodiscard]] std::filesystem::path GetConfigurationDirectory(const std::filesystem::path& root, std::string_view configuration) const;

        [[nodiscard]] bool operator==(const EngineManifest&) const = default;
    };

    [[nodiscard]] TRINITY_API std::string WriteEngineManifest(const EngineManifest& manifest);
    // Refused when not YAML, of a newer format, or naming no version, platform or configuration
    [[nodiscard]] TRINITY_API Expected<EngineManifest, std::string> ParseEngineManifest(std::string_view text, std::string_view name = EngineManifest::c_FileName);
    // Read from the disk rather than through a mount, so Hub can read any version
    [[nodiscard]] TRINITY_API Expected<EngineManifest, std::string> ReadEngineManifest(const std::filesystem::path& root);
}
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
    // The newest format of each file an engine version reads and writes, or 0 for one it does not name
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

    // What makes a folder an engine version, as Trinity.trversion at its root holds it, which Trinity's build writes beside its configurations' folders and Hub and Forge read: the version and the commit it was built from, whether that tree had changes and the commit's date, the platform it runs on with the system and architecture its folders are named for, the platforms it exports games to, the configurations it holds, the compiler a C++ module must be built with, the .NET SDK its C# needs, the formats it reads and writes, and its project templates. Written as YAML, as a project's settings are
    struct TRINITY_API EngineManifest
    {
        static constexpr std::uint32_t c_FormatVersion = 1;
        static constexpr std::string_view c_FileName = "Trinity.trversion";
        // Each configuration's programs are in <Configuration>-<System>-<Architecture>/Trinity, as Trinity's build lays them out
        static constexpr std::string_view c_ProgramFolder = "Trinity";

        std::string Version;
        // All 40 characters, or empty for a build outside a git checkout
        std::string Commit;
        bool Modified = false;
        // The commit's, in UTC, as 2026-11-06T12:53:00Z
        std::string Date;
        // As TRINITY_PLATFORM names it, such as Windows or Linux
        std::string Platform;
        // As CMake names the system, which each configuration's folder is named for, such as Windows, Linux or Darwin
        std::string System;
        std::string Architecture;
        // The platforms its players are for, by the names Platform takes
        std::vector<std::string> Exports;
        std::vector<std::string> Configurations;
        // As CMake names them, which a C++ module's compiler must match in kind and major version
        std::string CompilerId;
        std::string CompilerVersion;
        // As global.json pins the SDK, or empty for a version without C#
        std::string DotNetSdk;
        std::string DotNetRollForward;
        EngineFormats Formats;
        std::vector<std::string> Templates;

        [[nodiscard]] bool HasConfiguration(std::string_view configuration) const;
        // Where a configuration's programs are in a version whose folder is root
        [[nodiscard]] std::filesystem::path GetConfigurationDirectory(const std::filesystem::path& root, std::string_view configuration) const;

        [[nodiscard]] bool operator==(const EngineManifest&) const = default;
    };

    [[nodiscard]] TRINITY_API std::string WriteEngineManifest(const EngineManifest& manifest);
    // Refused for text that is not YAML or holds no map, of a newer format, or naming no version, no platform, system and architecture, or no configuration, in one message that name begins. Keys it does not know are ignored, and entries of a list that are not names are left out
    [[nodiscard]] TRINITY_API Expected<EngineManifest, std::string> ParseEngineManifest(std::string_view text, std::string_view name = EngineManifest::c_FileName);
    // The manifest at the root of a version's folder, read from the disk where it lies rather than through a mount, so Hub can read any version
    [[nodiscard]] TRINITY_API Expected<EngineManifest, std::string> ReadEngineManifest(const std::filesystem::path& root);
}
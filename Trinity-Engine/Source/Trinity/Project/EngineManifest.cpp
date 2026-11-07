#include "Trinity/Project/EngineManifest.hpp"

#include "Trinity/FileSystem/FileSystemUtilities.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <charconv>
#include <format>
#include <fstream>
#include <iterator>
#include <system_error>

namespace Trinity
{
    namespace
    {
        using FileSystemUtilities::FromUtf8;
        using FileSystemUtilities::ToUtf8;

        // The key's text when it is a scalar, or empty
        std::string ReadScalar(const YAML::Node& map, const char* key)
        {
            if (!map || !map.IsMap())
            {
                return {};
            }

            const YAML::Node l_Value = map[key];

            return l_Value && l_Value.IsScalar() ? l_Value.Scalar() : std::string();
        }

        // A whole number, or 0 for one missing or not a number
        std::uint32_t ReadFormat(const YAML::Node& formats, const char* key)
        {
            const std::string l_Text = ReadScalar(formats, key);
            std::uint32_t l_Value = 0;
            const std::from_chars_result l_Parsed = std::from_chars(l_Text.data(), l_Text.data() + l_Text.size(), l_Value);

            return l_Parsed.ec == std::errc() && l_Parsed.ptr == l_Text.data() + l_Text.size() ? l_Value : 0;
        }

        // The sequence's names in order, each once
        std::vector<std::string> ReadNames(const YAML::Node& map, const char* key)
        {
            std::vector<std::string> l_Names;
            const YAML::Node l_List = map[key];
            if (!l_List || !l_List.IsSequence())
            {
                return l_Names;
            }

            for (const YAML::Node it_Entry : l_List)
            {
                if (it_Entry.IsScalar() && !it_Entry.Scalar().empty() && std::ranges::find(l_Names, it_Entry.Scalar()) == l_Names.end())
                {
                    l_Names.push_back(it_Entry.Scalar());
                }
            }

            return l_Names;
        }

        // On one line, each name quoted when it is free text rather than one of a known set
        void WriteNames(YAML::Emitter& emitter, const char* key, const std::vector<std::string>& names, bool quoted)
        {
            emitter << YAML::Key << key << YAML::Value << YAML::Flow << YAML::BeginSeq;
            for (const std::string& it_Name : names)
            {
                if (quoted)
                {
                    emitter << YAML::DoubleQuoted;
                }

                emitter << it_Name;
            }

            emitter << YAML::EndSeq;
        }
    }

    bool EngineManifest::HasConfiguration(std::string_view configuration) const
    {
        return std::ranges::find(Configurations, configuration) != Configurations.end();
    }

    std::filesystem::path EngineManifest::GetConfigurationDirectory(const std::filesystem::path& root, std::string_view configuration) const
    {
        return root / FromUtf8(std::format("{}-{}-{}", configuration, System, Architecture)) / FromUtf8(c_ProgramFolder);
    }

    // Key for key the text CMake/TrinityWriteVersion.cmake writes, so a manifest the build writes saves again as the same bytes
    std::string WriteEngineManifest(const EngineManifest& manifest)
    {
        YAML::Emitter l_Emitter;
        l_Emitter << YAML::BeginMap;
        l_Emitter << YAML::Key << "Format" << YAML::Value << EngineManifest::c_FormatVersion;
        l_Emitter << YAML::Key << "Version" << YAML::Value << YAML::DoubleQuoted << manifest.Version;
        l_Emitter << YAML::Key << "Commit" << YAML::Value << YAML::DoubleQuoted << manifest.Commit;
        l_Emitter << YAML::Key << "Modified" << YAML::Value << manifest.Modified;
        l_Emitter << YAML::Key << "Date" << YAML::Value << YAML::DoubleQuoted << manifest.Date;
        l_Emitter << YAML::Key << "Platform" << YAML::Value << manifest.Platform;
        l_Emitter << YAML::Key << "System" << YAML::Value << manifest.System;
        l_Emitter << YAML::Key << "Architecture" << YAML::Value << manifest.Architecture;
        WriteNames(l_Emitter, "Exports", manifest.Exports, false);
        WriteNames(l_Emitter, "Configurations", manifest.Configurations, false);

        l_Emitter << YAML::Key << "Compiler" << YAML::Value << YAML::BeginMap;
        l_Emitter << YAML::Key << "Id" << YAML::Value << YAML::DoubleQuoted << manifest.CompilerId;
        l_Emitter << YAML::Key << "Version" << YAML::Value << YAML::DoubleQuoted << manifest.CompilerVersion;
        l_Emitter << YAML::EndMap;

        l_Emitter << YAML::Key << "DotNet" << YAML::Value << YAML::BeginMap;
        l_Emitter << YAML::Key << "Sdk" << YAML::Value << YAML::DoubleQuoted << manifest.DotNetSdk;
        l_Emitter << YAML::Key << "RollForward" << YAML::Value << YAML::DoubleQuoted << manifest.DotNetRollForward;
        l_Emitter << YAML::EndMap;

        const EngineFormats& l_Formats = manifest.Formats;
        l_Emitter << YAML::Key << "Formats" << YAML::Value << YAML::BeginMap;
        l_Emitter << YAML::Key << "Project" << YAML::Value << l_Formats.Project;
        l_Emitter << YAML::Key << "Scene" << YAML::Value << l_Formats.Scene;
        l_Emitter << YAML::Key << "Prefab" << YAML::Value << l_Formats.Prefab;
        l_Emitter << YAML::Key << "InputActions" << YAML::Value << l_Formats.InputActions;
        l_Emitter << YAML::Key << "AudioMixer" << YAML::Value << l_Formats.AudioMixer;
        l_Emitter << YAML::Key << "SoundEvent" << YAML::Value << l_Formats.SoundEvent;
        l_Emitter << YAML::Key << "Meta" << YAML::Value << l_Formats.Meta;
        l_Emitter << YAML::Key << "Pack" << YAML::Value << l_Formats.Pack;
        l_Emitter << YAML::EndMap;

        WriteNames(l_Emitter, "Templates", manifest.Templates, true);
        l_Emitter << YAML::EndMap;

        return std::string(l_Emitter.c_str()) + "\n";
    }

    Expected<EngineManifest, std::string> ParseEngineManifest(std::string_view text, std::string_view name)
    {
        YAML::Node l_Root;
        try
        {
            l_Root = YAML::Load(std::string(text));
        }
        catch (const YAML::Exception& exception)
        {
            return Unexpected{ std::format("{} is not valid YAML: {}", name, exception.what()) };
        }

        if (!l_Root.IsMap())
        {
            return Unexpected{ std::format("{} holds no manifest", name) };
        }

        std::uint32_t l_Format = 0;
        const std::string l_FormatText = ReadScalar(l_Root, "Format");
        const std::from_chars_result l_Parsed = std::from_chars(l_FormatText.data(), l_FormatText.data() + l_FormatText.size(), l_Format);
        if (l_Parsed.ec != std::errc() || l_Parsed.ptr != l_FormatText.data() + l_FormatText.size() || l_Format == 0)
        {
            return Unexpected{ std::format("{} has no valid Format version", name) };
        }

        if (l_Format > EngineManifest::c_FormatVersion)
        {
            return Unexpected{ std::format("{} was written in format {} by a newer Trinity, and this build reads format {} and older", name, l_Format, EngineManifest::c_FormatVersion) };
        }

        EngineManifest l_Manifest;
        l_Manifest.Version = ReadScalar(l_Root, "Version");
        if (l_Manifest.Version.empty())
        {
            return Unexpected{ std::format("{} names no version", name) };
        }

        l_Manifest.Commit = ReadScalar(l_Root, "Commit");
        const YAML::Node l_Modified = l_Root["Modified"];
        if (l_Modified && l_Modified.IsScalar())
        {
            static_cast<void>(YAML::convert<bool>::decode(l_Modified, l_Manifest.Modified));
        }

        l_Manifest.Date = ReadScalar(l_Root, "Date");
        l_Manifest.Platform = ReadScalar(l_Root, "Platform");
        l_Manifest.System = ReadScalar(l_Root, "System");
        l_Manifest.Architecture = ReadScalar(l_Root, "Architecture");
        if (l_Manifest.Platform.empty() || l_Manifest.System.empty() || l_Manifest.Architecture.empty())
        {
            return Unexpected{ std::format("{} does not name the platform, system and architecture its builds run on", name) };
        }

        l_Manifest.Exports = ReadNames(l_Root, "Exports");
        l_Manifest.Configurations = ReadNames(l_Root, "Configurations");
        if (l_Manifest.Configurations.empty())
        {
            return Unexpected{ std::format("{} names no configuration, so its folder holds no build of Trinity", name) };
        }

        const YAML::Node l_Compiler = l_Root["Compiler"];
        l_Manifest.CompilerId = ReadScalar(l_Compiler, "Id");
        l_Manifest.CompilerVersion = ReadScalar(l_Compiler, "Version");

        const YAML::Node l_DotNet = l_Root["DotNet"];
        l_Manifest.DotNetSdk = ReadScalar(l_DotNet, "Sdk");
        l_Manifest.DotNetRollForward = ReadScalar(l_DotNet, "RollForward");

        const YAML::Node l_Formats = l_Root["Formats"];
        l_Manifest.Formats.Project = ReadFormat(l_Formats, "Project");
        l_Manifest.Formats.Scene = ReadFormat(l_Formats, "Scene");
        l_Manifest.Formats.Prefab = ReadFormat(l_Formats, "Prefab");
        l_Manifest.Formats.InputActions = ReadFormat(l_Formats, "InputActions");
        l_Manifest.Formats.AudioMixer = ReadFormat(l_Formats, "AudioMixer");
        l_Manifest.Formats.SoundEvent = ReadFormat(l_Formats, "SoundEvent");
        l_Manifest.Formats.Meta = ReadFormat(l_Formats, "Meta");
        l_Manifest.Formats.Pack = ReadFormat(l_Formats, "Pack");

        l_Manifest.Templates = ReadNames(l_Root, "Templates");

        return l_Manifest;
    }
    Expected<EngineManifest, std::string> ReadEngineManifest(const std::filesystem::path& root)
    {
        const std::filesystem::path l_Path = root / FromUtf8(EngineManifest::c_FileName);
        std::error_code l_Error;
        if (!std::filesystem::is_regular_file(l_Path, l_Error))
        {
            return Unexpected{ std::format("{} holds no {}, so it is not a version of Trinity", ToUtf8(root), EngineManifest::c_FileName) };
        }

        std::ifstream l_File(l_Path, std::ios::binary);
        if (!l_File)
        {
            return Unexpected{ std::format("{} could not be opened", ToUtf8(l_Path)) };
        }

        const std::string l_Text{ std::istreambuf_iterator<char>(l_File), std::istreambuf_iterator<char>() };
        if (l_File.bad())
        {
            return Unexpected{ std::format("{} could not be read", ToUtf8(l_Path)) };
        }

        return ParseEngineManifest(l_Text, ToUtf8(l_Path));
    }
}
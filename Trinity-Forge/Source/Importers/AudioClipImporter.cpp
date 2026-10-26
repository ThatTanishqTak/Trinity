#include "Importers/AudioClipImporter.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <format>
#include <optional>
#include <span>

namespace
{
    constexpr std::string_view c_LoadTypeSetting = "LoadType";
    constexpr std::string_view c_PreloadSetting = "Preload";

    constexpr std::array<Trinity::AudioEncoding, 4> c_Encodings{ Trinity::AudioEncoding::Wav, Trinity::AudioEncoding::Flac, Trinity::AudioEncoding::Mp3, Trinity::AudioEncoding::Vorbis };

    // As the .meta writes them, without the spaces Properties shows
    struct LoadTypeName
    {
        Trinity::AudioLoadType Type = Trinity::AudioLoadType::DecompressOnLoad;
        std::string_view Name;
    };

    constexpr std::array<LoadTypeName, 3> c_LoadTypes{ { { Trinity::AudioLoadType::DecompressOnLoad, "DecompressOnLoad" },{ Trinity::AudioLoadType::CompressedInMemory, "CompressedInMemory" },{ Trinity::AudioLoadType::Streaming, "Streaming" } } };

    std::string_view GetSettingName(Trinity::AudioLoadType type)
    {
        const auto a_Found = std::ranges::find(c_LoadTypes, type, &LoadTypeName::Type);

        return a_Found != c_LoadTypes.end() ? a_Found->Name : c_LoadTypes.front().Name;
    }

    std::optional<Trinity::AudioLoadType> ParseLoadType(std::string_view text)
    {
        const auto a_Found = std::ranges::find(c_LoadTypes, text, &LoadTypeName::Name);

        return a_Found != c_LoadTypes.end() ? std::optional(a_Found->Type) : std::nullopt;
    }

    std::optional<bool> ParseBool(const std::string& text)
    {
        if (text == "true")
        {
            return true;
        }

        if (text == "false")
        {
            return false;
        }

        return std::nullopt;
    }

    // FNV-1a, which is plenty to tell one version of a file from the next
    std::uint64_t HashBytes(std::span<const std::byte> bytes)
    {
        std::uint64_t l_Hash = 14695981039346656037ull;
        for (const std::byte it_Byte : bytes)
        {
            l_Hash ^= std::to_integer<std::uint64_t>(it_Byte);
            l_Hash *= 1099511628211ull;
        }

        return l_Hash;
    }

    // Anything that changes what is cooked changes the key: the source, its settings, and the importer, whose version goes up with any change to how it cooks or to the engine's header
    std::string GetCacheKey(std::span<const std::byte> source, const AudioClipImportSettings& settings)
    {
        return std::format("content {:016x}, load type {}, preload {}, importer {}", HashBytes(source), GetSettingName(settings.LoadType), settings.Preload, AudioClipImporter::c_Version);
    }

    // The header, and the samples under one encoding's extension, which the header names
    bool HasCookedFiles(Trinity::UUID id)
    {
        return Trinity::FileSystem::Exists(Trinity::GetCookedAudioClipPath(id)) && std::ranges::any_of(c_Encodings, [id](Trinity::AudioEncoding encoding) { return Trinity::FileSystem::Exists(Trinity::GetCookedAudioDataPath(id, encoding)); });
    }
}

Trinity::AssetSettings AudioClipImporter::GetDefaultSettings()
{
    return MakeSettings({});
}

Trinity::AssetSettings AudioClipImporter::MakeSettings(const AudioClipImportSettings& settings)
{
    return { { std::string(c_LoadTypeSetting), std::string(GetSettingName(settings.LoadType)) },{ std::string(c_PreloadSetting), settings.Preload ? "true" : "false" } };
}

// A missing setting takes its default. One that cannot be read does too, with a warning naming the file
AudioClipImportSettings AudioClipImporter::ReadSettings(const Trinity::AssetRecord& record)
{
    AudioClipImportSettings l_Settings;
    if (const std::string* l_LoadType = record.FindSetting(c_LoadTypeSetting))
    {
        const std::optional<Trinity::AudioLoadType> l_Value = ParseLoadType(*l_LoadType);
        if (!l_Value)
        {
            TR_WARN("Audio: {} has {}: {}, which is not DecompressOnLoad, CompressedInMemory or Streaming, so it is read as {}", record.Path, c_LoadTypeSetting, *l_LoadType, GetSettingName(l_Settings.LoadType));
        }

        l_Settings.LoadType = l_Value.value_or(l_Settings.LoadType);
    }

    if (const std::string* l_Preload = record.FindSetting(c_PreloadSetting))
    {
        const std::optional<bool> l_Value = ParseBool(*l_Preload);
        if (!l_Value)
        {
            TR_WARN("Audio: {} has {}: {}, which is not true or false, so it is read as {}", record.Path, c_PreloadSetting, *l_Preload, l_Settings.Preload);
        }

        l_Settings.Preload = l_Value.value_or(l_Settings.Preload);
    }

    return l_Settings;
}

std::string AudioClipImporter::GetCacheKeyPath(Trinity::UUID id)
{
    return std::format("{}/Audio/{}.key", Trinity::Project::c_CacheMount, id);
}

std::vector<std::string> AudioClipImporter::GetCookedPaths(Trinity::UUID id)
{
    std::vector<std::string> l_Paths{ Trinity::GetCookedAudioClipPath(id), GetCacheKeyPath(id) };
    for (const Trinity::AudioEncoding it_Encoding : c_Encodings)
    {
        l_Paths.push_back(Trinity::GetCookedAudioDataPath(id, it_Encoding));
    }

    return l_Paths;
}

std::string AudioClipImporter::FormatLength(std::uint64_t frames, std::uint32_t sampleRate)
{
    const std::uint64_t l_Milliseconds = sampleRate != 0 ? static_cast<std::uint64_t>(std::llround(static_cast<double>(frames) * 1000.0 / sampleRate)) : 0;

    return std::format("{}:{:02}.{:03}", l_Milliseconds / 60000, l_Milliseconds / 1000 % 60, l_Milliseconds % 1000);
}

// The samples go first, then the header, and the key last, so an import cut short leaves no key and cooks again. Samples the cache already holds byte for byte are left as they are, so applying new settings or reimporting never writes over a file a clip streaming in a preview holds open
AudioClipImporter::Result AudioClipImporter::Import(const Trinity::AssetRecord& record)
{
    TR_PROFILE_FUNCTION();

    const auto l_Start = std::chrono::steady_clock::now();

    const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_Source = Trinity::FileSystem::ReadFile(record.Path);
    if (!l_Source)
    {
        TR_ERROR("Audio: {} could not be read: {}", record.Path, Trinity::ToString(l_Source.GetError()));

        return Result::Failed;
    }

    const AudioClipImportSettings l_Settings = ReadSettings(record);
    const std::string l_Key = GetCacheKey(*l_Source, l_Settings);
    const std::string l_KeyPath = GetCacheKeyPath(record.ID);
    const Trinity::Expected<std::string, Trinity::FileError> l_CachedKey = Trinity::FileSystem::ReadText(l_KeyPath);
    if (l_CachedKey && *l_CachedKey == l_Key && HasCookedFiles(record.ID))
    {
        return Result::Cached;
    }

    Trinity::Expected<Trinity::AudioClipHeader, std::string> l_Header = Trinity::AnalyzeAudio(*l_Source);
    if (!l_Header)
    {
        TR_ERROR("Audio: {} was not imported, since {}", record.Path, l_Header.GetError());

        return Result::Failed;
    }

    l_Header->ID = record.ID;
    l_Header->LoadType = l_Settings.LoadType;
    l_Header->Preload = l_Settings.Preload;

    const std::string l_SamplesPath = Trinity::GetCookedAudioDataPath(record.ID, l_Header->Encoding);
    const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_Cooked = Trinity::FileSystem::ReadFile(l_SamplesPath);
    if (!l_Cooked || !std::ranges::equal(*l_Cooked, *l_Source))
    {
        if (const Trinity::Expected<void, Trinity::FileError> l_Saved = Trinity::FileSystem::WriteFile(l_SamplesPath, *l_Source); !l_Saved)
        {
            TR_ERROR("Audio: {} could not be written to {}: {}", record.Path, l_SamplesPath, Trinity::ToString(l_Saved.GetError()));

            return Result::Failed;
        }
    }

    const std::string l_HeaderPath = Trinity::GetCookedAudioClipPath(record.ID);
    const Trinity::Expected<void, Trinity::FileError> l_HeaderSaved = Trinity::FileSystem::WriteFile(l_HeaderPath, Trinity::WriteAudioClipHeader(*l_Header));
    const Trinity::Expected<void, Trinity::FileError> l_KeySaved = l_HeaderSaved ? Trinity::FileSystem::WriteText(l_KeyPath, l_Key) : l_HeaderSaved;
    if (!l_KeySaved)
    {
        TR_ERROR("Audio: {} could not be written to {}: {}", record.Path, l_HeaderSaved ? l_KeyPath : l_HeaderPath, Trinity::ToString(l_KeySaved.GetError()));

        return Result::Failed;
    }

    // Another file put in the source's place may have been another encoding, whose samples are under their own extension
    for (const Trinity::AudioEncoding it_Encoding : c_Encodings)
    {
        if (it_Encoding != l_Header->Encoding)
        {
            static_cast<void>(Trinity::FileSystem::RemoveFile(Trinity::GetCookedAudioDataPath(record.ID, it_Encoding)));
        }
    }

    const auto l_Milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - l_Start).count();
    TR_INFO("Audio: imported {} ({}, {} channel(s) at {} Hz, {}, decoded as {}, {}{}) in {} ms", record.Path, Trinity::ToString(l_Header->Encoding), l_Header->Channels, l_Header->SampleRate, FormatLength(l_Header->Frames, l_Header->SampleRate), Trinity::ToString(l_Header->Format), Trinity::ToString(l_Settings.LoadType), l_Settings.Preload ? ", preloaded" : "", l_Milliseconds);

    return Result::Imported;
}
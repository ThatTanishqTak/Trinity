#pragma once

#include <Trinity.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Unity's Load Type and Preload Audio Data. Both go into the header alone, leaving the samples
struct AudioClipImportSettings
{
    Trinity::AudioLoadType LoadType = Trinity::AudioLoadType::DecompressOnLoad;
    bool Preload = true;

    [[nodiscard]] bool operator==(const AudioClipImportSettings&) const = default;
};

struct AudioClipImportReport
{
    std::size_t Clips = 0;
    std::size_t Imported = 0;
    std::size_t Cached = 0;
    std::size_t Failed = 0;
    std::size_t Stopped = 0;
};

// A WAV, FLAC, MP3 or OGG Vorbis file is cooked once every frame decodes: a header beside its bytes
class AudioClipImporter
{
public:
    enum class Result : std::uint8_t
    {
        Cached,
        Imported,
        Failed
    };

    static constexpr std::string_view c_Importer = Trinity::AudioClipAsset::c_AssetType;
    static constexpr std::uint32_t c_Version = 1;

    [[nodiscard]] static Trinity::AssetSettings GetDefaultSettings();
    [[nodiscard]] static Trinity::AssetSettings MakeSettings(const AudioClipImportSettings& settings);
    [[nodiscard]] static AudioClipImportSettings ReadSettings(const Trinity::AssetRecord& record);
    [[nodiscard]] static std::string GetCacheKeyPath(Trinity::UUID id);
    // Its header, its key, and its samples under each encoding's extension, as the source may change
    [[nodiscard]] static std::vector<std::string> GetCookedPaths(Trinity::UUID id);
    // A clip's length as Forge shows it, in minutes, seconds and milliseconds, as 3:05.250
    [[nodiscard]] static std::string FormatLength(std::uint64_t frames, std::uint32_t sampleRate);

    [[nodiscard]] static Result Import(const Trinity::AssetRecord& record);
};
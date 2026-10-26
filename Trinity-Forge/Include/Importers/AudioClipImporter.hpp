#pragma once

#include <Trinity.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Unity's Load Type and Preload Audio Data. Both go into the header alone, so applying either writes the header again and leaves the samples as they were cooked
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

// A WAV, FLAC, MP3 or OGG Vorbis file becomes a cooked clip once every frame of it decodes: the engine's header, with its length, channels, rate and waveform, and beside it the file's bytes as they were
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
    // Everything the importer may have cooked for the clip: its header, its key, and its samples under each encoding's extension, since another file put in the source's place may be another encoding
    [[nodiscard]] static std::vector<std::string> GetCookedPaths(Trinity::UUID id);
    // A clip's length as Forge shows it, in minutes, seconds and milliseconds, as 3:05.250
    [[nodiscard]] static std::string FormatLength(std::uint64_t frames, std::uint32_t sampleRate);

    [[nodiscard]] static Result Import(const Trinity::AssetRecord& record);
};
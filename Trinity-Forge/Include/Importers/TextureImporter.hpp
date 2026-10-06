#pragma once

#include <Trinity.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

struct TextureImportSettings
{
    bool Srgb = true;
    bool GenerateMips = true;
    std::uint32_t UastcLevel = 2;
    Trinity::RHI::Filter Filter = Trinity::RHI::Filter::Linear;

    [[nodiscard]] bool operator==(const TextureImportSettings&) const = default;
};

struct TextureImportReport
{
    std::size_t Textures = 0;
    std::size_t Encoded = 0;
    std::size_t Cached = 0;
    std::size_t Failed = 0;
    std::size_t Stopped = 0;
};

class TextureImporter
{
public:
    enum class Result : std::uint8_t
    {
        Cached,
        Encoded,
        Failed
    };

    static constexpr std::string_view c_Importer = Trinity::TextureAsset::c_AssetType;
    static constexpr std::uint32_t c_Version = 2;
    static constexpr std::uint32_t c_MaxUastcLevel = 4;

    // What Import would do, found without encoding: Cached, Failed, or Encoded when the texture needs encoding, with the size of its image and the texels of every level together
    struct Plan
    {
        Result Outcome = Result::Failed;
        std::uint32_t Width = 0;
        std::uint32_t Height = 0;
        std::uint64_t Texels = 0;
    };

    [[nodiscard]] static Trinity::AssetSettings GetDefaultSettings();
    [[nodiscard]] static Trinity::AssetSettings MakeSettings(const TextureImportSettings& settings);
    [[nodiscard]] static TextureImportSettings ReadSettings(const Trinity::AssetRecord& record);
    [[nodiscard]] static std::string GetCacheKey(std::span<const std::byte> source, const TextureImportSettings& settings);
    [[nodiscard]] static std::string GetCacheKeyPath(Trinity::UUID id);

    [[nodiscard]] static Plan PlanImport(const Trinity::AssetRecord& record);
    [[nodiscard]] static Result Import(const Trinity::AssetRecord& record);
};
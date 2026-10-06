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
};

struct TextureImportReport
{
    std::size_t Textures = 0;
    std::size_t Encoded = 0;
    std::size_t Cached = 0;
    std::size_t Failed = 0;
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

    [[nodiscard]] static Trinity::AssetSettings GetDefaultSettings();
    [[nodiscard]] static TextureImportSettings ReadSettings(const Trinity::AssetRecord& record);
    [[nodiscard]] static std::string GetCacheKey(std::span<const std::byte> source, const TextureImportSettings& settings);
    [[nodiscard]] static std::string GetCacheKeyPath(Trinity::UUID id);

    [[nodiscard]] static Result Import(const Trinity::AssetRecord& record);
    static TextureImportReport ImportAll(const Trinity::AssetRegistry& registry);
};
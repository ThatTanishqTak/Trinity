#include "Importers/TextureImporter.hpp"

#include <ktx.h>
#include <stb_image.h>
#include <stb_image_resize2.h>

#include <algorithm>
#include <bit>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <format>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

namespace
{
    // zstd decompresses as fast at any level, and a texture is encoded once, so the level favours size
    constexpr ktx_uint32_t c_ZstdLevel = 18;
    constexpr int c_Channels = 4;

    // The VkFormat of the RGBA8 image handed to the Basis encoder, which takes the transfer function from it
    constexpr ktx_uint32_t c_VkFormatRGBA8Unorm = 37;
    constexpr ktx_uint32_t c_VkFormatRGBA8Srgb = 43;

    constexpr std::string_view c_SrgbSetting = "Srgb";
    constexpr std::string_view c_MipsSetting = "GenerateMips";
    constexpr std::string_view c_UastcSetting = "UastcLevel";

    struct KtxDeleter
    {
        void operator()(ktxTexture2* texture) const
        {
            ktxTexture2_Destroy(texture);
        }
    };

    struct StbDeleter
    {
        void operator()(stbi_uc* pixels) const
        {
            stbi_image_free(pixels);
        }
    };

    struct MallocDeleter
    {
        void operator()(ktx_uint8_t* data) const
        {
            std::free(data);
        }
    };

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

    std::optional<std::uint32_t> ParseUnsigned(const std::string& text)
    {
        std::uint32_t l_Value = 0;
        const std::from_chars_result l_Parsed = std::from_chars(text.data(), text.data() + text.size(), l_Value);
        if (l_Parsed.ec != std::errc() || l_Parsed.ptr != text.data() + text.size())
        {
            return std::nullopt;
        }

        return l_Value;
    }
}

Trinity::AssetSettings TextureImporter::GetDefaultSettings()
{
    const TextureImportSettings l_Defaults;

    return { { std::string(c_SrgbSetting), l_Defaults.Srgb ? "true" : "false" }, { std::string(c_MipsSetting), l_Defaults.GenerateMips ? "true" : "false" }, { std::string(c_UastcSetting), std::to_string(l_Defaults.UastcLevel) } };
}

// A missing setting takes its default. One that cannot be read does too, with a warning naming the file
TextureImportSettings TextureImporter::ReadSettings(const Trinity::AssetRecord& record)
{
    TextureImportSettings l_Settings;
    if (const std::string* l_Srgb = record.FindSetting(c_SrgbSetting))
    {
        const std::optional<bool> l_Value = ParseBool(*l_Srgb);
        if (!l_Value)
        {
            TR_WARN("Textures: {} has {}: {}, which is not true or false, so it is read as {}", record.Path, c_SrgbSetting, *l_Srgb, l_Settings.Srgb);
        }

        l_Settings.Srgb = l_Value.value_or(l_Settings.Srgb);
    }

    if (const std::string* l_Mips = record.FindSetting(c_MipsSetting))
    {
        const std::optional<bool> l_Value = ParseBool(*l_Mips);
        if (!l_Value)
        {
            TR_WARN("Textures: {} has {}: {}, which is not true or false, so it is read as {}", record.Path, c_MipsSetting, *l_Mips, l_Settings.GenerateMips);
        }

        l_Settings.GenerateMips = l_Value.value_or(l_Settings.GenerateMips);
    }

    if (const std::string* l_Level = record.FindSetting(c_UastcSetting))
    {
        const std::optional<std::uint32_t> l_Value = ParseUnsigned(*l_Level);
        if (!l_Value || *l_Value > c_MaxUastcLevel)
        {
            TR_WARN("Textures: {} has {}: {}, and the levels go from 0 to {}, so it is read as {}", record.Path, c_UastcSetting, *l_Level, c_MaxUastcLevel, l_Settings.UastcLevel);
        }
        else
        {
            l_Settings.UastcLevel = *l_Value;
        }
    }

    return l_Settings;
}

// Anything that changes the cooked file changes the key: the source, its settings, and the importer, whose version goes up with any change to how it encodes, KTX-Software's included
std::string TextureImporter::GetCacheKey(std::span<const std::byte> source, const TextureImportSettings& settings)
{
    return std::format("content {:016x}, srgb {}, mips {}, uastc {}, importer {}", HashBytes(source), settings.Srgb, settings.GenerateMips, settings.UastcLevel, c_Version);
}

std::string TextureImporter::GetCacheKeyPath(Trinity::UUID id)
{
    return std::format("{}/Textures/{}.key", Trinity::Project::c_CacheMount, id);
}

// The key is written after the KTX2 file, so an import cut short leaves no key and is encoded again
TextureImporter::Result TextureImporter::Import(const Trinity::AssetRecord& record)
{
    TR_PROFILE_FUNCTION();

    const auto l_Start = std::chrono::steady_clock::now();

    const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_Source = Trinity::FileSystem::ReadFile(record.Path);
    if (!l_Source)
    {
        TR_ERROR("Textures: {} could not be read: {}", record.Path, Trinity::ToString(l_Source.GetError()));

        return Result::Failed;
    }

    const TextureImportSettings l_Settings = ReadSettings(record);
    const std::string l_Key = GetCacheKey(*l_Source, l_Settings);
    const std::string l_CookedPath = Trinity::GetCookedTexturePath(record.ID);
    const std::string l_KeyPath = GetCacheKeyPath(record.ID);
    if (Trinity::FileSystem::Exists(l_CookedPath))
    {
        const Trinity::Expected<std::string, Trinity::FileError> l_CachedKey = Trinity::FileSystem::ReadText(l_KeyPath);
        if (l_CachedKey && *l_CachedKey == l_Key)
        {
            return Result::Cached;
        }
    }

    int l_Width = 0;
    int l_Height = 0;
    int l_SourceChannels = 0;
    const std::unique_ptr<stbi_uc, StbDeleter> l_Pixels(stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(l_Source->data()), static_cast<int>(l_Source->size()), &l_Width, &l_Height, &l_SourceChannels, c_Channels));
    if (!l_Pixels)
    {
        TR_ERROR("Textures: {} could not be decoded: {}", record.Path, stbi_failure_reason());

        return Result::Failed;
    }

    const std::uint32_t l_BaseWidth = static_cast<std::uint32_t>(l_Width);
    const std::uint32_t l_BaseHeight = static_cast<std::uint32_t>(l_Height);
    const std::uint32_t l_Levels = l_Settings.GenerateMips ? static_cast<std::uint32_t>(std::bit_width(std::max(l_BaseWidth, l_BaseHeight))) : 1;

    ktxTextureCreateInfo l_CreateInfo{};
    l_CreateInfo.vkFormat = l_Settings.Srgb ? c_VkFormatRGBA8Srgb : c_VkFormatRGBA8Unorm;
    l_CreateInfo.baseWidth = l_BaseWidth;
    l_CreateInfo.baseHeight = l_BaseHeight;
    l_CreateInfo.baseDepth = 1;
    l_CreateInfo.numDimensions = 2;
    l_CreateInfo.numLevels = l_Levels;
    l_CreateInfo.numLayers = 1;
    l_CreateInfo.numFaces = 1;
    l_CreateInfo.isArray = KTX_FALSE;
    l_CreateInfo.generateMipmaps = KTX_FALSE;

    ktxTexture2* l_Created = nullptr;
    KTX_error_code l_Result = ktxTexture2_Create(&l_CreateInfo, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &l_Created);
    if (l_Result != KTX_SUCCESS)
    {
        TR_ERROR("Textures: {} ({}x{}) could not be given a KTX2 texture: {}", record.Path, l_Width, l_Height, ktxErrorString(l_Result));

        return Result::Failed;
    }

    const std::unique_ptr<ktxTexture2, KtxDeleter> l_Texture(l_Created);
    ktxTexture* l_Base = ktxTexture(l_Texture.get());

    // Every mip comes from the full image, filtered in linear light for an sRGB texture, with colours weighted by alpha
    std::vector<stbi_uc> l_Mip;
    for (std::uint32_t it_Level = 0; it_Level < l_Levels && l_Result == KTX_SUCCESS; ++it_Level)
    {
        const std::uint32_t l_MipWidth = Trinity::RHI::GetMipSize(l_BaseWidth, it_Level);
        const std::uint32_t l_MipHeight = Trinity::RHI::GetMipSize(l_BaseHeight, it_Level);
        const std::size_t l_MipSize = std::size_t{ l_MipWidth } * l_MipHeight * c_Channels;

        const stbi_uc* l_Image = l_Pixels.get();
        if (it_Level != 0)
        {
            l_Mip.resize(l_MipSize);
            const int l_OutputWidth = static_cast<int>(l_MipWidth);
            const int l_OutputHeight = static_cast<int>(l_MipHeight);
            const stbi_uc* l_Resized = l_Settings.Srgb ? stbir_resize_uint8_srgb(l_Pixels.get(), l_Width, l_Height, 0, l_Mip.data(), l_OutputWidth, l_OutputHeight, 0, STBIR_RGBA) : stbir_resize_uint8_linear(l_Pixels.get(), l_Width, l_Height, 0, l_Mip.data(), l_OutputWidth, l_OutputHeight, 0, STBIR_RGBA);
            if (l_Resized == nullptr)
            {
                TR_ERROR("Textures: {} could not be resized for mip {}", record.Path, it_Level);

                return Result::Failed;
            }

            l_Image = l_Mip.data();
        }

        l_Result = ktxTexture_SetImageFromMemory(l_Base, it_Level, 0, 0, l_Image, l_MipSize);
    }

    if (l_Result != KTX_SUCCESS)
    {
        TR_ERROR("Textures: {} could not be copied into its KTX2 texture: {}", record.Path, ktxErrorString(l_Result));

        return Result::Failed;
    }

    ktxBasisParams l_Parameters{};
    l_Parameters.structSize = sizeof(l_Parameters);
    l_Parameters.uastc = KTX_TRUE;
    l_Parameters.threadCount = std::max(1u, std::thread::hardware_concurrency());
    l_Parameters.uastcFlags = l_Settings.UastcLevel;

    l_Result = ktxTexture2_CompressBasisEx(l_Texture.get(), &l_Parameters);
    if (l_Result == KTX_SUCCESS)
    {
        l_Result = ktxTexture2_DeflateZstd(l_Texture.get(), c_ZstdLevel);
    }

    ktx_uint8_t* l_Written = nullptr;
    ktx_size_t l_WrittenSize = 0;
    if (l_Result == KTX_SUCCESS)
    {
        l_Result = ktxTexture2_WriteToMemory(l_Texture.get(), &l_Written, &l_WrittenSize);
    }

    const std::unique_ptr<ktx_uint8_t, MallocDeleter> l_File(l_Written);
    if (l_Result != KTX_SUCCESS)
    {
        TR_ERROR("Textures: {} could not be encoded: {}", record.Path, ktxErrorString(l_Result));

        return Result::Failed;
    }

    const Trinity::Expected<void, Trinity::FileError> l_Saved = Trinity::FileSystem::WriteFile(l_CookedPath, std::as_bytes(std::span(l_File.get(), l_WrittenSize)));
    const Trinity::Expected<void, Trinity::FileError> l_KeySaved = l_Saved ? Trinity::FileSystem::WriteText(l_KeyPath, l_Key) : l_Saved;
    if (!l_KeySaved)
    {
        TR_ERROR("Textures: {} could not be written to {}: {}", record.Path, l_Saved ? l_KeyPath : l_CookedPath, Trinity::ToString(l_KeySaved.GetError()));

        return Result::Failed;
    }

    const auto l_Milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - l_Start).count();
    TR_INFO("Textures: encoded {} ({}x{}, {} mip(s), {}, UASTC level {}) into {} in {} ms", record.Path, l_Width, l_Height, l_Levels, l_Settings.Srgb ? "sRGB" : "linear", l_Settings.UastcLevel, Trinity::Memory::FormatBytes(l_WrittenSize), l_Milliseconds);

    return Result::Encoded;
}

// Runs after every scan. A folder whose textures are all in the cache stays quiet
TextureImportReport TextureImporter::ImportAll(const Trinity::AssetRegistry& registry)
{
    TR_PROFILE_FUNCTION();

    TextureImportReport l_Report;
    for (const Trinity::AssetRecord* it_Record : registry.GetRecords())
    {
        if (it_Record->Importer != c_Importer)
        {
            continue;
        }

        ++l_Report.Textures;
        switch (Import(*it_Record))
        {
            case Result::Cached:
            {
                ++l_Report.Cached;
                break;
            }
            case Result::Encoded:
            {
                ++l_Report.Encoded;
                break;
            }
            case Result::Failed:
            {
                ++l_Report.Failed;
                break;
            }
        }
    }

    if (l_Report.Encoded != 0 || l_Report.Failed != 0)
    {
        TR_INFO("Textures: {} in the project, {} encoded, {} from the cache, {} failed", l_Report.Textures, l_Report.Encoded, l_Report.Cached, l_Report.Failed);
    }

    return l_Report;
}
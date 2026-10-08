#pragma once

#include "Trinity/Asset/Asset.hpp"
#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/UUID.hpp"
#include "Trinity/RHI/Resources.hpp"
#include "Trinity/RHI/Types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Trinity
{
    class EnvironmentLoader;

    [[nodiscard]] TRINITY_API std::string GetCookedEnvironmentPath(UUID id);

    // What a cooked environment holds, in this order: the radiance prefiltered for GGX, one roughness to a mip, the irradiance, and the BRDF lookup table the split sum reads
    enum class EnvironmentImage : std::uint32_t
    {
        Specular,
        Irradiance,
        BrdfLookup,

        Count
    };

    // A cooked environment file: this header, then each image as a KTX2 file of RGBA16Float texels, zstd-supercompressed, the cubemaps with their faces as +X, -X, +Y, -Y, +Z, -Z. The specular cubemap's mip m is filtered for perceptual roughness m / (levels - 1)
    struct EnvironmentFileHeader
    {
        static constexpr std::array<char, 8> c_Magic{ 'T', 'R', 'E', 'N', 'V', '\0', '\0', '\0' };
        static constexpr std::uint32_t c_Version = 1;

        std::array<char, 8> Magic = c_Magic;
        std::uint32_t Version = c_Version;
        std::uint32_t Padding = 0;
        std::array<std::uint64_t, static_cast<std::size_t>(EnvironmentImage::Count)> Offsets{};
        std::array<std::uint64_t, static_cast<std::size_t>(EnvironmentImage::Count)> Sizes{};
    };

    static_assert(sizeof(EnvironmentFileHeader) == 64);

    // Image-based lighting from an HDR environment, sampled through bindless indices
    class TRINITY_API EnvironmentAsset final : public Asset
    {
    public:
        static constexpr std::string_view c_AssetType = "Environment";

        EnvironmentAsset() = default;
        ~EnvironmentAsset() override;

        EnvironmentAsset(const EnvironmentAsset&) = delete;
        EnvironmentAsset& operator=(const EnvironmentAsset&) = delete;

        [[nodiscard]] std::string_view GetAssetType() const override
        {
            return c_AssetType;
        }

        [[nodiscard]] std::uint32_t GetShaderResourceIndex(EnvironmentImage image) const { return m_Images[static_cast<std::size_t>(image)].ShaderResourceIndex; }
        [[nodiscard]] RHI::TextureHandle GetTexture(EnvironmentImage image) const { return m_Images[static_cast<std::size_t>(image)].Texture; }
        [[nodiscard]] std::uint32_t GetSize(EnvironmentImage image) const { return m_Images[static_cast<std::size_t>(image)].Size; }
        [[nodiscard]] std::uint32_t GetSpecularLevels() const { return m_Images[static_cast<std::size_t>(EnvironmentImage::Specular)].Levels; }

    private:
        friend class EnvironmentLoader;

        using Bytes = std::vector<std::byte, TaggedAllocator<std::byte, MemoryTag::Assets>>;

        struct Image
        {
            RHI::TextureHandle Texture;
            std::uint32_t ShaderResourceIndex = RHI::c_NoBindlessIndex;
            std::uint32_t Size = 0;
            std::uint32_t Levels = 0;
            std::uint32_t Faces = 0;
            // Level by level, each level's faces in turn, until the upload, which frees them
            std::vector<Bytes, TaggedAllocator<Bytes, MemoryTag::Assets>> Data;
        };

        const EnvironmentLoader* m_Loader = nullptr;
        std::array<Image, static_cast<std::size_t>(EnvironmentImage::Count)> m_Images{};
    };
}
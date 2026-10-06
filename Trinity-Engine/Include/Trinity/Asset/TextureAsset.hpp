#pragma once

#include "Trinity/Asset/Asset.hpp"
#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/UUID.hpp"
#include "Trinity/RHI/Resources.hpp"
#include "Trinity/RHI/Types.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Trinity
{
    class TextureLoader;

    inline constexpr std::string_view c_TextureFilterKey = "Trinity.Filter";

    [[nodiscard]] TRINITY_API std::string GetCookedTexturePath(UUID id);

    class TRINITY_API TextureAsset final : public Asset
    {
    public:
        static constexpr std::string_view c_AssetType = "Texture";

        TextureAsset() = default;
        ~TextureAsset() override;

        TextureAsset(const TextureAsset&) = delete;
        TextureAsset& operator=(const TextureAsset&) = delete;

        [[nodiscard]] std::string_view GetAssetType() const override
        {
            return c_AssetType;
        }

        [[nodiscard]] RHI::TextureHandle GetTexture() const { return m_Texture; }
        [[nodiscard]] std::uint32_t GetShaderResourceIndex() const { return m_ShaderResourceIndex; }
        [[nodiscard]] std::uint32_t GetWidth() const { return m_Width; }
        [[nodiscard]] std::uint32_t GetHeight() const { return m_Height; }
        [[nodiscard]] std::uint32_t GetMipLevels() const { return m_MipLevels; }
        [[nodiscard]] RHI::Format GetFormat() const { return m_Format; }
        [[nodiscard]] RHI::Filter GetFilter() const { return m_Filter; }
        [[nodiscard]] bool IsSrgb() const { return m_Srgb; }

    private:
        friend class TextureLoader;

        using MipData = std::vector<std::byte, TaggedAllocator<std::byte, MemoryTag::Assets>>;

        const TextureLoader* m_Loader = nullptr;
        RHI::TextureHandle m_Texture;
        std::uint32_t m_ShaderResourceIndex = RHI::c_NoBindlessIndex;
        std::uint32_t m_Width = 0;
        std::uint32_t m_Height = 0;
        std::uint32_t m_MipLevels = 0;
        RHI::Format m_Format = RHI::Format::Unknown;
        RHI::Filter m_Filter = RHI::Filter::Linear;
        bool m_Srgb = false;
        std::vector<MipData, TaggedAllocator<MipData, MemoryTag::Assets>> m_Mips;
    };
}
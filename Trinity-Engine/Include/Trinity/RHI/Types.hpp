#pragma once

#include "Trinity/Core/Export.hpp"

#include <cstdint>
#include <string_view>

namespace Trinity
{
    namespace RHI
    {
        constexpr std::uint32_t c_FramesInFlight = 2;
        constexpr std::uint32_t c_MaxPushConstantSize = 128;
        constexpr std::uint32_t c_MaxColorAttachments = 8;
        constexpr std::uint32_t c_TextureCopyRowAlignment = 256;
        constexpr std::uint32_t c_TextureCopyOffsetAlignment = 512;
        constexpr std::uint32_t c_NoBindlessIndex = UINT32_MAX;
        constexpr std::uint32_t c_MaxArrayLayers = 2048;
        constexpr std::uint32_t c_CubeFaceCount = 6;

        // A mip or layer count that reaches the last mip or layer
        constexpr std::uint32_t c_RemainingSubresources = UINT32_MAX;

        template<typename Tag>
        struct Handle
        {
            std::uint32_t Index = 0;
            std::uint32_t Generation = 0;

            [[nodiscard]] constexpr bool IsValid() const { return Generation != 0; }
            constexpr explicit operator bool() const { return IsValid(); }

            constexpr bool operator==(const Handle&) const = default;
        };

        using BufferHandle = Handle<struct BufferTag>;
        using TextureHandle = Handle<struct TextureTag>;
        using PipelineHandle = Handle<struct PipelineTag>;
        using SamplerHandle = Handle<struct SamplerTag>;

        enum class Format : std::uint8_t
        {
            Unknown = 0,
            RGBA8Unorm,
            RGBA8Srgb,
            BGRA8Unorm,
            BGRA8Srgb,
            RGBA16Float,
            R11G11B10Float,
            R32Float,
            R32Uint,
            RG32Float,
            RGB32Float,
            RGBA32Float,
            D32Float,
            BC1Unorm,
            BC1Srgb,
            BC3Unorm,
            BC3Srgb,
            BC4Unorm,
            BC5Unorm,
            BC7Unorm,
            BC7Srgb
        };

        [[nodiscard]] TRINITY_API std::string_view ToString(Format format);

        // Bytes per texel, or per 4x4 block for a compressed format, and 0 for Unknown
        [[nodiscard]] TRINITY_API std::uint32_t GetFormatSize(Format format);

        [[nodiscard]] constexpr bool IsCompressedFormat(Format format)
        {
            return format >= Format::BC1Unorm && format <= Format::BC7Srgb;
        }

        // Texels across and down one block: 4 for a compressed format, and 1 otherwise
        [[nodiscard]] constexpr std::uint32_t GetFormatBlockDimension(Format format)
        {
            return IsCompressedFormat(format) ? 4 : 1;
        }

        // The bytes from one row of texels, or of blocks, to the next when CopyTextureToBuffer or CopyBufferToTexture copies a region this many texels wide
        [[nodiscard]] TRINITY_API std::uint64_t GetTextureCopyRowPitch(Format format, std::uint32_t width);

        // Rows of texels, or of blocks, in a region this many texels high
        [[nodiscard]] TRINITY_API std::uint32_t GetTextureCopyRowCount(Format format, std::uint32_t height);

        // The bytes a copy of a region this size reads or writes in its buffer
        [[nodiscard]] TRINITY_API std::uint64_t GetTextureCopySize(Format format, std::uint32_t width, std::uint32_t height);

        // The width or height of a mip, never below 1
        [[nodiscard]] constexpr std::uint32_t GetMipSize(std::uint32_t size, std::uint32_t mipLevel)
        {
            return mipLevel < 32 && (size >> mipLevel) > 1 ? size >> mipLevel : 1;
        }

        [[nodiscard]] constexpr bool IsDepthFormat(Format format)
        {
            return format == Format::D32Float;
        }

        // A cube has six layers, one per face in the order +X, -X, +Y, -Y, +Z, -Z
        enum class TextureDimension : std::uint8_t
        {
            Texture2D,
            Texture2DArray,
            TextureCube
        };

        // Mips and layers that a barrier covers, all of them by default
        struct TextureSubresourceRange
        {
            std::uint32_t BaseMipLevel = 0;
            std::uint32_t MipLevelCount = c_RemainingSubresources;
            std::uint32_t BaseArrayLayer = 0;
            std::uint32_t ArrayLayerCount = c_RemainingSubresources;

            [[nodiscard]] constexpr bool IsWhole() const { return BaseMipLevel == 0 && MipLevelCount == c_RemainingSubresources && BaseArrayLayer == 0 && ArrayLayerCount == c_RemainingSubresources; }
        };

        // Whether a range starts inside a texture with this many mips and layers and does not reach past its end
        [[nodiscard]] constexpr bool IsRangeInsideTexture(const TextureSubresourceRange& range, std::uint32_t mipLevels, std::uint32_t arrayLayers)
        {
            const auto a_Fits = [](std::uint32_t base, std::uint32_t count, std::uint32_t total) { return base < total && count != 0 && (count == c_RemainingSubresources || std::uint64_t{ base } + count <= total); };

            return a_Fits(range.BaseMipLevel, range.MipLevelCount, mipLevels) && a_Fits(range.BaseArrayLayer, range.ArrayLayerCount, arrayLayers);
        }

        // One mip of one layer, numbered with the mips of each layer together, as D3D12 numbers subresources
        [[nodiscard]] constexpr std::uint32_t GetSubresourceIndex(std::uint32_t mipLevel, std::uint32_t arrayLayer, std::uint32_t mipLevels)
        {
            return mipLevel + arrayLayer * mipLevels;
        }

        enum class IndexFormat : std::uint8_t
        {
            UInt16,
            UInt32
        };

        [[nodiscard]] constexpr std::uint32_t GetIndexSize(IndexFormat format)
        {
            return format == IndexFormat::UInt16 ? 2 : 4;
        }

        enum class MemoryType : std::uint8_t
        {
            GPU,
            Upload,
            Readback
        };

        enum class BufferUsage : std::uint32_t
        {
            None = 0,
            ShaderResource = 1u << 0,
            UnorderedAccess = 1u << 1,
            Index = 1u << 2,
            Indirect = 1u << 3,
            CopySource = 1u << 4,
            CopyDestination = 1u << 5
        };

        enum class TextureUsage : std::uint32_t
        {
            None = 0,
            ShaderResource = 1u << 0,
            UnorderedAccess = 1u << 1,
            RenderTarget = 1u << 2,
            DepthStencil = 1u << 3,
            CopySource = 1u << 4,
            CopyDestination = 1u << 5
        };

        [[nodiscard]] constexpr BufferUsage operator|(BufferUsage left, BufferUsage right)
        {
            return static_cast<BufferUsage>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
        }

        [[nodiscard]] constexpr bool HasFlag(BufferUsage flags, BufferUsage flag)
        {
            return (static_cast<std::uint32_t>(flags) & static_cast<std::uint32_t>(flag)) != 0;
        }

        [[nodiscard]] constexpr TextureUsage operator|(TextureUsage left, TextureUsage right)
        {
            return static_cast<TextureUsage>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
        }

        [[nodiscard]] constexpr bool HasFlag(TextureUsage flags, TextureUsage flag)
        {
            return (static_cast<std::uint32_t>(flags) & static_cast<std::uint32_t>(flag)) != 0;
        }

        // What a resource is used for between two barriers, undefined lets the GPU discard the contents
        enum class ResourceState : std::uint8_t
        {
            Undefined,
            Present,
            RenderTarget,
            DepthWrite,
            DepthRead,
            ShaderResource,
            UnorderedAccess,
            CopySource,
            CopyDestination,
            IndexBuffer,
            IndirectArgument
        };

        struct Viewport
        {
            float X = 0.0f;
            float Y = 0.0f;
            float Width = 0.0f;
            float Height = 0.0f;
            float MinDepth = 0.0f;
            float MaxDepth = 1.0f;
        };

        struct Rect
        {
            std::int32_t X = 0;
            std::int32_t Y = 0;
            std::uint32_t Width = 0;
            std::uint32_t Height = 0;
        };

        // Whether region is not empty and lies inside one mip of a texture this size
        [[nodiscard]] constexpr bool IsRegionInsideMip(const Rect& region, std::uint32_t width, std::uint32_t height, std::uint32_t mipLevel)
        {
            return region.X >= 0 && region.Y >= 0 && region.Width != 0 && region.Height != 0 && std::uint64_t{ static_cast<std::uint32_t>(region.X) } + region.Width <= GetMipSize(width, mipLevel) && std::uint64_t{ static_cast<std::uint32_t>(region.Y) } + region.Height <= GetMipSize(height, mipLevel);
        }

        // A region of a compressed texture starts on a block and covers whole blocks, except where it reaches the right or bottom edge of its mip
        [[nodiscard]] constexpr bool IsRegionBlockAligned(const Rect& region, Format format, std::uint32_t width, std::uint32_t height, std::uint32_t mipLevel)
        {
            const std::uint32_t l_Block = GetFormatBlockDimension(format);
            const std::uint32_t l_X = static_cast<std::uint32_t>(region.X);
            const std::uint32_t l_Y = static_cast<std::uint32_t>(region.Y);

            return l_X % l_Block == 0 && l_Y % l_Block == 0 && (region.Width % l_Block == 0 || l_X + region.Width == GetMipSize(width, mipLevel)) && (region.Height % l_Block == 0 || l_Y + region.Height == GetMipSize(height, mipLevel));
        }
    }
}
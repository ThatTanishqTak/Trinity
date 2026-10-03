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

        enum class Format : std::uint8_t
        {
            Unknown = 0,
            RGBA8Unorm,
            RGBA8Srgb,
            BGRA8Unorm,
            BGRA8Srgb,
            RGBA16Float,
            R32Float,
            R32Uint,
            RG32Float,
            RGB32Float,
            RGBA32Float,
            D32Float
        };

        [[nodiscard]] TRINITY_API std::string_view ToString(Format format);

        // Bytes per texel, 0 for Unknown
        [[nodiscard]] TRINITY_API std::uint32_t GetFormatSize(Format format);

        [[nodiscard]] constexpr bool IsDepthFormat(Format format)
        {
            return format == Format::D32Float;
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
    }
}
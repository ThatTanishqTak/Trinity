#pragma once

#include "Trinity/RHI/Types.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace Trinity
{
    namespace RHI
    {
        // Upload and Readback buffers stay mapped for their whole life, upload buffers can always be copied from, and Readback buffers copied into
        struct BufferDescription
        {
            std::uint64_t Size = 0;
            BufferUsage Usage = BufferUsage::None;
            MemoryType Memory = MemoryType::GPU;
            std::string_view DebugName;
        };

        // A Texture2D has one layer, a TextureCube six and a square size, and a Texture2DArray up to c_MaxArrayLayers. A multisampled texture is a Texture2D with one mip, used as a render target or depth texture and read by shaders, never copied or written as storage
        struct TextureDescription
        {
            std::uint32_t Width = 1;
            std::uint32_t Height = 1;
            std::uint32_t MipLevels = 1;
            std::uint32_t ArrayLayers = 1;
            TextureDimension Dimension = TextureDimension::Texture2D;
            std::uint32_t SampleCount = 1;

            Format TextureFormat = Format::RGBA8Unorm;
            TextureUsage Usage = TextureUsage::ShaderResource;

            std::array<float, 4> ClearColor{ 0.0f, 0.0f, 0.0f, 1.0f };
            float ClearDepth = 0.0f;
            bool OptimizedClear = true;

            std::string_view DebugName;
        };

        enum class Filter : std::uint8_t
        {
            Nearest,
            Linear
        };

        enum class AddressMode : std::uint8_t
        {
            Repeat,
            MirroredRepeat,
            ClampToEdge
        };

        // MaxLod at or above c_LodUnclamped leaves the mip range open
        constexpr float c_LodUnclamped = 1000.0f;

        struct SamplerDescription
        {
            Filter MinFilter = Filter::Linear;
            Filter MagFilter = Filter::Linear;
            Filter MipFilter = Filter::Linear;

            AddressMode AddressU = AddressMode::Repeat;
            AddressMode AddressV = AddressMode::Repeat;
            AddressMode AddressW = AddressMode::Repeat;

            float MipLodBias = 0.0f;
            float MinLod = 0.0f;
            float MaxLod = c_LodUnclamped;

            std::string_view DebugName;
        };
    }
}
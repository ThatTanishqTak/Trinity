#pragma once

#include "Trinity/RHI/Types.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace Trinity
{
    namespace RHI
    {
        // Upload and Readback buffers stay mapped for their whole life
        struct BufferDescription
        {
            std::uint64_t Size = 0;
            BufferUsage Usage = BufferUsage::None;
            MemoryType Memory = MemoryType::GPU;
            std::string_view DebugName;
        };

        // A TextureCube has six layers and a square size. A multisampled texture is a Texture2D, one mip
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

        // Written by CommandList::WriteTimestamp, copied out as 64-bit ticks by ResolveTimestamps
        struct QueryPoolDescription
        {
            std::uint32_t Count = 0;
            std::string_view DebugName;
        };

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
#pragma once

#include "Trinity/RHI/Types.hpp"

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

        struct TextureDescription
        {
            std::uint32_t Width = 1;
            std::uint32_t Height = 1;
            std::uint32_t MipLevels = 1;
            Format TextureFormat = Format::RGBA8Unorm;
            TextureUsage Usage = TextureUsage::ShaderResource;
            std::string_view DebugName;
        };
    }
}
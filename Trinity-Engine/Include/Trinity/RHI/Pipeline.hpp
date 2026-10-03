#pragma once

#include "Trinity/RHI/Types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace Trinity
{
    namespace RHI
    {
        enum class PrimitiveTopology : std::uint8_t
        {
            TriangleList,
            TriangleStrip,
            LineList,
            PointList
        };

        enum class CullMode : std::uint8_t
        {
            None,
            Front,
            Back
        };

        enum class CompareOp : std::uint8_t
        {
            Never,
            Less,
            Equal,
            LessOrEqual,
            Greater,
            NotEqual,
            GreaterOrEqual,
            Always
        };

        struct ShaderDescription
        {
            std::span<const std::byte> Code;
            std::string_view EntryPoint;
        };

        struct GraphicsPipelineDescription
        {
            ShaderDescription VertexShader;
            ShaderDescription PixelShader;
            std::span<const Format> ColorFormats;
            Format DepthFormat = Format::Unknown;
            PrimitiveTopology Topology = PrimitiveTopology::TriangleList;
            CullMode Cull = CullMode::Back;
            bool FrontCounterClockwise = true;
            bool DepthTest = false;
            bool DepthWrite = false;
            CompareOp DepthCompare = CompareOp::GreaterOrEqual;
            bool AlphaBlend = false;
            std::string_view DebugName;
        };
    }
}
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
            // Added to each fragment's depth: the constant in units of the smallest step of a 32-bit float depth near that depth, the slope times the triangle's largest depth slope, and the sum limited to the clamp unless the clamp is 0. With reversed depth a positive bias moves towards the camera
            std::int32_t DepthBiasConstant = 0;
            float DepthBiasSlope = 0.0f;
            float DepthBiasClamp = 0.0f;
            // Clamps depth to the viewport's range instead of clipping triangles that reach past it
            bool DepthClamp = false;
            bool AlphaBlend = false;
            std::string_view DebugName;
        };

        // Shares the root signature or pipeline layout, and so the push constants and bindless heaps, with every graphics pipeline
        struct ComputePipelineDescription
        {
            ShaderDescription ComputeShader;
            std::string_view DebugName;
        };
    }
}
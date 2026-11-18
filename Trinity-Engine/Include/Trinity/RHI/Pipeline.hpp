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

        // Applied by a fragment that passes the stencil and depth tests. One that fails keeps the value
        enum class StencilOp : std::uint8_t
        {
            Keep,
            Zero,
            Replace,
            IncrementClamp,
            DecrementClamp
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
            std::uint32_t SampleCount = 1;
            PrimitiveTopology Topology = PrimitiveTopology::TriangleList;
            CullMode Cull = CullMode::Back;
            bool FrontCounterClockwise = true;
            bool DepthTest = false;
            bool DepthWrite = false;
            CompareOp DepthCompare = CompareOp::GreaterOrEqual;
            // With reversed depth a positive bias moves towards the camera. A clamp of 0 is no clamp
            std::int32_t DepthBiasConstant = 0;
            float DepthBiasSlope = 0.0f;
            float DepthBiasClamp = 0.0f;
            // Clamps depth to the viewport's range instead of clipping triangles that reach past it
            bool DepthClamp = false;
            // Front and back faces alike. Needs a DepthFormat with stencil
            bool StencilTest = false;
            CompareOp StencilCompare = CompareOp::Always;
            StencilOp StencilPass = StencilOp::Keep;
            std::uint8_t StencilReadMask = 0xFF;
            std::uint8_t StencilWriteMask = 0xFF;
            bool AlphaBlend = false;
            // With AlphaBlend, premultiplied colours, as RmlUi's are, blend as One and OneMinusSrcAlpha
            bool PremultipliedAlpha = false;
            // Off writes no colour at all, as a pipeline that only draws a stencil mask does
            bool ColorWrite = true;
            std::string_view DebugName;
        };

        // Shares the root signature or pipeline layout with every graphics pipeline
        struct ComputePipelineDescription
        {
            ShaderDescription ComputeShader;
            std::string_view DebugName;
        };
    }
}
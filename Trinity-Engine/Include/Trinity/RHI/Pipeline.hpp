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

        // What a fragment that passes the stencil and depth tests does to the stencil value. One that fails either keeps it
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
            // Added to each fragment's depth: the constant in units of the smallest step of a 32-bit float depth near that depth, the slope times the triangle's largest depth slope, and the sum limited to the clamp unless the clamp is 0. With reversed depth a positive bias moves towards the camera
            std::int32_t DepthBiasConstant = 0;
            float DepthBiasSlope = 0.0f;
            float DepthBiasClamp = 0.0f;
            // Clamps depth to the viewport's range instead of clipping triangles that reach past it
            bool DepthClamp = false;
            // Front and back faces alike, comparing the reference CommandList::SetStencilReference sets with the stencil value, each through the read mask, and writing through the write mask. Needs a DepthFormat with stencil
            bool StencilTest = false;
            CompareOp StencilCompare = CompareOp::Always;
            StencilOp StencilPass = StencilOp::Keep;
            std::uint8_t StencilReadMask = 0xFF;
            std::uint8_t StencilWriteMask = 0xFF;
            bool AlphaBlend = false;
            // With AlphaBlend, colours already multiplied by their alpha, as RmlUi's are, blend as One and OneMinusSrcAlpha
            bool PremultipliedAlpha = false;
            // Off writes no colour at all, as a pipeline that only draws a stencil mask does
            bool ColorWrite = true;
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
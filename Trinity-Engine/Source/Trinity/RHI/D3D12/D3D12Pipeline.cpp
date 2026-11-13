#include "Trinity/RHI/D3D12/D3D12Pipeline.hpp"
#include "Trinity/RHI/D3D12/D3D12PipelineLibrary.hpp"
#include "Trinity/RHI/D3D12/D3D12RootSignature.hpp"
#include "Trinity/RHI/D3D12/D3D12Utilities.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Hash.hpp"
#include "Trinity/Core/Log.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Trinity
{
    namespace RHI
    {
        namespace
        {
            D3D12_PRIMITIVE_TOPOLOGY_TYPE ToTopologyType(PrimitiveTopology topology)
            {
                switch (topology)
                {
                    case PrimitiveTopology::LineList:
                    {
                        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
                    }
                    case PrimitiveTopology::PointList:
                    {
                        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
                    }
                    default:
                    {
                        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
                    }
                }
            }

            D3D_PRIMITIVE_TOPOLOGY ToD3DTopology(PrimitiveTopology topology)
            {
                switch (topology)
                {
                    case PrimitiveTopology::TriangleStrip:
                    {
                        return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
                    }
                    case PrimitiveTopology::LineList:
                    {
                        return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
                    }
                    case PrimitiveTopology::PointList:
                    {
                        return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
                    }
                    default:
                    {
                        return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
                    }
                }
            }

            D3D12_CULL_MODE ToCullMode(CullMode cull)
            {
                switch (cull)
                {
                    case CullMode::Front:
                    {
                        return D3D12_CULL_MODE_FRONT;
                    }
                    case CullMode::Back:
                    {
                        return D3D12_CULL_MODE_BACK;
                    }
                    default:
                    {
                        return D3D12_CULL_MODE_NONE;
                    }
                }
            }

            D3D12_COMPARISON_FUNC ToComparisonFunc(CompareOp compare)
            {
                switch (compare)
                {
                    case CompareOp::Never:
                    {
                        return D3D12_COMPARISON_FUNC_NEVER;
                    }
                    case CompareOp::Less:
                    {
                        return D3D12_COMPARISON_FUNC_LESS;
                    }
                    case CompareOp::Equal:
                    {
                        return D3D12_COMPARISON_FUNC_EQUAL;
                    }
                    case CompareOp::LessOrEqual:
                    {
                        return D3D12_COMPARISON_FUNC_LESS_EQUAL;
                    }
                    case CompareOp::Greater:
                    {
                        return D3D12_COMPARISON_FUNC_GREATER;
                    }
                    case CompareOp::NotEqual:
                    {
                        return D3D12_COMPARISON_FUNC_NOT_EQUAL;
                    }
                    case CompareOp::GreaterOrEqual:
                    {
                        return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
                    }
                    default:
                    {
                        return D3D12_COMPARISON_FUNC_ALWAYS;
                    }
                }
            }

            D3D12_STENCIL_OP ToStencilOp(StencilOp operation)
            {
                switch (operation)
                {
                    case StencilOp::Zero:
                    {
                        return D3D12_STENCIL_OP_ZERO;
                    }
                    case StencilOp::Replace:
                    {
                        return D3D12_STENCIL_OP_REPLACE;
                    }
                    case StencilOp::IncrementClamp:
                    {
                        return D3D12_STENCIL_OP_INCR_SAT;
                    }
                    case StencilOp::DecrementClamp:
                    {
                        return D3D12_STENCIL_OP_DECR_SAT;
                    }
                    default:
                    {
                        return D3D12_STENCIL_OP_KEEP;
                    }
                }
            }

            template<typename... Values>
            void Append(std::vector<std::byte>& bytes, const Values&... values)
            {
                (bytes.insert(bytes.end(), reinterpret_cast<const std::byte*>(&values), reinterpret_cast<const std::byte*>(&values) + sizeof(Values)), ...);
            }

            std::span<const std::byte> ToBytes(std::string_view text)
            {
                return std::as_bytes(std::span(text.data(), text.size()));
            }

            // The name a pipeline state has in the pipeline library, from the hash of everything it is made from, each part's hash seeding the next, so two pipeline states share a name only when they are the same
            std::wstring GetLibraryName(std::string_view kind, std::initializer_list<std::span<const std::byte>> parts)
            {
                std::uint64_t l_Hash = 0;
                for (const std::span<const std::byte> it_Part : parts)
                {
                    l_Hash = Hash::XXHash64(it_Part, l_Hash);
                }

                return ToWide(std::format("{} {:016x}", kind, l_Hash));
            }

            std::wstring GetLibraryName(const GraphicsPipelineDescription& description)
            {
                std::vector<std::byte> l_State;
                Append(l_State, description.DepthFormat, description.SampleCount, description.Topology, description.Cull, description.FrontCounterClockwise, description.DepthTest, description.DepthWrite, description.DepthCompare, description.DepthBiasConstant, description.DepthBiasSlope, description.DepthBiasClamp, description.DepthClamp);
                Append(l_State, description.StencilTest, description.StencilCompare, description.StencilPass, description.StencilReadMask, description.StencilWriteMask, description.AlphaBlend, description.PremultipliedAlpha, description.ColorWrite);

                return GetLibraryName("Graphics", { description.VertexShader.Code, ToBytes(description.VertexShader.EntryPoint), description.PixelShader.Code, ToBytes(description.PixelShader.EntryPoint), std::as_bytes(description.ColorFormats), l_State });
            }
        }

        std::optional<D3D12Pipeline> D3D12Pipeline::CreateGraphics(const D3D12RootSignature& rootSignature, D3D12PipelineLibrary& library, const GraphicsPipelineDescription& description)
        {
            TR_CORE_ASSERT(!description.VertexShader.Code.empty() && !description.PixelShader.Code.empty(), "Pipeline '{}' is missing shader code.", description.DebugName);
            TR_CORE_ASSERT(description.ColorFormats.size() <= c_MaxColorAttachments, "Pipeline '{}' has too many color formats.", description.DebugName);
            TR_CORE_ASSERT(!description.StencilTest || HasStencil(description.DepthFormat), "Pipeline '{}' tests stencil without a depth format that has it.", description.DebugName);

            D3D12_GRAPHICS_PIPELINE_STATE_DESC l_Description{};
            l_Description.pRootSignature = rootSignature.GetHandle();
            l_Description.VS = { description.VertexShader.Code.data(), description.VertexShader.Code.size() };
            l_Description.PS = { description.PixelShader.Code.data(), description.PixelShader.Code.size() };
            l_Description.SampleMask = UINT_MAX;
            l_Description.PrimitiveTopologyType = ToTopologyType(description.Topology);
            l_Description.SampleDesc.Count = description.SampleCount;

            // An integer target is never blended, so blending is set for each target
            l_Description.NumRenderTargets = static_cast<UINT>(std::min<std::size_t>(description.ColorFormats.size(), c_MaxColorAttachments));
            l_Description.BlendState.IndependentBlendEnable = TRUE;
            for (UINT it_Target = 0; it_Target < l_Description.NumRenderTargets; ++it_Target)
            {
                D3D12_RENDER_TARGET_BLEND_DESC& l_Blend = l_Description.BlendState.RenderTarget[it_Target];
                l_Blend.BlendEnable = description.AlphaBlend && !IsIntegerFormat(description.ColorFormats[it_Target]) ? TRUE : FALSE;
                l_Blend.SrcBlend = description.PremultipliedAlpha ? D3D12_BLEND_ONE : D3D12_BLEND_SRC_ALPHA;
                l_Blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
                l_Blend.BlendOp = D3D12_BLEND_OP_ADD;
                l_Blend.SrcBlendAlpha = D3D12_BLEND_ONE;
                l_Blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
                l_Blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
                l_Blend.LogicOp = D3D12_LOGIC_OP_NOOP;
                l_Blend.RenderTargetWriteMask = description.ColorWrite ? D3D12_COLOR_WRITE_ENABLE_ALL : 0;

                l_Description.RTVFormats[it_Target] = ToDXGIFormat(description.ColorFormats[it_Target]);
            }

            l_Description.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
            l_Description.RasterizerState.CullMode = ToCullMode(description.Cull);
            l_Description.RasterizerState.FrontCounterClockwise = description.FrontCounterClockwise ? TRUE : FALSE;
            l_Description.RasterizerState.DepthBias = description.DepthBiasConstant;
            l_Description.RasterizerState.SlopeScaledDepthBias = description.DepthBiasSlope;
            l_Description.RasterizerState.DepthBiasClamp = description.DepthBiasClamp;

            // D3D12 always clamps depth to the viewport's range, so turning clipping off is what Vulkan's depth clamp does
            l_Description.RasterizerState.DepthClipEnable = description.DepthClamp ? FALSE : TRUE;

            // Vulkan never writes depth without the depth test, so neither does this
            l_Description.DepthStencilState.DepthEnable = description.DepthTest ? TRUE : FALSE;
            l_Description.DepthStencilState.DepthWriteMask = description.DepthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
            l_Description.DepthStencilState.DepthFunc = ToComparisonFunc(description.DepthCompare);
            l_Description.DepthStencilState.StencilEnable = description.StencilTest ? TRUE : FALSE;
            l_Description.DepthStencilState.StencilReadMask = description.StencilReadMask;
            l_Description.DepthStencilState.StencilWriteMask = description.StencilWriteMask;
            l_Description.DepthStencilState.FrontFace = { D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, ToStencilOp(description.StencilPass), ToComparisonFunc(description.StencilCompare) };
            l_Description.DepthStencilState.BackFace = l_Description.DepthStencilState.FrontFace;
            l_Description.DSVFormat = description.DepthFormat != Format::Unknown ? ToDXGIFormat(description.DepthFormat) : DXGI_FORMAT_UNKNOWN;

            D3D12Pipeline l_Pipeline;
            l_Pipeline.Topology = ToD3DTopology(description.Topology);
            const HRESULT l_Result = library.CreateGraphics(GetLibraryName(description), l_Description, l_Pipeline.State);
            if (FAILED(l_Result))
            {
                TR_CORE_ERROR("D3D12: pipeline '{}' could not be created ({})", description.DebugName, FormatResult(l_Result));

                return {};
            }

            if (!description.DebugName.empty())
            {
                l_Pipeline.State->SetName(ToWide(description.DebugName).c_str());
            }

            return l_Pipeline;
        }

        std::optional<D3D12Pipeline> D3D12Pipeline::CreateCompute(const D3D12RootSignature& rootSignature, D3D12PipelineLibrary& library, const ComputePipelineDescription& description)
        {
            TR_CORE_ASSERT(!description.ComputeShader.Code.empty(), "Pipeline '{}' is missing shader code.", description.DebugName);

            D3D12_COMPUTE_PIPELINE_STATE_DESC l_Description{};
            l_Description.pRootSignature = rootSignature.GetHandle();
            l_Description.CS = { description.ComputeShader.Code.data(), description.ComputeShader.Code.size() };

            D3D12Pipeline l_Pipeline;
            l_Pipeline.Compute = true;
            const HRESULT l_Result = library.CreateCompute(GetLibraryName("Compute", { description.ComputeShader.Code, ToBytes(description.ComputeShader.EntryPoint) }), l_Description, l_Pipeline.State);
            if (FAILED(l_Result))
            {
                TR_CORE_ERROR("D3D12: pipeline '{}' could not be created ({})", description.DebugName, FormatResult(l_Result));

                return {};
            }

            if (!description.DebugName.empty())
            {
                l_Pipeline.State->SetName(ToWide(description.DebugName).c_str());
            }

            return l_Pipeline;
        }

        D3D12Release D3D12Pipeline::ToRelease()
        {
            D3D12Release l_Release;
            l_Release.Pipeline = std::move(State);

            return l_Release;
        }
    }
}
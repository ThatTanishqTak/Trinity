#pragma once

#include "Trinity/RHI/D3D12/D3D12Headers.hpp"
#include "Trinity/RHI/D3D12/D3D12ReleaseQueue.hpp"
#include "Trinity/RHI/Pipeline.hpp"

#include <optional>

namespace Trinity
{
    namespace RHI
    {
        class D3D12PipelineLibrary;
        class D3D12RootSignature;

        struct D3D12Pipeline
        {
            // Loaded from the pipeline library, or made and stored in it. Empty, logged, on failure
            [[nodiscard]] static std::optional<D3D12Pipeline> CreateGraphics(const D3D12RootSignature& rootSignature, D3D12PipelineLibrary& library, const GraphicsPipelineDescription& description);
            [[nodiscard]] static std::optional<D3D12Pipeline> CreateCompute(const D3D12RootSignature& rootSignature, D3D12PipelineLibrary& library, const ComputePipelineDescription& description);
            // What destroying it leaves for the release queue, which takes its pipeline state
            [[nodiscard]] D3D12Release ToRelease();

            Microsoft::WRL::ComPtr<ID3D12PipelineState> State;
            D3D_PRIMITIVE_TOPOLOGY Topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
            bool Compute = false;
        };
    }
}
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
        class D3D12RootSignature;

        // A graphics or compute pipeline state on the bindless root signature, with the topology command lists draw it with
        struct D3D12Pipeline
        {
            // Loaded from the pipeline library, or else made and stored in it. Empty, with the reason logged, when the pipeline state cannot be created
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
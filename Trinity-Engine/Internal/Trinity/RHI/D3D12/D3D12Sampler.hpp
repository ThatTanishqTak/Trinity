#pragma once

#include "Trinity/RHI/D3D12/D3D12DescriptorHeap.hpp"
#include "Trinity/RHI/D3D12/D3D12ReleaseQueue.hpp"
#include "Trinity/RHI/Resources.hpp"

#include <cstdint>
#include <optional>

namespace Trinity
{
    namespace RHI
    {
        class D3D12Device;

        // A sampler is only its descriptor in the shader-visible sampler heap
        struct D3D12Sampler
        {
            // Empty, with the reason logged, when every sampler index is in use
            [[nodiscard]] static std::optional<D3D12Sampler> Create(const D3D12Device& device, D3D12DescriptorHeap& samplerHeap, const SamplerDescription& description);
            // What destroying it leaves for the release queue
            [[nodiscard]] D3D12Release ToRelease() const;

            std::uint32_t Index = c_NoDescriptor;
        };
    }
}
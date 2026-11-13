#pragma once

#include "Trinity/RHI/D3D12/D3D12Headers.hpp"

#include <cstdint>

namespace Trinity
{
    namespace RHI
    {
        class D3D12Device;

        // A frame's command allocator, with the fence value the frame signals once its commands have finished
        struct D3D12CommandAllocator
        {
            // S_OK, or the failure creating it
            [[nodiscard]] HRESULT Create(const D3D12Device& device);

            Microsoft::WRL::ComPtr<ID3D12CommandAllocator> Allocator;
            std::uint64_t CompletionValue = 0;
        };
    }
}
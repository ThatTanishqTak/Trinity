#pragma once

#include "Trinity/RHI/D3D12/D3D12Headers.hpp"

#include <cstdint>

namespace Trinity
{
    namespace RHI
    {
        class D3D12Device;

        // Holds the fence value its frame signals once its commands have finished
        struct D3D12CommandAllocator
        {
            [[nodiscard]] HRESULT Create(const D3D12Device& device, D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT);

            Microsoft::WRL::ComPtr<ID3D12CommandAllocator> Allocator;
            std::uint64_t CompletionValue = 0;
        };
    }
}
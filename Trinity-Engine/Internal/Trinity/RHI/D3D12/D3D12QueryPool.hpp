#pragma once

#include "Trinity/RHI/D3D12/D3D12Headers.hpp"
#include "Trinity/RHI/D3D12/D3D12ReleaseQueue.hpp"
#include "Trinity/RHI/Resources.hpp"

#include <cstdint>
#include <optional>

namespace Trinity
{
    namespace RHI
    {
        class D3D12Device;

        // A heap of timestamps the GPU writes on the queue
        struct D3D12QueryPool
        {
            // Empty, with the reason logged, when the query heap cannot be created
            [[nodiscard]] static std::optional<D3D12QueryPool> Create(const D3D12Device& device, const QueryPoolDescription& description);
            // Ticks per second of the timestamps the queue writes, or 0 without a queue
            [[nodiscard]] static std::uint64_t GetTimestampFrequency(ID3D12CommandQueue* queue);
            // What destroying it leaves for the release queue, which takes its query heap
            [[nodiscard]] D3D12Release ToRelease();

            Microsoft::WRL::ComPtr<ID3D12QueryHeap> Heap;
            std::uint32_t Count = 0;
        };
    }
}
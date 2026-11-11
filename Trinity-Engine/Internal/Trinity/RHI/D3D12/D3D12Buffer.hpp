#pragma once

#include "Trinity/RHI/D3D12/D3D12DescriptorHeap.hpp"
#include "Trinity/RHI/D3D12/D3D12Headers.hpp"
#include "Trinity/RHI/D3D12/D3D12ReleaseQueue.hpp"
#include "Trinity/RHI/Resources.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace Trinity
{
    namespace RHI
    {
        class D3D12Allocator;
        class D3D12Device;

        // A buffer in D3D12MemoryAllocator's memory, mapped for good when it lives in an Upload or Readback heap, with a raw view for each way shaders reach it
        struct D3D12Buffer
        {
            // Empty, with the reason logged, when the buffer cannot be created or mapped
            [[nodiscard]] static std::optional<D3D12Buffer> Create(const D3D12Device& device, const D3D12Allocator& allocator, D3D12DescriptorHeap& resourceHeap, const BufferDescription& description);
            // What destroying it leaves for the release queue, which takes its allocation
            [[nodiscard]] D3D12Release ToRelease();

            Microsoft::WRL::ComPtr<D3D12MA::Allocation> Allocation;
            std::byte* Mapped = nullptr;
            std::uint64_t Size = 0;
            std::uint32_t ShaderResourceIndex = c_NoDescriptor;
            std::uint32_t UnorderedAccessIndex = c_NoDescriptor;
        };
    }
}
#pragma once

#include "Trinity/RHI/Bindless.hpp"
#include "Trinity/RHI/D3D12/D3D12Headers.hpp"

#include <cstdint>

namespace Trinity
{
    namespace RHI
    {
        // A heap of descriptors handed out a slot at a time, CPU-only for render target and depth views and shader-visible for the bindless heaps
        class D3D12DescriptorHeap
        {
        public:
            [[nodiscard]] bool Initialize(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, std::uint32_t capacity, bool shaderVisible);
            void Reset();

            [[nodiscard]] std::uint32_t Allocate();
            void Free(std::uint32_t index);

            [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE GetHandle(std::uint32_t index) const;
            [[nodiscard]] ID3D12DescriptorHeap* GetHeap() const { return m_Heap.Get(); }

        private:
            Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_Heap;
            D3D12_CPU_DESCRIPTOR_HANDLE m_Start{};
            std::uint32_t m_Increment = 0;
            IndexAllocator m_Indices;
        };
    }
}
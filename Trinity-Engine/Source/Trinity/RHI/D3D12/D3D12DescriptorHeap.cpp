#include "Trinity/RHI/D3D12/D3D12DescriptorHeap.hpp"

namespace Trinity
{
    namespace RHI
    {
        bool D3D12DescriptorHeap::Initialize(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, std::uint32_t capacity, bool shaderVisible)
        {
            D3D12_DESCRIPTOR_HEAP_DESC l_Description{};
            l_Description.Type = type;
            l_Description.NumDescriptors = capacity;
            l_Description.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            if (FAILED(device->CreateDescriptorHeap(&l_Description, IID_PPV_ARGS(&m_Heap))))
            {
                return false;
            }

            m_Start = m_Heap->GetCPUDescriptorHandleForHeapStart();
            m_Increment = device->GetDescriptorHandleIncrementSize(type);
            m_Indices.Reset(capacity);

            return true;
        }

        void D3D12DescriptorHeap::Reset()
        {
            m_Heap.Reset();
        }

        std::uint32_t D3D12DescriptorHeap::Allocate()
        {
            return m_Indices.Allocate();
        }

        void D3D12DescriptorHeap::Free(std::uint32_t index)
        {
            m_Indices.Free(index);
        }

        D3D12_CPU_DESCRIPTOR_HANDLE D3D12DescriptorHeap::GetHandle(std::uint32_t index) const
        {
            return { m_Start.ptr + static_cast<SIZE_T>(index) * m_Increment };
        }
    }
}
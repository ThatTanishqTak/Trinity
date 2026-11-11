#include "Trinity/RHI/D3D12/D3D12Buffer.hpp"
#include "Trinity/RHI/D3D12/D3D12Allocator.hpp"
#include "Trinity/RHI/D3D12/D3D12Device.hpp"
#include "Trinity/RHI/D3D12/D3D12Utilities.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Memory.hpp"

#include <optional>
#include <utility>

namespace Trinity
{
    namespace RHI
    {
        namespace
        {
            D3D12_HEAP_TYPE ToHeapType(MemoryType memory)
            {
                switch (memory)
                {
                    case MemoryType::Upload:
                    {
                        return D3D12_HEAP_TYPE_UPLOAD;
                    }
                    case MemoryType::Readback:
                    {
                        return D3D12_HEAP_TYPE_READBACK;
                    }
                    default:
                    {
                        return D3D12_HEAP_TYPE_DEFAULT;
                    }
                }
            }

            // Buffers get raw views, which is what a ByteAddressBuffer reads and writes
            void CreateShaderViews(const D3D12Device& device, D3D12DescriptorHeap& resourceHeap, D3D12Buffer& buffer, BufferUsage usage)
            {
                ID3D12Resource* l_Resource = buffer.Allocation->GetResource();
                const UINT l_Elements = static_cast<UINT>(buffer.Size / 4);

                if (HasFlag(usage, BufferUsage::ShaderResource))
                {
                    buffer.ShaderResourceIndex = resourceHeap.Allocate();
                    TR_CORE_ASSERT(buffer.ShaderResourceIndex != c_NoDescriptor, "Out of bindless resource indices.");
                    if (buffer.ShaderResourceIndex != c_NoDescriptor)
                    {
                        D3D12_SHADER_RESOURCE_VIEW_DESC l_View{};
                        l_View.Format = DXGI_FORMAT_R32_TYPELESS;
                        l_View.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
                        l_View.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                        l_View.Buffer.NumElements = l_Elements;
                        l_View.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
                        device.GetHandle()->CreateShaderResourceView(l_Resource, &l_View, resourceHeap.GetHandle(buffer.ShaderResourceIndex));
                    }
                }

                if (HasFlag(usage, BufferUsage::UnorderedAccess))
                {
                    buffer.UnorderedAccessIndex = resourceHeap.Allocate();
                    TR_CORE_ASSERT(buffer.UnorderedAccessIndex != c_NoDescriptor, "Out of bindless resource indices.");
                    if (buffer.UnorderedAccessIndex != c_NoDescriptor)
                    {
                        D3D12_UNORDERED_ACCESS_VIEW_DESC l_View{};
                        l_View.Format = DXGI_FORMAT_R32_TYPELESS;
                        l_View.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
                        l_View.Buffer.NumElements = l_Elements;
                        l_View.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
                        device.GetHandle()->CreateUnorderedAccessView(l_Resource, nullptr, &l_View, resourceHeap.GetHandle(buffer.UnorderedAccessIndex));
                    }
                }
            }
        }

        // Buffers always start in the undefined layout under enhanced barriers. Upload and Readback heaps are coherent and stay mapped
        std::optional<D3D12Buffer> D3D12Buffer::Create(const D3D12Device& device, const D3D12Allocator& allocator, D3D12DescriptorHeap& resourceHeap, const BufferDescription& description)
        {
            TR_CORE_ASSERT(description.Size != 0, "Buffer '{}' has no size.", description.DebugName);

            D3D12MA::ALLOCATION_DESC l_Allocation{};
            l_Allocation.HeapType = ToHeapType(description.Memory);

            D3D12_RESOURCE_DESC1 l_Resource{};
            l_Resource.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            l_Resource.Width = description.Size;
            l_Resource.Height = 1;
            l_Resource.DepthOrArraySize = 1;
            l_Resource.MipLevels = 1;
            l_Resource.Format = DXGI_FORMAT_UNKNOWN;
            l_Resource.SampleDesc.Count = 1;
            l_Resource.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            l_Resource.Flags = HasFlag(description.Usage, BufferUsage::UnorderedAccess) ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;

            D3D12Buffer l_Buffer;
            HRESULT l_Result = allocator.GetHandle()->CreateResource3(&l_Allocation, &l_Resource, D3D12_BARRIER_LAYOUT_UNDEFINED, nullptr, 0, nullptr, &l_Buffer.Allocation, IID_NULL, nullptr);
            if (FAILED(l_Result))
            {
                TR_CORE_ERROR("D3D12: buffer '{}' of {} could not be created ({})", description.DebugName, Memory::FormatBytes(description.Size), FormatResult(l_Result));

                return {};
            }

            ID3D12Resource* l_Native = l_Buffer.Allocation->GetResource();
            if (description.Memory != MemoryType::GPU)
            {
                // The CPU only writes Upload buffers, so mapping one says nothing will be read
                const D3D12_RANGE l_NoRead{ 0, 0 };
                void* l_Mapped = nullptr;
                l_Result = l_Native->Map(0, description.Memory == MemoryType::Upload ? &l_NoRead : nullptr, &l_Mapped);
                if (FAILED(l_Result))
                {
                    TR_CORE_ERROR("D3D12: buffer '{}' could not be mapped ({})", description.DebugName, FormatResult(l_Result));

                    return {};
                }

                l_Buffer.Mapped = static_cast<std::byte*>(l_Mapped);
            }

            l_Buffer.Size = description.Size;
            SetDebugName(l_Native, description.DebugName);
            CreateShaderViews(device, resourceHeap, l_Buffer, description.Usage);

            return l_Buffer;
        }

        D3D12Release D3D12Buffer::ToRelease()
        {
            D3D12Release l_Release;
            l_Release.Allocation = std::move(Allocation);
            l_Release.ShaderResourceIndex = ShaderResourceIndex;
            l_Release.UnorderedAccessIndex = UnorderedAccessIndex;

            return l_Release;
        }
    }
}
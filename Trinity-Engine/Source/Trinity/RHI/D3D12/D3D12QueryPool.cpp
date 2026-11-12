#include "Trinity/RHI/D3D12/D3D12QueryPool.hpp"
#include "Trinity/RHI/D3D12/D3D12Device.hpp"
#include "Trinity/RHI/D3D12/D3D12Utilities.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"

#include <cstdint>
#include <optional>
#include <utility>

namespace Trinity
{
    namespace RHI
    {
        std::optional<D3D12QueryPool> D3D12QueryPool::Create(const D3D12Device& device, const QueryPoolDescription& description)
        {
            TR_CORE_ASSERT(description.Count != 0, "Query pool '{}' has no queries.", description.DebugName);

            D3D12_QUERY_HEAP_DESC l_Description{};
            l_Description.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
            l_Description.Count = description.Count;

            D3D12QueryPool l_Pool;
            l_Pool.Count = description.Count;
            const HRESULT l_Result = device.GetHandle()->CreateQueryHeap(&l_Description, IID_PPV_ARGS(&l_Pool.Heap));
            if (FAILED(l_Result))
            {
                TR_CORE_ERROR("D3D12: query pool '{}' could not be created ({})", description.DebugName, FormatResult(l_Result));

                return {};
            }

            if (!description.DebugName.empty())
            {
                l_Pool.Heap->SetName(ToWide(description.DebugName).c_str());
            }

            return l_Pool;
        }

        std::uint64_t D3D12QueryPool::GetTimestampFrequency(ID3D12CommandQueue* queue)
        {
            UINT64 l_Frequency = 0;

            return queue && SUCCEEDED(queue->GetTimestampFrequency(&l_Frequency)) ? l_Frequency : 0;
        }

        D3D12Release D3D12QueryPool::ToRelease()
        {
            D3D12Release l_Release;
            l_Release.QueryHeap = std::move(Heap);

            return l_Release;
        }
    }
}
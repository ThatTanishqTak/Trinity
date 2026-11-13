#include "Trinity/RHI/D3D12/D3D12CommandAllocator.hpp"
#include "Trinity/RHI/D3D12/D3D12Device.hpp"

namespace Trinity
{
    namespace RHI
    {
        HRESULT D3D12CommandAllocator::Create(const D3D12Device& device)
        {
            return device.GetHandle()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&Allocator));
        }
    }
}
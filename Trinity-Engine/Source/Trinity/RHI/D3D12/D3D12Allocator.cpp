#include "Trinity/RHI/D3D12/D3D12Allocator.hpp"
#include "Trinity/RHI/D3D12/D3D12Device.hpp"
#include "Trinity/RHI/D3D12/D3D12Factory.hpp"
#include "Trinity/RHI/D3D12/D3D12Utilities.hpp"

#include "Trinity/Core/Memory.hpp"

#include <cstddef>
#include <format>
#include <string>

namespace Trinity
{
    namespace RHI
    {
        namespace
        {
            // D3D12MemoryAllocator's bookkeeping, counted under the Renderer tag, what the driver allocates is out of reach, since D3D12 takes no allocator
            void* AllocateHostMemory(std::size_t size, std::size_t alignment, [[maybe_unused]] void* userData)
            {
                return Memory::TryAllocate(size, MemoryTag::Renderer, alignment);
            }

            void FreeHostMemory(void* memory, [[maybe_unused]] void* userData)
            {
                Memory::Free(memory);
            }

            constexpr D3D12MA::ALLOCATION_CALLBACKS c_HostAllocator{ &AllocateHostMemory, &FreeHostMemory, nullptr };
        }

        D3D12Allocator::D3D12Allocator(const D3D12Factory& factory, const D3D12Device& device) : m_Factory(factory), m_Device(device)
        {

        }

        bool D3D12Allocator::Initialize(std::string& error)
        {
            D3D12MA::ALLOCATOR_DESC l_Description{};
            // D3D12MemoryAllocator's flag operators are global, and RHI's own operator| hides them in here
            l_Description.Flags = static_cast<D3D12MA::ALLOCATOR_FLAGS>(D3D12MA_RECOMMENDED_ALLOCATOR_FLAGS);
            l_Description.pDevice = m_Device.GetHandle();
            l_Description.pAdapter = m_Factory.GetAdapter();
            l_Description.pAllocationCallbacks = &c_HostAllocator;

            const HRESULT l_Result = D3D12MA::CreateAllocator(&l_Description, &m_Allocator);
            if (FAILED(l_Result))
            {
                error = std::format("D3D12MemoryAllocator could not be created on {} ({})", m_Device.GetInfo().AdapterName, FormatResult(l_Result));

                return false;
            }

            return true;
        }

        void D3D12Allocator::Reset()
        {
            m_Allocator.Reset();
        }
    }
}
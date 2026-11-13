#pragma once

#include "Trinity/RHI/D3D12/D3D12Headers.hpp"
#include "Trinity/RHI/Device.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace Trinity
{
    namespace RHI
    {
        class D3D12Device;
        class D3D12Factory;

        // D3D12MemoryAllocator on the device and its adapter, whose bookkeeping is counted under the Renderer tag
        class D3D12Allocator
        {
        public:
            D3D12Allocator(const D3D12Factory& factory, const D3D12Device& device);

            // False, with the reason in error, when D3D12MemoryAllocator cannot be created
            [[nodiscard]] bool Initialize(std::string& error);
            // Once every allocation made through it is released
            void Reset();
            // Fetches the budget for the frame, as D3D12MemoryAllocator asks to be told once a frame
            void BeginFrame(std::uint64_t frame);

            // Video memory and, on a discrete GPU, the system memory it reaches, with QueryVideoMemoryInfo's budget and use
            [[nodiscard]] std::vector<MemoryHeapBudget> GetMemoryBudget() const;

            [[nodiscard]] D3D12MA::Allocator* GetHandle() const { return m_Allocator.Get(); }

        private:
            const D3D12Factory& m_Factory;
            const D3D12Device& m_Device;

            Microsoft::WRL::ComPtr<D3D12MA::Allocator> m_Allocator;
        };
    }
}
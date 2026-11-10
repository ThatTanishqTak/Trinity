#pragma once

#include "Trinity/RHI/D3D12/D3D12Headers.hpp"

#include <string>

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

            [[nodiscard]] D3D12MA::Allocator* GetHandle() const { return m_Allocator.Get(); }

        private:
            const D3D12Factory& m_Factory;
            const D3D12Device& m_Device;

            Microsoft::WRL::ComPtr<D3D12MA::Allocator> m_Allocator;
        };
    }
}
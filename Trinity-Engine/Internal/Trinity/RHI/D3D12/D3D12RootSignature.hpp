#pragma once

#include "Trinity/RHI/D3D12/D3D12Headers.hpp"

#include <string>

namespace Trinity
{
    namespace RHI
    {
        class D3D12Device;

        // The root signature every pipeline shares: the push constants as 32 root constants, with shaders indexing the resource and sampler heaps directly
        class D3D12RootSignature
        {
        public:
            explicit D3D12RootSignature(const D3D12Device& device);

            // False, with the reason in error, when the root signature cannot be serialized or created
            [[nodiscard]] bool Initialize(std::string& error);
            void Reset();

            [[nodiscard]] ID3D12RootSignature* GetHandle() const { return m_RootSignature.Get(); }

        private:
            const D3D12Device& m_Device;

            Microsoft::WRL::ComPtr<ID3D12RootSignature> m_RootSignature;
        };
    }
}
#pragma once

#include "Trinity/Core/Base.hpp"
#include "Trinity/RHI/D3D12/D3D12Headers.hpp"
#include "Trinity/RHI/SwapChain.hpp"

#include <cstdint>
#include <vector>

namespace Trinity
{
    namespace RHI
    {
        class D3D12Device;

        class D3D12SwapChain final : public SwapChain
        {
        public:
            // Null, with the reason logged, when the window cannot be presented to
            [[nodiscard]] static Scope<D3D12SwapChain> Create(D3D12Device& device, const SwapChainSpecification& specification);

            D3D12SwapChain(D3D12Device& device, const SwapChainSpecification& specification);
            ~D3D12SwapChain() override;

            [[nodiscard]] TextureHandle AcquireNextTexture() override;
            void Present() override;

            void Resize(std::uint32_t width, std::uint32_t height) override;
            void SetVSync(bool enabled) override;

            [[nodiscard]] Format GetFormat() const override { return m_Format; }
            [[nodiscard]] std::uint32_t GetWidth() const override { return m_Width; }
            [[nodiscard]] std::uint32_t GetHeight() const override { return m_Height; }

        private:
            [[nodiscard]] bool Initialize();

            D3D12Device& m_Device;
            SwapChainSpecification m_Specification;

            Microsoft::WRL::ComPtr<IDXGISwapChain3> m_SwapChain;
            std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> m_Buffers;
            std::vector<TextureHandle> m_Textures;
            Format m_Format = Format::Unknown;
            std::uint32_t m_Width = 0;
            std::uint32_t m_Height = 0;
            bool m_Acquired = false;
            bool m_ReportedFailure = false;
        };
    }
}
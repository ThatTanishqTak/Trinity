#pragma once

#include "Trinity/RHI/D3D12/D3D12DescriptorHeap.hpp"
#include "Trinity/RHI/D3D12/D3D12Headers.hpp"
#include "Trinity/RHI/D3D12/D3D12ReleaseQueue.hpp"
#include "Trinity/RHI/Resources.hpp"
#include "Trinity/RHI/Types.hpp"

#include <cstdint>
#include <optional>

namespace Trinity
{
    namespace RHI
    {
        class D3D12Allocator;
        class D3D12Device;

        // Swap chain buffers have no allocation, since the swap chain owns them. A render target or depth texture has a view per mip of each layer, numbered by GetSubresourceIndex
        struct D3D12Texture
        {
            // Empty, with the reason logged, when the resource cannot be created. The context checks the description first
            [[nodiscard]] static std::optional<D3D12Texture> Create(const D3D12Device& device, const D3D12Allocator& allocator, D3D12DescriptorHeap& renderTargetViews, D3D12DescriptorHeap& depthStencilViews, D3D12DescriptorHeap& resourceHeap, const TextureDescription& description);
            // A swap chain buffer taken in as a texture with a single render target view
            [[nodiscard]] static D3D12Texture FromSwapChainBuffer(const D3D12Device& device, D3D12DescriptorHeap& renderTargetViews, D3D12DescriptorHeap& depthStencilViews, ID3D12Resource* resource, Format format, DXGI_FORMAT resourceFormat, std::uint32_t width, std::uint32_t height);
            [[nodiscard]] static bool IsFormatSupported(const D3D12Device& device, Format format, TextureUsage usage, std::uint32_t sampleCount);

            // What destroying it leaves for the release queue, which takes its allocation and views
            [[nodiscard]] D3D12Release ToRelease();

            Microsoft::WRL::ComPtr<D3D12MA::Allocation> Allocation;
            ID3D12Resource* Resource = nullptr;
            Format TextureFormat = Format::Unknown;
            DXGI_FORMAT ResourceFormat = DXGI_FORMAT_UNKNOWN;
            TextureDimension Dimension = TextureDimension::Texture2D;
            std::uint32_t Width = 0;
            std::uint32_t Height = 0;
            std::uint32_t MipLevels = 0;
            std::uint32_t ArrayLayers = 0;
            std::uint32_t SampleCount = 1;
            D3D12DescriptorIndices RenderTargetViews;
            D3D12DescriptorIndices DepthStencilViews;
            std::uint32_t ShaderResourceIndex = c_NoDescriptor;
            std::uint32_t UnorderedAccessIndex = c_NoDescriptor;
        };
    }
}
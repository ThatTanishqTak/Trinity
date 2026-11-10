#pragma once

#include "Trinity/Core/Memory.hpp"
#include "Trinity/RHI/D3D12/D3D12DescriptorHeap.hpp"
#include "Trinity/RHI/D3D12/D3D12Headers.hpp"
#include "Trinity/RHI/ReleaseQueue.hpp"

#include <cstdint>
#include <vector>

namespace Trinity
{
    namespace RHI
    {
        using D3D12DescriptorIndices = std::vector<std::uint32_t, TaggedAllocator<std::uint32_t, MemoryTag::Renderer>>;

        // What a destroyed buffer, texture, sampler, pipeline or query pool leaves to let go of, and the views and bindless indices it frees
        struct D3D12Release
        {
            Microsoft::WRL::ComPtr<D3D12MA::Allocation> Allocation;
            Microsoft::WRL::ComPtr<ID3D12PipelineState> Pipeline;
            Microsoft::WRL::ComPtr<ID3D12QueryHeap> QueryHeap;
            D3D12DescriptorIndices RenderTargetViews;
            D3D12DescriptorIndices DepthStencilViews;
            std::uint32_t ShaderResourceIndex = c_NoDescriptor;
            std::uint32_t UnorderedAccessIndex = c_NoDescriptor;
            std::uint32_t SamplerIndex = c_NoDescriptor;
        };

        // Destroyed objects kept until no frame in flight can still use them, then let go of with their views and bindless indices freed
        class D3D12ReleaseQueue
        {
        public:
            D3D12ReleaseQueue(D3D12DescriptorHeap& renderTargetViews, D3D12DescriptorHeap& depthStencilViews, D3D12DescriptorHeap& resourceHeap, D3D12DescriptorHeap& samplerHeap);

            void Push(D3D12Release release);
            // Once the frame c_FramesInFlight before this one has finished on the GPU
            void BeginFrame();
            void EndFrame();
            // Once the GPU is idle, keeping what the frame being recorded destroyed
            void ReleaseIdle();
            void ReleaseAll();

        private:
            void Release(D3D12Release& release);

            D3D12DescriptorHeap& m_RenderTargetViews;
            D3D12DescriptorHeap& m_DepthStencilViews;
            D3D12DescriptorHeap& m_ResourceHeap;
            D3D12DescriptorHeap& m_SamplerHeap;

            ReleaseQueue<D3D12Release> m_Releases;
        };
    }
}
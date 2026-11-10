#include "Trinity/RHI/D3D12/D3D12ReleaseQueue.hpp"

#include <utility>

namespace Trinity
{
    namespace RHI
    {
        D3D12ReleaseQueue::D3D12ReleaseQueue(D3D12DescriptorHeap& renderTargetViews, D3D12DescriptorHeap& depthStencilViews, D3D12DescriptorHeap& resourceHeap, D3D12DescriptorHeap& samplerHeap) : m_RenderTargetViews(renderTargetViews), m_DepthStencilViews(depthStencilViews), m_ResourceHeap(resourceHeap), m_SamplerHeap(samplerHeap)
        {

        }

        void D3D12ReleaseQueue::Push(D3D12Release release)
        {
            m_Releases.Push(std::move(release));
        }

        void D3D12ReleaseQueue::BeginFrame()
        {
            m_Releases.BeginFrame([this](D3D12Release& release) { Release(release); });
        }

        void D3D12ReleaseQueue::EndFrame()
        {
            m_Releases.EndFrame();
        }

        void D3D12ReleaseQueue::ReleaseIdle()
        {
            m_Releases.ReleaseIdle([this](D3D12Release& release) { Release(release); });
        }

        void D3D12ReleaseQueue::ReleaseAll()
        {
            m_Releases.ReleaseAll([this](D3D12Release& release) { Release(release); });
        }

        // An index is only handed out again once the frames that could read it have finished
        void D3D12ReleaseQueue::Release(D3D12Release& release)
        {
            for (const std::uint32_t it_View : release.RenderTargetViews)
            {
                m_RenderTargetViews.Free(it_View);
            }

            for (const std::uint32_t it_View : release.DepthStencilViews)
            {
                m_DepthStencilViews.Free(it_View);
            }

            m_ResourceHeap.Free(release.ShaderResourceIndex);
            m_ResourceHeap.Free(release.UnorderedAccessIndex);
            m_SamplerHeap.Free(release.SamplerIndex);
            release.Pipeline.Reset();
            release.QueryHeap.Reset();
            release.Allocation.Reset();
        }
    }
}
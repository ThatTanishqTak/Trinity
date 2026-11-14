#include "Trinity/RHI/D3D12/D3D12CommandList.hpp"
#include "Trinity/RHI/D3D12/D3D12Context.hpp"
#include "Trinity/RHI/D3D12/D3D12Device.hpp"
#include "Trinity/RHI/D3D12/D3D12Utilities.hpp"

#include "Trinity/Core/Assert.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Trinity
{
    namespace RHI
    {
        namespace
        {
            // The descriptor of one mip of one layer, or c_NoDescriptor when the texture has no such view
            std::uint32_t GetAttachmentView(const D3D12DescriptorIndices& views, const D3D12Texture& texture, std::uint32_t mipLevel, std::uint32_t arrayLayer)
            {
                if (mipLevel >= texture.MipLevels || arrayLayer >= texture.ArrayLayers)
                {
                    return c_NoDescriptor;
                }

                const std::size_t l_Index = GetSubresourceIndex(mipLevel, arrayLayer, texture.MipLevels);

                return l_Index < views.size() ? views[l_Index] : c_NoDescriptor;
            }

            struct D3D12State
            {
                D3D12_BARRIER_SYNC Sync = D3D12_BARRIER_SYNC_NONE;
                D3D12_BARRIER_ACCESS Access = D3D12_BARRIER_ACCESS_NO_ACCESS;
                D3D12_BARRIER_LAYOUT Layout = D3D12_BARRIER_LAYOUT_UNDEFINED;
            };

            D3D12State ToD3D12State(ResourceState state)
            {
                switch (state)
                {
                    case ResourceState::Undefined:
                    {
                        return { D3D12_BARRIER_SYNC_NONE, D3D12_BARRIER_ACCESS_NO_ACCESS, D3D12_BARRIER_LAYOUT_UNDEFINED };
                    }
                    case ResourceState::Present:
                    {
                        return { D3D12_BARRIER_SYNC_NONE, D3D12_BARRIER_ACCESS_NO_ACCESS, D3D12_BARRIER_LAYOUT_PRESENT };
                    }
                    case ResourceState::RenderTarget:
                    {
                        return { D3D12_BARRIER_SYNC_RENDER_TARGET, D3D12_BARRIER_ACCESS_RENDER_TARGET, D3D12_BARRIER_LAYOUT_RENDER_TARGET };
                    }
                    case ResourceState::ResolveDestination:
                    {
                        return { D3D12_BARRIER_SYNC_RESOLVE, D3D12_BARRIER_ACCESS_RESOLVE_DEST, D3D12_BARRIER_LAYOUT_RESOLVE_DEST };
                    }
                    case ResourceState::DepthWrite:
                    {
                        return { D3D12_BARRIER_SYNC_DEPTH_STENCIL, D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE };
                    }
                    case ResourceState::DepthRead:
                    {
                        return { D3D12_BARRIER_SYNC_DEPTH_STENCIL | D3D12_BARRIER_SYNC_ALL_SHADING, D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ | D3D12_BARRIER_ACCESS_SHADER_RESOURCE, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ };
                    }
                    case ResourceState::ShaderResource:
                    {
                        return { D3D12_BARRIER_SYNC_ALL_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE, D3D12_BARRIER_LAYOUT_SHADER_RESOURCE };
                    }
                    case ResourceState::UnorderedAccess:
                    {
                        return { D3D12_BARRIER_SYNC_ALL_SHADING, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS, D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS };
                    }
                    case ResourceState::CopySource:
                    {
                        return { D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_ACCESS_COPY_SOURCE, D3D12_BARRIER_LAYOUT_COPY_SOURCE };
                    }
                    case ResourceState::CopyDestination:
                    {
                        return { D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_ACCESS_COPY_DEST, D3D12_BARRIER_LAYOUT_COPY_DEST };
                    }
                    case ResourceState::IndexBuffer:
                    {
                        return { D3D12_BARRIER_SYNC_INDEX_INPUT, D3D12_BARRIER_ACCESS_INDEX_BUFFER, D3D12_BARRIER_LAYOUT_UNDEFINED };
                    }
                    case ResourceState::IndirectArgument:
                    {
                        return { D3D12_BARRIER_SYNC_EXECUTE_INDIRECT, D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT, D3D12_BARRIER_LAYOUT_UNDEFINED };
                    }
                    case ResourceState::Geometry:
                    {
                        return { D3D12_BARRIER_SYNC_ALL_SHADING | D3D12_BARRIER_SYNC_INDEX_INPUT, D3D12_BARRIER_ACCESS_SHADER_RESOURCE | D3D12_BARRIER_ACCESS_INDEX_BUFFER, D3D12_BARRIER_LAYOUT_UNDEFINED };
                    }
                }

                return {};
            }

            D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE ToBeginningAccess(LoadOp load)
            {
                switch (load)
                {
                    case LoadOp::Load:
                    {
                        return D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE;
                    }
                    case LoadOp::Clear:
                    {
                        return D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
                    }
                    default:
                    {
                        return D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_DISCARD;
                    }
                }
            }

            D3D12_RENDER_PASS_ENDING_ACCESS_TYPE ToEndingAccess(StoreOp store)
            {
                return store == StoreOp::Store ? D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE : D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_DISCARD;
            }

            // A footprint of a compressed format covers whole blocks, even where the mip edge cuts one
            UINT RoundUpToBlock(std::uint32_t size, std::uint32_t block)
            {
                return (size + block - 1) / block * block;
            }

            // Out of any other state than these, a resource may still be read by the frame before
            bool IsInUse(ResourceState state)
            {
                return state != ResourceState::Undefined && state != ResourceState::CopySource && state != ResourceState::CopyDestination;
            }
        }

        D3D12CommandList::D3D12CommandList(D3D12Context& device, bool uploads) : m_Device(device), m_Uploads(uploads)
        {

        }

        HRESULT D3D12CommandList::Initialize(const D3D12Device& device)
        {
            const HRESULT l_Result = device.GetHandle()->CreateCommandList1(0, m_Uploads ? D3D12_COMMAND_LIST_TYPE_COPY : D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&m_GraphicsList));
            if (SUCCEEDED(l_Result))
            {
                m_GraphicsList->SetName(m_Uploads ? L"Trinity upload commands" : L"Trinity frame commands");
            }

            return l_Result;
        }

        void D3D12CommandList::Reset()
        {
            m_GraphicsList.Reset();
        }

        void D3D12CommandList::Begin(ID3D12CommandAllocator* allocator)
        {
            allocator->Reset();
            m_GraphicsList->Reset(allocator, nullptr);

            m_CommandList = m_GraphicsList.Get();
            m_Rendering = false;
            m_HasPipeline = false;
            m_HasComputePipeline = false;
            m_HasIndexBuffer = false;
            m_HasCommands = false;
            m_WaitsForGraphics = false;
            m_TextureHandoffs.clear();
            if (m_Uploads)
            {
                return;
            }

            const std::array<ID3D12DescriptorHeap*, 2> l_Heaps{ m_Device.GetResourceHeap(), m_Device.GetSamplerHeap() };
            m_CommandList->SetDescriptorHeaps(static_cast<UINT>(l_Heaps.size()), l_Heaps.data());
            m_CommandList->SetGraphicsRootSignature(m_Device.GetRootSignature());
            m_CommandList->SetComputeRootSignature(m_Device.GetRootSignature());
            m_CommandList->OMSetStencilRef(0);
        }

        void D3D12CommandList::End()
        {
            TR_CORE_ASSERT(!m_Rendering, "The frame ended inside BeginRendering.");

            m_CommandList->Close();
            m_CommandList = nullptr;
        }

        void D3D12CommandList::RecordHandoffs(D3D12CommandList& frame)
        {
            if (!m_TextureHandoffs.empty())
            {
                D3D12_BARRIER_GROUP l_Group{};
                l_Group.Type = D3D12_BARRIER_TYPE_TEXTURE;
                l_Group.NumBarriers = static_cast<UINT32>(m_TextureHandoffs.size());
                l_Group.pTextureBarriers = m_TextureHandoffs.data();
                frame.m_CommandList->Barrier(1, &l_Group);
            }

            m_TextureHandoffs.clear();
        }

        // Moving out of Undefined discards the contents, which also counts as the first write a render target or depth texture needs. A copy queue makes no layout transitions and keeps a texture in the common layout it was created in, so an upload list records none, and a texture moves on from the common layout in the frame's list, which waits for the uploads
        void D3D12CommandList::TextureBarrier(TextureHandle texture, ResourceState before, ResourceState after, const TextureSubresourceRange& range)
        {
            TR_CORE_ASSERT(m_CommandList != nullptr && !m_Rendering, "Barriers are recorded within a frame and outside rendering.");
            TR_CORE_ASSERT(after != ResourceState::Undefined, "A texture cannot move into the undefined state.");

            const D3D12Texture* l_Texture = m_Device.GetTexture(texture);
            TR_CORE_ASSERT(l_Texture != nullptr, "TextureBarrier on a destroyed or invalid texture.");
            if (l_Texture == nullptr)
            {
                return;
            }

            TR_CORE_ASSERT(IsRangeInsideTexture(range, l_Texture->MipLevels, l_Texture->ArrayLayers), "TextureBarrier on mips or layers that a texture with {} mip(s) and {} layer(s) lacks.", l_Texture->MipLevels, l_Texture->ArrayLayers);

            const D3D12State l_Before = ToD3D12State(before);
            const D3D12State l_After = ToD3D12State(after);

            D3D12_TEXTURE_BARRIER l_Barrier{};
            l_Barrier.SyncBefore = l_Before.Sync;
            l_Barrier.SyncAfter = l_After.Sync;
            l_Barrier.AccessBefore = l_Before.Access;
            l_Barrier.AccessAfter = l_After.Access;
            l_Barrier.LayoutBefore = l_Before.Layout;
            l_Barrier.LayoutAfter = l_After.Layout;
            l_Barrier.pResource = l_Texture->Resource;
            if (range.IsWhole())
            {
                l_Barrier.Subresources.IndexOrFirstMipLevel = 0xFFFFFFFF;
            }
            else
            {
                l_Barrier.Subresources.IndexOrFirstMipLevel = range.BaseMipLevel;
                l_Barrier.Subresources.NumMipLevels = range.MipLevelCount == c_RemainingSubresources ? l_Texture->MipLevels - range.BaseMipLevel : range.MipLevelCount;
                l_Barrier.Subresources.FirstArraySlice = range.BaseArrayLayer;
                l_Barrier.Subresources.NumArraySlices = range.ArrayLayerCount == c_RemainingSubresources ? l_Texture->ArrayLayers - range.BaseArrayLayer : range.ArrayLayerCount;
                l_Barrier.Subresources.FirstPlane = 0;
                l_Barrier.Subresources.NumPlanes = HasStencil(l_Texture->TextureFormat) ? 2 : 1;
            }

            l_Barrier.Flags = before == ResourceState::Undefined ? D3D12_TEXTURE_BARRIER_FLAG_DISCARD : D3D12_TEXTURE_BARRIER_FLAG_NONE;
            if (m_Uploads)
            {
                TR_CORE_ASSERT(!IsInUse(before), "An uploaded texture is new, so it moves into a copy state from Undefined.");
                m_WaitsForGraphics = m_WaitsForGraphics || IsInUse(before);
                if (after != ResourceState::CopySource && after != ResourceState::CopyDestination)
                {
                    l_Barrier.SyncBefore = D3D12_BARRIER_SYNC_NONE;
                    l_Barrier.AccessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS;
                    l_Barrier.LayoutBefore = D3D12_BARRIER_LAYOUT_COMMON;
                    l_Barrier.Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE;
                    m_TextureHandoffs.push_back(l_Barrier);
                    m_HasCommands = true;
                }

                return;
            }

            D3D12_BARRIER_GROUP l_Group{};
            l_Group.Type = D3D12_BARRIER_TYPE_TEXTURE;
            l_Group.NumBarriers = 1;
            l_Group.pTextureBarriers = &l_Barrier;
            m_CommandList->Barrier(1, &l_Group);
        }

        // An upload list's copies need no barrier, and the fence orders them before the frame. Out of a state the frame before may be reading, the uploads wait for it
        void D3D12CommandList::BufferBarrier(BufferHandle buffer, ResourceState before, ResourceState after)
        {
            TR_CORE_ASSERT(m_CommandList != nullptr && !m_Rendering, "Barriers are recorded within a frame and outside rendering.");
            if (m_Uploads)
            {
                m_WaitsForGraphics = m_WaitsForGraphics || IsInUse(before);

                return;
            }

            const D3D12Buffer* l_Buffer = m_Device.GetBuffer(buffer);
            TR_CORE_ASSERT(l_Buffer != nullptr, "BufferBarrier on a destroyed or invalid buffer.");
            if (l_Buffer == nullptr)
            {
                return;
            }

            const D3D12State l_Before = ToD3D12State(before);
            const D3D12State l_After = ToD3D12State(after);

            D3D12_BUFFER_BARRIER l_Barrier{};
            l_Barrier.SyncBefore = l_Before.Sync;
            l_Barrier.SyncAfter = l_After.Sync;
            l_Barrier.AccessBefore = l_Before.Access;
            l_Barrier.AccessAfter = l_After.Access;
            l_Barrier.pResource = l_Buffer->Allocation->GetResource();
            l_Barrier.Offset = 0;
            l_Barrier.Size = UINT64_MAX;

            D3D12_BARRIER_GROUP l_Group{};
            l_Group.Type = D3D12_BARRIER_TYPE_BUFFER;
            l_Group.NumBarriers = 1;
            l_Group.pBufferBarriers = &l_Barrier;
            m_CommandList->Barrier(1, &l_Group);
        }

        // A render pass clears whole attachments, so the render area only matters to Vulkan
        void D3D12CommandList::BeginRendering(const RenderingDescription& description)
        {
            TR_CORE_ASSERT(m_CommandList != nullptr && !m_Rendering, "BeginRendering is called once within a frame, before EndRendering.");
            TR_CORE_ASSERT(!m_Uploads, "An upload list only copies.");
            TR_CORE_ASSERT(description.ColorAttachments.size() <= c_MaxColorAttachments, "Too many color attachments.");

            std::array<D3D12_RENDER_PASS_RENDER_TARGET_DESC, c_MaxColorAttachments> l_Targets{};
            UINT l_TargetCount = 0;
            for (const ColorAttachment& it_Attachment : description.ColorAttachments)
            {
                const D3D12Texture* l_Texture = m_Device.GetTexture(it_Attachment.Texture);
                const std::uint32_t l_View = l_Texture != nullptr ? GetAttachmentView(l_Texture->RenderTargetViews, *l_Texture, it_Attachment.MipLevel, it_Attachment.ArrayLayer) : c_NoDescriptor;
                TR_CORE_ASSERT(l_View != c_NoDescriptor, "BeginRendering with a destroyed texture, one without RenderTarget usage, or a mip or layer it lacks.");
                if (l_View == c_NoDescriptor || l_TargetCount == c_MaxColorAttachments)
                {
                    continue;
                }

                D3D12_RENDER_PASS_RENDER_TARGET_DESC& l_Target = l_Targets[l_TargetCount++];
                l_Target.cpuDescriptor = m_Device.GetRenderTargetView(l_View);
                l_Target.BeginningAccess.Type = ToBeginningAccess(it_Attachment.Load);
                l_Target.BeginningAccess.Clear.ClearValue.Format = ToDXGIFormat(l_Texture->TextureFormat);
                for (std::size_t it_Channel = 0; it_Channel < it_Attachment.ClearColor.size(); ++it_Channel)
                {
                    l_Target.BeginningAccess.Clear.ClearValue.Color[it_Channel] = it_Attachment.ClearColor[it_Channel];
                }

                l_Target.EndingAccess.Type = ToEndingAccess(it_Attachment.Store);
                if (it_Attachment.ResolveTexture)
                {
                    const D3D12Texture* l_Resolve = m_Device.GetTexture(it_Attachment.ResolveTexture);
                    const bool l_Resolvable = l_Resolve != nullptr && !l_Resolve->RenderTargetViews.empty() && l_Texture->SampleCount > 1 && l_Resolve->SampleCount == 1 && l_Resolve->TextureFormat == l_Texture->TextureFormat && l_Resolve->TextureFormat != Format::R32Uint;
                    TR_CORE_ASSERT(l_Resolvable, "BeginRendering resolving into a destroyed texture, one without RenderTarget usage, or one whose format or sample count does not fit.");
                    if (l_Resolvable)
                    {
                        D3D12_RENDER_PASS_ENDING_ACCESS_RESOLVE_SUBRESOURCE_PARAMETERS& l_Parameters = m_ResolveParameters[l_TargetCount - 1];
                        l_Parameters = {};
                        l_Parameters.SrcRect = { 0, 0, static_cast<LONG>(l_Texture->Width), static_cast<LONG>(l_Texture->Height) };

                        l_Target.EndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_RESOLVE;
                        l_Target.EndingAccess.Resolve.pSrcResource = l_Texture->Resource;
                        l_Target.EndingAccess.Resolve.pDstResource = l_Resolve->Resource;
                        l_Target.EndingAccess.Resolve.SubresourceCount = 1;
                        l_Target.EndingAccess.Resolve.pSubresourceParameters = &l_Parameters;
                        l_Target.EndingAccess.Resolve.Format = ToDXGIFormat(l_Texture->TextureFormat);
                        l_Target.EndingAccess.Resolve.ResolveMode = D3D12_RESOLVE_MODE_AVERAGE;
                        l_Target.EndingAccess.Resolve.PreserveResolveSource = it_Attachment.Store == StoreOp::Store ? TRUE : FALSE;
                    }
                }
            }

            D3D12_RENDER_PASS_DEPTH_STENCIL_DESC l_Depth{};
            const D3D12Texture* l_DepthTexture = description.Depth.Texture ? m_Device.GetTexture(description.Depth.Texture) : nullptr;
            const std::uint32_t l_DepthView = l_DepthTexture != nullptr ? GetAttachmentView(l_DepthTexture->DepthStencilViews, *l_DepthTexture, description.Depth.MipLevel, description.Depth.ArrayLayer) : c_NoDescriptor;
            TR_CORE_ASSERT(!description.Depth.Texture || l_DepthView != c_NoDescriptor, "BeginRendering with a destroyed depth texture, one without DepthStencil usage, or a mip or layer it lacks.");
            const bool l_HasDepth = l_DepthView != c_NoDescriptor;
            if (l_HasDepth)
            {
                l_Depth.cpuDescriptor = m_Device.GetDepthStencilView(l_DepthView);
                l_Depth.DepthBeginningAccess.Type = ToBeginningAccess(description.Depth.Load);
                l_Depth.DepthBeginningAccess.Clear.ClearValue.Format = ToDXGIFormat(l_DepthTexture->TextureFormat);
                l_Depth.DepthBeginningAccess.Clear.ClearValue.DepthStencil = { description.Depth.ClearDepth, description.Depth.ClearStencil };
                l_Depth.DepthEndingAccess.Type = ToEndingAccess(description.Depth.Store);
                if (HasStencil(l_DepthTexture->TextureFormat))
                {
                    l_Depth.StencilBeginningAccess = l_Depth.DepthBeginningAccess;
                    l_Depth.StencilEndingAccess.Type = l_Depth.DepthEndingAccess.Type;
                }
                else
                {
                    l_Depth.StencilBeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_NO_ACCESS;
                    l_Depth.StencilEndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_NO_ACCESS;
                }
                l_Depth.StencilEndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_NO_ACCESS;
            }

            m_CommandList->BeginRenderPass(l_TargetCount, l_Targets.data(), l_HasDepth ? &l_Depth : nullptr, D3D12_RENDER_PASS_FLAG_NONE);
            m_Rendering = true;
            m_HasComputePipeline = false;
        }

        void D3D12CommandList::EndRendering()
        {
            TR_CORE_ASSERT(m_Rendering, "EndRendering without BeginRendering.");

            m_CommandList->EndRenderPass();
            m_Rendering = false;
            m_HasPipeline = false;
            m_HasIndexBuffer = false;
        }

        // Graphics and compute share one pipeline state slot, so setting either replaces the other
        void D3D12CommandList::SetPipeline(PipelineHandle pipeline)
        {
            TR_CORE_ASSERT(m_CommandList != nullptr, "SetPipeline is recorded within a frame.");
            TR_CORE_ASSERT(!m_Uploads, "An upload list only copies.");

            const D3D12Pipeline* l_Pipeline = m_Device.GetPipeline(pipeline);
            TR_CORE_ASSERT(l_Pipeline != nullptr, "SetPipeline with a destroyed or invalid pipeline.");
            if (l_Pipeline == nullptr)
            {
                return;
            }

            TR_CORE_ASSERT(l_Pipeline->Compute != m_Rendering, "A graphics pipeline is set inside rendering, and a compute pipeline outside it.");

            m_CommandList->SetPipelineState(l_Pipeline->State.Get());
            if (l_Pipeline->Compute)
            {
                m_HasComputePipeline = true;
            }
            else
            {
                m_CommandList->IASetPrimitiveTopology(l_Pipeline->Topology);
                m_HasPipeline = true;
            }
        }

        void D3D12CommandList::SetViewport(const Viewport& viewport)
        {
            TR_CORE_ASSERT(m_Rendering, "SetViewport is recorded inside rendering.");

            const D3D12_VIEWPORT l_Viewport{ viewport.X, viewport.Y, viewport.Width, viewport.Height, viewport.MinDepth, viewport.MaxDepth };
            m_CommandList->RSSetViewports(1, &l_Viewport);
        }

        void D3D12CommandList::SetScissor(const Rect& scissor)
        {
            TR_CORE_ASSERT(m_Rendering, "SetScissor is recorded inside rendering.");

            const D3D12_RECT l_Scissor{ static_cast<LONG>(scissor.X), static_cast<LONG>(scissor.Y), static_cast<LONG>(scissor.X) + static_cast<LONG>(scissor.Width), static_cast<LONG>(scissor.Y) + static_cast<LONG>(scissor.Height) };
            m_CommandList->RSSetScissorRects(1, &l_Scissor);
        }

        void D3D12CommandList::SetStencilReference(std::uint32_t reference)
        {
            TR_CORE_ASSERT(m_CommandList != nullptr, "SetStencilReference is recorded within a frame.");

            m_CommandList->OMSetStencilRef(reference);
        }

        // The push constants are root constants at b0, the first parameter of the shared root signature. Graphics and compute keep their own, so they go to whichever pipeline the commands are recorded for
        void D3D12CommandList::PushConstants(std::span<const std::byte> data)
        {
            TR_CORE_ASSERT(m_Rendering ? m_HasPipeline : m_HasComputePipeline, "PushConstants needs a graphics pipeline inside rendering, or a compute pipeline outside it.");
            TR_CORE_ASSERT(data.size() <= c_MaxPushConstantSize && data.size() % 4 == 0, "Push constants are whole 32-bit values, at most c_MaxPushConstantSize bytes.");

            if (m_Rendering)
            {
                m_CommandList->SetGraphicsRoot32BitConstants(0, static_cast<UINT>(data.size() / 4), data.data(), 0);
            }
            else
            {
                m_CommandList->SetComputeRoot32BitConstants(0, static_cast<UINT>(data.size() / 4), data.data(), 0);
            }
        }

        void D3D12CommandList::Draw(std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex, std::uint32_t firstInstance)
        {
            TR_CORE_ASSERT(m_HasPipeline, "Draw needs a pipeline.");

            m_CommandList->DrawInstanced(vertexCount, instanceCount, firstVertex, firstInstance);
        }

        void D3D12CommandList::SetIndexBuffer(BufferHandle buffer, std::uint64_t offset, IndexFormat format)
        {
            TR_CORE_ASSERT(m_Rendering, "SetIndexBuffer is recorded inside rendering.");

            const D3D12Buffer* l_Buffer = m_Device.GetBuffer(buffer);
            TR_CORE_ASSERT(l_Buffer != nullptr, "SetIndexBuffer with a destroyed or invalid buffer.");
            TR_CORE_ASSERT(l_Buffer == nullptr || (offset < l_Buffer->Size && offset % GetIndexSize(format) == 0), "SetIndexBuffer at offset {}, which is past the end of the buffer or not a multiple of the index size.", offset);
            if (l_Buffer == nullptr || offset >= l_Buffer->Size)
            {
                return;
            }

            D3D12_INDEX_BUFFER_VIEW l_View{};
            l_View.BufferLocation = l_Buffer->Allocation->GetResource()->GetGPUVirtualAddress() + offset;
            l_View.SizeInBytes = static_cast<UINT>(std::min<std::uint64_t>(l_Buffer->Size - offset, UINT_MAX));
            l_View.Format = format == IndexFormat::UInt16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
            m_CommandList->IASetIndexBuffer(&l_View);
            m_HasIndexBuffer = true;
        }

        void D3D12CommandList::DrawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex, std::uint32_t firstInstance)
        {
            TR_CORE_ASSERT(m_HasPipeline && m_HasIndexBuffer, "DrawIndexed needs a pipeline and an index buffer.");

            m_CommandList->DrawIndexedInstanced(indexCount, instanceCount, firstIndex, 0, firstInstance);
        }

        void D3D12CommandList::Dispatch(std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ)
        {
            TR_CORE_ASSERT(m_CommandList != nullptr && !m_Rendering && m_HasComputePipeline, "Dispatch is recorded outside rendering, with a compute pipeline set.");

            m_CommandList->Dispatch(groupCountX, groupCountY, groupCountZ);
        }

        void D3D12CommandList::WriteTimestamp(QueryPoolHandle pool, std::uint32_t index)
        {
            TR_CORE_ASSERT(m_CommandList != nullptr, "WriteTimestamp is recorded within a frame.");

            const D3D12QueryPool* l_Pool = m_Device.GetQueryPool(pool);
            TR_CORE_ASSERT(l_Pool != nullptr && index < l_Pool->Count, "WriteTimestamp with a destroyed query pool, or an index past its end.");
            if (l_Pool == nullptr || index >= l_Pool->Count)
            {
                return;
            }

            m_CommandList->EndQuery(l_Pool->Heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index);
        }

        void D3D12CommandList::ResolveTimestamps(QueryPoolHandle pool, std::uint32_t first, std::uint32_t count, BufferHandle destination, std::uint64_t destinationOffset)
        {
            TR_CORE_ASSERT(m_CommandList != nullptr && !m_Rendering, "ResolveTimestamps is recorded within a frame and outside rendering.");

            const D3D12QueryPool* l_Pool = m_Device.GetQueryPool(pool);
            const D3D12Buffer* l_Buffer = m_Device.GetBuffer(destination);
            TR_CORE_ASSERT(l_Pool != nullptr && l_Buffer != nullptr, "ResolveTimestamps with a destroyed query pool or buffer.");
            if (l_Pool == nullptr || l_Buffer == nullptr)
            {
                return;
            }

            TR_CORE_ASSERT(count != 0 && std::uint64_t{ first } + count <= l_Pool->Count, "ResolveTimestamps with a range past the end of a pool of {}.", l_Pool->Count);
            TR_CORE_ASSERT(destinationOffset % 8 == 0 && destinationOffset + std::uint64_t{ count } * 8 <= l_Buffer->Size, "ResolveTimestamps at an offset that is not a multiple of 8, or past the end of the buffer.");

            m_CommandList->ResolveQueryData(l_Pool->Heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, first, count, l_Buffer->Allocation->GetResource(), destinationOffset);
        }

        void D3D12CommandList::CopyBuffer(BufferHandle source, std::uint64_t sourceOffset, BufferHandle destination, std::uint64_t destinationOffset, std::uint64_t size)
        {
            TR_CORE_ASSERT(m_CommandList != nullptr && !m_Rendering, "Copies are recorded within a frame and outside rendering.");
            m_HasCommands = true;

            const D3D12Buffer* l_Source = m_Device.GetBuffer(source);
            const D3D12Buffer* l_Destination = m_Device.GetBuffer(destination);
            TR_CORE_ASSERT(l_Source != nullptr && l_Destination != nullptr, "CopyBuffer with a destroyed or invalid buffer.");
            if (l_Source == nullptr || l_Destination == nullptr)
            {
                return;
            }

            TR_CORE_ASSERT(sourceOffset + size <= l_Source->Size && destinationOffset + size <= l_Destination->Size, "CopyBuffer reaches past the end of a buffer.");

            m_CommandList->CopyBufferRegion(l_Destination->Allocation->GetResource(), destinationOffset, l_Source->Allocation->GetResource(), sourceOffset, size);
        }

        // The whole mip, laid out in the buffer as CopyBufferToTexture reads it: rows of texels or blocks GetTextureCopyRowPitch apart
        void D3D12CommandList::CopyTextureToBuffer(TextureHandle source, std::uint32_t mipLevel, std::uint32_t arrayLayer, BufferHandle destination, std::uint64_t destinationOffset)
        {
            TR_CORE_ASSERT(m_CommandList != nullptr && !m_Rendering, "Copies are recorded within a frame and outside rendering.");
            m_HasCommands = true;

            const D3D12Texture* l_Texture = m_Device.GetTexture(source);
            const D3D12Buffer* l_Buffer = m_Device.GetBuffer(destination);
            TR_CORE_ASSERT(l_Texture != nullptr && l_Buffer != nullptr, "CopyTextureToBuffer with a destroyed or invalid resource.");
            if (l_Texture == nullptr || l_Buffer == nullptr)
            {
                return;
            }

            const std::uint32_t l_Width = GetMipSize(l_Texture->Width, mipLevel);
            const std::uint32_t l_Height = GetMipSize(l_Texture->Height, mipLevel);
            const std::uint64_t l_RowPitch = GetTextureCopyRowPitch(l_Texture->TextureFormat, l_Width);
            [[maybe_unused]] const std::uint64_t l_Size = GetTextureCopySize(l_Texture->TextureFormat, l_Width, l_Height);
            TR_CORE_ASSERT(mipLevel < l_Texture->MipLevels && arrayLayer < l_Texture->ArrayLayers, "CopyTextureToBuffer from mip {} of layer {} of a texture with {} mip(s) and {} layer(s).", mipLevel, arrayLayer, l_Texture->MipLevels, l_Texture->ArrayLayers);
            TR_CORE_ASSERT(destinationOffset % c_TextureCopyOffsetAlignment == 0, "CopyTextureToBuffer writes to offset {}, which is not a multiple of {}.", destinationOffset, c_TextureCopyOffsetAlignment);
            TR_CORE_ASSERT(destinationOffset + l_Size <= l_Buffer->Size, "CopyTextureToBuffer needs {} bytes from offset {}, and the buffer has {}.", l_Size, destinationOffset, l_Buffer->Size);

            D3D12_TEXTURE_COPY_LOCATION l_Source{};
            l_Source.pResource = l_Texture->Resource;
            l_Source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            l_Source.SubresourceIndex = GetSubresourceIndex(mipLevel, arrayLayer, l_Texture->MipLevels);

            const std::uint32_t l_Block = GetFormatBlockDimension(l_Texture->TextureFormat);
            D3D12_TEXTURE_COPY_LOCATION l_Destination{};
            l_Destination.pResource = l_Buffer->Allocation->GetResource();
            l_Destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            l_Destination.PlacedFootprint.Offset = destinationOffset;
            l_Destination.PlacedFootprint.Footprint.Format = l_Texture->ResourceFormat;
            l_Destination.PlacedFootprint.Footprint.Width = RoundUpToBlock(l_Width, l_Block);
            l_Destination.PlacedFootprint.Footprint.Height = RoundUpToBlock(l_Height, l_Block);
            l_Destination.PlacedFootprint.Footprint.Depth = 1;
            l_Destination.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(l_RowPitch);

            m_CommandList->CopyTextureRegion(&l_Destination, 0, 0, 0, &l_Source, nullptr);
        }

        void D3D12CommandList::CopyBufferToTexture(BufferHandle source, std::uint64_t sourceOffset, TextureHandle destination, std::uint32_t mipLevel, std::uint32_t arrayLayer, const Rect& region)
        {
            TR_CORE_ASSERT(m_CommandList != nullptr && !m_Rendering, "Copies are recorded within a frame and outside rendering.");
            m_HasCommands = true;

            const D3D12Buffer* l_Buffer = m_Device.GetBuffer(source);
            const D3D12Texture* l_Texture = m_Device.GetTexture(destination);
            TR_CORE_ASSERT(l_Buffer != nullptr && l_Texture != nullptr, "CopyBufferToTexture with a destroyed or invalid resource.");
            if (l_Buffer == nullptr || l_Texture == nullptr)
            {
                return;
            }

            const std::uint64_t l_RowPitch = GetTextureCopyRowPitch(l_Texture->TextureFormat, region.Width);
            [[maybe_unused]] const std::uint64_t l_Size = GetTextureCopySize(l_Texture->TextureFormat, region.Width, region.Height);
            TR_CORE_ASSERT(mipLevel < l_Texture->MipLevels && IsRegionInsideMip(region, l_Texture->Width, l_Texture->Height, mipLevel), "CopyBufferToTexture with a region outside mip {} of a {}x{} texture with {} mip(s).", mipLevel, l_Texture->Width, l_Texture->Height, l_Texture->MipLevels);
            TR_CORE_ASSERT(arrayLayer < l_Texture->ArrayLayers, "CopyBufferToTexture into layer {} of a texture with {} layer(s).", arrayLayer, l_Texture->ArrayLayers);
            TR_CORE_ASSERT(IsRegionBlockAligned(region, l_Texture->TextureFormat, l_Texture->Width, l_Texture->Height, mipLevel), "CopyBufferToTexture with a region of {} that does not start on a block or cover whole blocks.", ToString(l_Texture->TextureFormat));
            TR_CORE_ASSERT(sourceOffset % c_TextureCopyOffsetAlignment == 0, "CopyBufferToTexture reads from offset {}, which is not a multiple of {}.", sourceOffset, c_TextureCopyOffsetAlignment);
            TR_CORE_ASSERT(sourceOffset + l_Size <= l_Buffer->Size, "CopyBufferToTexture needs {} bytes from offset {}, and the buffer has {}.", l_Size, sourceOffset, l_Buffer->Size);

            const std::uint32_t l_Block = GetFormatBlockDimension(l_Texture->TextureFormat);
            D3D12_TEXTURE_COPY_LOCATION l_Source{};
            l_Source.pResource = l_Buffer->Allocation->GetResource();
            l_Source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            l_Source.PlacedFootprint.Offset = sourceOffset;
            l_Source.PlacedFootprint.Footprint.Format = l_Texture->ResourceFormat;
            l_Source.PlacedFootprint.Footprint.Width = RoundUpToBlock(region.Width, l_Block);
            l_Source.PlacedFootprint.Footprint.Height = RoundUpToBlock(region.Height, l_Block);
            l_Source.PlacedFootprint.Footprint.Depth = 1;
            l_Source.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(l_RowPitch);

            D3D12_TEXTURE_COPY_LOCATION l_Destination{};
            l_Destination.pResource = l_Texture->Resource;
            l_Destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            l_Destination.SubresourceIndex = GetSubresourceIndex(mipLevel, arrayLayer, l_Texture->MipLevels);

            m_CommandList->CopyTextureRegion(&l_Destination, static_cast<UINT>(region.X), static_cast<UINT>(region.Y), 0, &l_Source, nullptr);
        }

        // PIX's Unicode encoding, metadata 0 and a null-terminated wide string, which PIX and RenderDoc read with no package
        void D3D12CommandList::BeginLabel(std::string_view name)
        {
            TR_CORE_ASSERT(m_CommandList != nullptr, "BeginLabel is recorded within a frame.");

            const std::wstring l_Name = ToWide(name);
            m_CommandList->BeginEvent(0, l_Name.c_str(), static_cast<UINT>((l_Name.size() + 1) * sizeof(wchar_t)));
        }

        void D3D12CommandList::EndLabel()
        {
            TR_CORE_ASSERT(m_CommandList != nullptr, "EndLabel is recorded within a frame.");

            m_CommandList->EndEvent();
        }
    }
}
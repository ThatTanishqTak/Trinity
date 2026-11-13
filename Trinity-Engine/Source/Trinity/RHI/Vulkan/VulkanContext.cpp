#include "Trinity/RHI/Vulkan/VulkanContext.hpp"
#include "Trinity/RHI/Vulkan/VulkanSwapChain.hpp"
#include "Trinity/RHI/Vulkan/VulkanUtilities.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"

#include <optional>
#include <string>

namespace Trinity
{
    namespace RHI
    {
        Scope<VulkanContext> VulkanContext::Create(const DeviceSpecification& specification, std::string& error)
        {
            Scope<VulkanContext> l_Device = CreateScope<VulkanContext>();
            if (!l_Device->Initialize(specification, error))
            {
                return nullptr;
            }

            return l_Device;
        }

        VulkanContext::VulkanContext() : m_Device(m_Instance), m_Allocator(m_Instance, m_Device), m_Bindless(m_Device), m_Releases(m_Device, m_Allocator, m_Bindless), m_FrameSync(m_Device), m_CommandList(*this)
        {

        }

        VulkanContext::~VulkanContext()
        {
            const bool l_Report = m_Instance.IsValidationEnabled() && m_Device.GetHandle() != VK_NULL_HANDLE;

            // The validation layer reports objects still alive in vkDestroyDevice and vkDestroyInstance, so the messenger outlives the device
            if (m_Device.GetHandle() != VK_NULL_HANDLE)
            {
                m_Device.WaitIdle();
                m_UploadRing.Shutdown();

                if (m_Buffers.GetCount() != 0 || m_Textures.GetCount() != 0 || m_Pipelines.GetCount() != 0 || m_Samplers.GetCount() != 0 || m_QueryPools.GetCount() != 0)
                {
                    TR_CORE_WARN("Vulkan: the device was destroyed with {} buffer(s), {} texture(s), {} pipeline(s), {} sampler(s) and {} query pool(s) still alive", m_Buffers.GetCount(), m_Textures.GetCount(), m_Pipelines.GetCount(), m_Samplers.GetCount(), m_QueryPools.GetCount());
                }

                m_Releases.ReleaseAll();
                m_Buffers.ForEach([this](const VulkanBuffer& buffer) { m_Releases.Release(buffer.ToRelease()); });
                m_Textures.ForEach([this](const VulkanTexture& texture) { m_Releases.Release(texture.ToRelease()); });
                m_Pipelines.ForEach([this](const VulkanPipeline& pipeline) { m_Releases.Release(pipeline.ToRelease()); });
                m_Samplers.ForEach([this](const VulkanSampler& sampler) { m_Releases.Release(sampler.ToRelease()); });
                m_QueryPools.ForEach([this](const VulkanQueryPool& pool) { m_Releases.Release(pool.ToRelease()); });

                m_Bindless.Destroy();

                for (const VulkanCommandPool& it_Pool : m_CommandPools)
                {
                    it_Pool.Destroy(m_Device);
                }

                m_FrameSync.Destroy();
                m_Allocator.Destroy();
                m_Device.Destroy();
            }

            m_Instance.Destroy();

            if (l_Report)
            {
                TR_CORE_INFO("Vulkan validation: {} warning(s) or error(s) while the device lived", m_Instance.GetMessageCount());
            }
        }

        bool VulkanContext::Initialize(const DeviceSpecification& specification, std::string& error)
        {
            return m_Instance.Initialize(specification, error) && m_Device.Initialize(error) && m_Allocator.Initialize(error) && CreateFrames(error) && m_Bindless.Initialize(error) && m_UploadRing.Initialize(*this, error);
        }

        bool VulkanContext::CreateFrames(std::string& error)
        {
            VkResult l_Result = m_FrameSync.Initialize();
            for (VulkanCommandPool& it_Pool : m_CommandPools)
            {
                if (l_Result != VK_SUCCESS)
                {
                    break;
                }


                l_Result = it_Pool.Create(m_Device);
            }

            if (l_Result != VK_SUCCESS)
            {
                error = std::format("the frame command pools and timeline semaphore could not be created on {} ({})", m_Device.GetInfo().AdapterName, FormatResult(l_Result));

                return false;
            }

            return true;
        }

        BufferHandle VulkanContext::CreateBuffer(const BufferDescription& description)
        {
            const std::optional<VulkanBuffer> l_Buffer = VulkanBuffer::Create(m_Device, m_Allocator, m_Bindless, description);
            if (!l_Buffer)
            {
                return {};
            }

            return m_Buffers.Add(*l_Buffer);
        }

        void VulkanContext::DestroyBuffer(BufferHandle buffer)
        {
            if (!buffer)
            {
                return;
            }

            const std::optional<VulkanBuffer> l_Buffer = m_Buffers.Remove(buffer);
            TR_CORE_ASSERT(l_Buffer.has_value(), "DestroyBuffer on a buffer that was already destroyed.");

            if (l_Buffer)
            {
                m_Releases.Push(l_Buffer->ToRelease());
            }
        }

        std::span<std::byte> VulkanContext::GetMappedData(BufferHandle buffer)
        {
            const VulkanBuffer* l_Buffer = m_Buffers.Get(buffer);
            TR_CORE_ASSERT(l_Buffer != nullptr, "GetMappedData on a destroyed or invalid buffer.");

            if (l_Buffer == nullptr || l_Buffer->Mapped == nullptr)
            {
                return {};
            }

            return { l_Buffer->Mapped, static_cast<std::size_t>(l_Buffer->Size) };
        }

        bool VulkanContext::IsFormatSupported(Format format, TextureUsage usage, std::uint32_t sampleCount) const
        {
            return VulkanTexture::IsFormatSupported(m_Device, format, usage, sampleCount);
        }

        TextureHandle VulkanContext::CreateTexture(const TextureDescription& description)
        {
            TR_CORE_ASSERT(description.Width != 0 && description.Height != 0 && description.MipLevels != 0, "Texture '{}' has a zero size or no mips.", description.DebugName);
            TR_CORE_ASSERT(description.TextureFormat != Format::Unknown, "Texture '{}' has no format.", description.DebugName);

            if (!CanCreateTexture(description))
            {
                return {};
            }

            std::optional<VulkanTexture> l_Texture = VulkanTexture::Create(m_Device, m_Allocator, m_Bindless, description);
            if (!l_Texture)
            {
                return {};
            }

            return m_Textures.Add(std::move(*l_Texture));
        }

        void VulkanContext::DestroyTexture(TextureHandle texture)
        {
            if (!texture)
            {
                return;
            }

            const std::optional<VulkanTexture> l_Texture = m_Textures.Remove(texture);
            TR_CORE_ASSERT(l_Texture.has_value(), "DestroyTexture on a texture that was already destroyed.");

            if (l_Texture)
            {
                m_Releases.Push(l_Texture->ToRelease());
            }
        }

        TextureHandle VulkanContext::AddSwapChainImage(VkImage image, Format format, std::uint32_t width, std::uint32_t height)
        {
            return m_Textures.Add(VulkanTexture::FromSwapChainImage(m_Device, image, format, width, height));
        }

        // The swap chain waits for the device to be idle first, so the view goes at once
        void VulkanContext::RemoveSwapChainImage(TextureHandle texture)
        {
            const std::optional<VulkanTexture> l_Texture = m_Textures.Remove(texture);
            if (l_Texture)
            {
                l_Texture->DestroySwapChainView(m_Device);
            }
        }

        void VulkanContext::WaitForSemaphore(VkSemaphore semaphore)
        {
            m_FrameSync.WaitForSemaphore(semaphore);
        }

        void VulkanContext::SignalSemaphore(VkSemaphore semaphore)
        {
            m_FrameSync.SignalSemaphore(semaphore);
        }

        std::uint32_t VulkanContext::GetShaderResourceIndex(BufferHandle buffer)
        {
            const VulkanBuffer* l_Buffer = m_Buffers.Get(buffer);
            TR_CORE_ASSERT(l_Buffer != nullptr, "GetShaderResourceIndex on a destroyed or invalid buffer.");

            return l_Buffer != nullptr ? l_Buffer->ShaderResourceIndex : c_NoBindlessIndex;
        }

        std::uint32_t VulkanContext::GetUnorderedAccessIndex(BufferHandle buffer)
        {
            const VulkanBuffer* l_Buffer = m_Buffers.Get(buffer);
            TR_CORE_ASSERT(l_Buffer != nullptr, "GetUnorderedAccessIndex on a destroyed or invalid buffer.");

            return l_Buffer != nullptr ? l_Buffer->UnorderedAccessIndex : c_NoBindlessIndex;
        }

        std::uint32_t VulkanContext::GetShaderResourceIndex(TextureHandle texture)
        {
            const VulkanTexture* l_Texture = m_Textures.Get(texture);
            TR_CORE_ASSERT(l_Texture != nullptr, "GetShaderResourceIndex on a destroyed or invalid texture.");

            return l_Texture != nullptr ? l_Texture->ShaderResourceIndex : c_NoBindlessIndex;
        }

        std::uint32_t VulkanContext::GetUnorderedAccessIndex(TextureHandle texture)
        {
            const VulkanTexture* l_Texture = m_Textures.Get(texture);
            TR_CORE_ASSERT(l_Texture != nullptr, "GetUnorderedAccessIndex on a destroyed or invalid texture.");

            return l_Texture != nullptr ? l_Texture->UnorderedAccessIndex : c_NoBindlessIndex;
        }

        SamplerHandle VulkanContext::CreateSampler(const SamplerDescription& description)
        {
            const std::optional<VulkanSampler> l_Sampler = VulkanSampler::Create(m_Device, m_Bindless, description);
            if (!l_Sampler)
            {
                return {};
            }

            return m_Samplers.Add(*l_Sampler);
        }

        void VulkanContext::DestroySampler(SamplerHandle sampler)
        {
            if (!sampler)
            {
                return;
            }

            const std::optional<VulkanSampler> l_Sampler = m_Samplers.Remove(sampler);
            TR_CORE_ASSERT(l_Sampler.has_value(), "DestroySampler on a sampler that was already destroyed.");

            if (l_Sampler)
            {
                m_Releases.Push(l_Sampler->ToRelease());
            }
        }

        std::uint32_t VulkanContext::GetSamplerIndex(SamplerHandle sampler)
        {
            const VulkanSampler* l_Sampler = m_Samplers.Get(sampler);
            TR_CORE_ASSERT(l_Sampler != nullptr, "GetSamplerIndex on a destroyed or invalid sampler.");

            return l_Sampler != nullptr ? l_Sampler->Index : c_NoBindlessIndex;
        }

        PipelineHandle VulkanContext::CreateGraphicsPipeline(const GraphicsPipelineDescription& description)
        {
            const std::optional<VulkanPipeline> l_Pipeline = VulkanPipeline::CreateGraphics(m_Device, m_Bindless, description);
            if (!l_Pipeline)
            {
                return {};
            }


            return m_Pipelines.Add(*l_Pipeline);
        }

        QueryPoolHandle VulkanContext::CreateQueryPool(const QueryPoolDescription& description)
        {
            const std::optional<VulkanQueryPool> l_Pool = VulkanQueryPool::Create(m_Device, description);
            if (!l_Pool)
            {
                return {};
            }

            return m_QueryPools.Add(*l_Pool);
        }

        void VulkanContext::DestroyQueryPool(QueryPoolHandle pool)
        {
            if (!pool)
            {
                return;
            }

            const std::optional<VulkanQueryPool> l_Pool = m_QueryPools.Remove(pool);
            TR_CORE_ASSERT(l_Pool.has_value(), "DestroyQueryPool on a query pool that was already destroyed.");

            if (l_Pool)
            {
                m_Releases.Push(l_Pool->ToRelease());
            }
        }

        std::uint64_t VulkanContext::GetTimestampFrequency() const
        {
            return VulkanQueryPool::GetTimestampFrequency(m_Device);
        }

        PipelineHandle VulkanContext::CreateComputePipeline(const ComputePipelineDescription& description)
        {
            const std::optional<VulkanPipeline> l_Pipeline = VulkanPipeline::CreateCompute(m_Device, m_Bindless, description);
            if (!l_Pipeline)
            {
                return {};
            }

            return m_Pipelines.Add(*l_Pipeline);
        }

        void VulkanContext::DestroyPipeline(PipelineHandle pipeline)
        {
            if (!pipeline)
            {
                return;
            }

            const std::optional<VulkanPipeline> l_Pipeline = m_Pipelines.Remove(pipeline);
            TR_CORE_ASSERT(l_Pipeline.has_value(), "DestroyPipeline on a pipeline that was already destroyed.");

            if (l_Pipeline)
            {
                m_Releases.Push(l_Pipeline->ToRelease());
            }
        }

        Scope<SwapChain> VulkanContext::CreateSwapChain(const SwapChainSpecification& specification)
        {
            return VulkanSwapChain::Create(*this, specification);
        }

        // Waits until the frame c_FramesInFlight before this one, which used the same command pool, has finished on the GPU
        CommandList& VulkanContext::BeginFrame()
        {
            TR_CORE_ASSERT(!m_InFrame, "BeginFrame was called twice without EndFrame.");

            const VulkanCommandPool& l_Pool = m_CommandPools[GetFrameSlot()];
            m_FrameSync.WaitForTimeline(l_Pool.CompletionValue);

            m_InFrame = true;
            m_Releases.BeginFrame();
            m_UploadRing.BeginFrame();

            l_Pool.Reset(m_Device);
            m_FrameSync.BeginFrame();
            m_CommandList.Begin(l_Pool.CommandBuffer);

            return m_CommandList;
        }

        void VulkanContext::EndFrame()
        {
            TR_CORE_ASSERT(m_InFrame, "EndFrame without BeginFrame.");

            VulkanCommandPool& l_Pool = m_CommandPools[GetFrameSlot()];
            m_UploadRing.EndFrame();
            m_CommandList.End();

            l_Pool.CompletionValue = m_FrameNumber + 1;
            const VkResult l_Result = m_FrameSync.Submit(l_Pool.CommandBuffer, l_Pool.CompletionValue);
            if (l_Result != VK_SUCCESS)
            {
                TR_CORE_ERROR("Vulkan: frame {} could not be submitted ({})", m_FrameNumber, FormatResult(l_Result));
            }

            ++m_FrameNumber;
            m_Releases.EndFrame();
            m_InFrame = false;
        }

        void VulkanContext::WaitIdle()
        {
            if (m_Device.GetHandle() != VK_NULL_HANDLE)
            {
                m_Device.WaitIdle();
                m_Releases.ReleaseIdle();
            }
        }
    }
}
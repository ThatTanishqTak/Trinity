#include "Trinity/RHI/Vulkan/VulkanContext.hpp"
#include "Trinity/RHI/Vulkan/VulkanSwapChain.hpp"
#include "Trinity/RHI/Vulkan/VulkanUtilities.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Trinity
{
    namespace RHI
    {
        namespace
        {
            std::string_view ToString(VkResult result)
            {
                switch (result)
                {
                    case VK_SUCCESS:
                    {
                        return "VK_SUCCESS";
                    }
                    case VK_ERROR_OUT_OF_HOST_MEMORY:
                    {
                        return "VK_ERROR_OUT_OF_HOST_MEMORY";
                    }
                    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
                    {
                        return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
                    }
                    case VK_ERROR_INITIALIZATION_FAILED:
                    {
                        return "VK_ERROR_INITIALIZATION_FAILED";
                    }
                    case VK_ERROR_LAYER_NOT_PRESENT:
                    {
                        return "VK_ERROR_LAYER_NOT_PRESENT";
                    }
                    case VK_ERROR_EXTENSION_NOT_PRESENT:
                    {
                        return "VK_ERROR_EXTENSION_NOT_PRESENT";
                    }
                    case VK_ERROR_FEATURE_NOT_PRESENT:
                    {
                        return "VK_ERROR_FEATURE_NOT_PRESENT";
                    }
                    case VK_ERROR_INCOMPATIBLE_DRIVER:
                    {
                        return "VK_ERROR_INCOMPATIBLE_DRIVER";
                    }
                    case VK_ERROR_DEVICE_LOST:
                    {
                        return "VK_ERROR_DEVICE_LOST";
                    }
                    default:
                    {
                        return "another VkResult";
                    }
                }
            }
        }

        VkFormat ToVkFormat(Format format)
        {
            switch (format)
            {
                case Format::RGBA8Unorm:
                {
                    return VK_FORMAT_R8G8B8A8_UNORM;
                }
                case Format::RGBA8Srgb:
                {
                    return VK_FORMAT_R8G8B8A8_SRGB;
                }
                case Format::BGRA8Unorm:
                {
                    return VK_FORMAT_B8G8R8A8_UNORM;
                }
                case Format::BGRA8Srgb:
                {
                    return VK_FORMAT_B8G8R8A8_SRGB;
                }
                case Format::RGBA16Float:
                {
                    return VK_FORMAT_R16G16B16A16_SFLOAT;
                }
                case Format::R11G11B10Float:
                {
                    return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
                }
                case Format::R32Float:
                {
                    return VK_FORMAT_R32_SFLOAT;
                }
                case Format::R32Uint:
                {
                    return VK_FORMAT_R32_UINT;
                }
                case Format::RG32Float:
                {
                    return VK_FORMAT_R32G32_SFLOAT;
                }
                case Format::RGB32Float:
                {
                    return VK_FORMAT_R32G32B32_SFLOAT;
                }
                case Format::RGBA32Float:
                {
                    return VK_FORMAT_R32G32B32A32_SFLOAT;
                }
                case Format::D32Float:
                {
                    return VK_FORMAT_D32_SFLOAT;
                }
                case Format::D32FloatS8:
                {
                    return VK_FORMAT_D32_SFLOAT_S8_UINT;
                }
                case Format::BC1Unorm:
                {
                    return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
                }
                case Format::BC1Srgb:
                {
                    return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
                }
                case Format::BC3Unorm:
                {
                    return VK_FORMAT_BC3_UNORM_BLOCK;
                }
                case Format::BC3Srgb:
                {
                    return VK_FORMAT_BC3_SRGB_BLOCK;
                }
                case Format::BC4Unorm:
                {
                    return VK_FORMAT_BC4_UNORM_BLOCK;
                }
                case Format::BC5Unorm:
                {
                    return VK_FORMAT_BC5_UNORM_BLOCK;
                }
                case Format::BC7Unorm:
                {
                    return VK_FORMAT_BC7_UNORM_BLOCK;
                }
                case Format::BC7Srgb:
                {
                    return VK_FORMAT_BC7_SRGB_BLOCK;
                }
                default:
                {
                    return VK_FORMAT_UNDEFINED;
                }
            }
        }

        std::string FormatResult(VkResult result)
        {
            return std::format("{} ({})", ToString(result), static_cast<int>(result));
        }

        VkImageAspectFlags GetAspect(Format format)
        {
            if (HasStencil(format))
            {
                return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            }

            return IsDepthFormat(format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        }

        std::string FormatVersion(std::uint32_t version)
        {
            return std::format("{}.{}.{}", VK_API_VERSION_MAJOR(version), VK_API_VERSION_MINOR(version), VK_API_VERSION_PATCH(version));
        }

        bool HasExtension(const std::vector<VkExtensionProperties>& extensions, std::string_view name)
        {
            return std::ranges::any_of(extensions, [name](const VkExtensionProperties& extension) { return name == extension.extensionName; });
        }

        Scope<VulkanContext> VulkanContext::Create(const DeviceSpecification& specification, std::string& error)
        {
            Scope<VulkanContext> l_Device = CreateScope<VulkanContext>();
            if (!l_Device->Initialize(specification, error))
            {
                return nullptr;
            }

            return l_Device;
        }

        VulkanContext::VulkanContext() : m_Device(m_Instance), m_Allocator(m_Instance, m_Device), m_Bindless(m_Device), m_Releases(m_Device, m_Allocator, m_Bindless), m_CommandList(*this)
        {

        }

        VulkanContext::~VulkanContext()
        {
            const bool l_Report = m_Instance.IsValidationEnabled() && m_Device.GetHandle() != VK_NULL_HANDLE;

            // The validation layer reports objects still alive in vkDestroyDevice and vkDestroyInstance, so the messenger outlives the device
            if (m_Device.GetHandle() != VK_NULL_HANDLE)
            {
                vkDeviceWaitIdle(m_Device.GetHandle());
                m_UploadRing.Shutdown();

                if (m_Buffers.GetCount() != 0 || m_Textures.GetCount() != 0 || m_Pipelines.GetCount() != 0 || m_Samplers.GetCount() != 0 || m_QueryPools.GetCount() != 0)
                {
                    TR_CORE_WARN("Vulkan: the device was destroyed with {} buffer(s), {} texture(s), {} pipeline(s), {} sampler(s) and {} query pool(s) still alive", m_Buffers.GetCount(), m_Textures.GetCount(), m_Pipelines.GetCount(), m_Samplers.GetCount(), m_QueryPools.GetCount());
                }

                m_Releases.ReleaseAll();
                m_Buffers.ForEach([this](const VulkanBuffer& buffer) { m_Releases.Release(buffer.ToRelease()); });
                m_Textures.ForEach([this](const VulkanTexture& texture) { m_Releases.Release(texture.ToRelease()); });
                m_Pipelines.ForEach([this](const VulkanPipeline& pipeline) { vkDestroyPipeline(m_Device.GetHandle(), pipeline.Pipeline, nullptr); });
                m_Samplers.ForEach([this](const VulkanSampler& sampler) { m_Releases.Release(sampler.ToRelease()); });
                m_QueryPools.ForEach([this](const VulkanQueryPool& pool) { vkDestroyQueryPool(m_Device.GetHandle(), pool.Pool, nullptr); });

                m_Bindless.Destroy();

                for (const FrameContext& it_Frame : m_Frames)
                {
                    vkDestroyCommandPool(m_Device.GetHandle(), it_Frame.CommandPool, nullptr);
                }

                vkDestroySemaphore(m_Device.GetHandle(), m_FrameTimeline, nullptr);

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
            VkSemaphoreTypeCreateInfo l_Timeline = MakeInfo<VkSemaphoreTypeCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO);
            l_Timeline.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;

            VkSemaphoreCreateInfo l_Semaphore = MakeInfo<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
            l_Semaphore.pNext = &l_Timeline;

            VkResult l_Result = vkCreateSemaphore(m_Device.GetHandle(), &l_Semaphore, nullptr, &m_FrameTimeline);
            for (FrameContext& it_Frame : m_Frames)
            {
                if (l_Result != VK_SUCCESS)
                {
                    break;
                }

                VkCommandPoolCreateInfo l_Pool = MakeInfo<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
                l_Pool.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
                l_Pool.queueFamilyIndex = m_Device.GetQueueFamily();
                l_Result = vkCreateCommandPool(m_Device.GetHandle(), &l_Pool, nullptr, &it_Frame.CommandPool);
                if (l_Result != VK_SUCCESS)
                {
                    break;
                }

                VkCommandBufferAllocateInfo l_Allocate = MakeInfo<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
                l_Allocate.commandPool = it_Frame.CommandPool;
                l_Allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                l_Allocate.commandBufferCount = 1;
                l_Result = vkAllocateCommandBuffers(m_Device.GetHandle(), &l_Allocate, &it_Frame.CommandBuffer);
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
            VkSemaphoreSubmitInfo l_Wait = MakeInfo<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
            l_Wait.semaphore = semaphore;
            l_Wait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            m_FrameWaits.push_back(l_Wait);
        }

        void VulkanContext::SignalSemaphore(VkSemaphore semaphore)
        {
            VkSemaphoreSubmitInfo l_Signal = MakeInfo<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
            l_Signal.semaphore = semaphore;
            l_Signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            m_FrameSignals.push_back(l_Signal);
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

            FrameContext& l_Frame = m_Frames[GetFrameSlot()];
            if (l_Frame.CompletionValue != 0)
            {
                VkSemaphoreWaitInfo l_Wait = MakeInfo<VkSemaphoreWaitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO);
                l_Wait.semaphoreCount = 1;
                l_Wait.pSemaphores = &m_FrameTimeline;
                l_Wait.pValues = &l_Frame.CompletionValue;
                vkWaitSemaphores(m_Device.GetHandle(), &l_Wait, UINT64_MAX);
            }

            m_InFrame = true;
            m_Releases.BeginFrame();
            m_UploadRing.BeginFrame();

            vkResetCommandPool(m_Device.GetHandle(), l_Frame.CommandPool, 0);
            m_FrameWaits.clear();
            m_FrameSignals.clear();
            m_CommandList.Begin(l_Frame.CommandBuffer);

            return m_CommandList;
        }

        void VulkanContext::EndFrame()
        {
            TR_CORE_ASSERT(m_InFrame, "EndFrame without BeginFrame.");

            FrameContext& l_Frame = m_Frames[GetFrameSlot()];
            m_UploadRing.EndFrame();
            m_CommandList.End();

            l_Frame.CompletionValue = m_FrameNumber + 1;
            SignalSemaphore(m_FrameTimeline);
            m_FrameSignals.back().value = l_Frame.CompletionValue;

            VkCommandBufferSubmitInfo l_Commands = MakeInfo<VkCommandBufferSubmitInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO);
            l_Commands.commandBuffer = l_Frame.CommandBuffer;

            VkSubmitInfo2 l_Submit = MakeInfo<VkSubmitInfo2>(VK_STRUCTURE_TYPE_SUBMIT_INFO_2);
            l_Submit.waitSemaphoreInfoCount = static_cast<std::uint32_t>(m_FrameWaits.size());
            l_Submit.pWaitSemaphoreInfos = m_FrameWaits.data();
            l_Submit.commandBufferInfoCount = 1;
            l_Submit.pCommandBufferInfos = &l_Commands;
            l_Submit.signalSemaphoreInfoCount = static_cast<std::uint32_t>(m_FrameSignals.size());
            l_Submit.pSignalSemaphoreInfos = m_FrameSignals.data();

            const VkResult l_Result = vkQueueSubmit2(m_Device.GetQueue(), 1, &l_Submit, VK_NULL_HANDLE);
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
                vkDeviceWaitIdle(m_Device.GetHandle());
                m_Releases.ReleaseIdle();
            }
        }
    }
}
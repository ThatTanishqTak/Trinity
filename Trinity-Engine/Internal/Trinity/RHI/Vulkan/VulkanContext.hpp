#pragma once

#include "Trinity/Core/Memory.hpp"
#include "Trinity/RHI/Bindless.hpp"
#include "Trinity/RHI/Device.hpp"
#include "Trinity/RHI/HandlePool.hpp"
#include "Trinity/RHI/ReleaseQueue.hpp"
#include "Trinity/RHI/Vulkan/VulkanCommandList.hpp"
#include "Trinity/RHI/Vulkan/VulkanDevice.hpp"
#include "Trinity/RHI/Vulkan/VulkanHeaders.hpp"
#include "Trinity/RHI/Vulkan/VulkanInstance.hpp"

#include <array>
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
        struct VulkanBuffer
        {
            VkBuffer Buffer = VK_NULL_HANDLE;
            VmaAllocation Allocation = nullptr;
            std::byte* Mapped = nullptr;
            std::uint64_t Size = 0;
            std::uint32_t ShaderResourceIndex = c_NoBindlessIndex;
            std::uint32_t UnorderedAccessIndex = c_NoBindlessIndex;
        };

        using VulkanImageViews = std::vector<VkImageView, TaggedAllocator<VkImageView, MemoryTag::Renderer>>;

        // Swap chain images have no allocation, since the swap chain owns them. A render target or depth texture has an attachment view per mip of each layer, numbered by GetSubresourceIndex
        struct VulkanTexture
        {
            VkImage Image = VK_NULL_HANDLE;
            VmaAllocation Allocation = nullptr;
            VulkanImageViews AttachmentViews;
            VkImageView SampledView = VK_NULL_HANDLE;
            VkImageView StorageView = VK_NULL_HANDLE;
            Format TextureFormat = Format::Unknown;
            TextureDimension Dimension = TextureDimension::Texture2D;
            std::uint32_t Width = 0;
            std::uint32_t Height = 0;
            std::uint32_t MipLevels = 0;
            std::uint32_t ArrayLayers = 0;
            std::uint32_t SampleCount = 1;
            std::uint32_t ShaderResourceIndex = c_NoBindlessIndex;
            std::uint32_t UnorderedAccessIndex = c_NoBindlessIndex;
        };

        struct VulkanPipeline
        {
            VkPipeline Pipeline = VK_NULL_HANDLE;
            VkPipelineBindPoint BindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        };

        struct VulkanQueryPool
        {
            VkQueryPool Pool = VK_NULL_HANDLE;
            std::uint32_t Count = 0;
        };

        struct VulkanSampler
        {
            VkSampler Sampler = VK_NULL_HANDLE;
            std::uint32_t Index = c_NoBindlessIndex;
        };

        class VulkanContext final : public Device
        {
        public:
            // Null, with the reason in error, when no physical device can run the engine
            [[nodiscard]] static Scope<VulkanContext> Create(const DeviceSpecification& specification, std::string& error);

            VulkanContext();
            ~VulkanContext() override;

            [[nodiscard]] const DeviceInfo& GetInfo() const override { return m_Device.GetInfo(); }

            [[nodiscard]] BufferHandle CreateBuffer(const BufferDescription& description) override;
            void DestroyBuffer(BufferHandle buffer) override;
            [[nodiscard]] std::span<std::byte> GetMappedData(BufferHandle buffer) override;

            [[nodiscard]] bool IsFormatSupported(Format format, TextureUsage usage, std::uint32_t sampleCount) const override;
            [[nodiscard]] TextureHandle CreateTexture(const TextureDescription& description) override;
            void DestroyTexture(TextureHandle texture) override;

            [[nodiscard]] std::uint32_t GetShaderResourceIndex(BufferHandle buffer) override;
            [[nodiscard]] std::uint32_t GetUnorderedAccessIndex(BufferHandle buffer) override;
            [[nodiscard]] std::uint32_t GetShaderResourceIndex(TextureHandle texture) override;
            [[nodiscard]] std::uint32_t GetUnorderedAccessIndex(TextureHandle texture) override;

            [[nodiscard]] SamplerHandle CreateSampler(const SamplerDescription& description) override;
            void DestroySampler(SamplerHandle sampler) override;
            [[nodiscard]] std::uint32_t GetSamplerIndex(SamplerHandle sampler) override;

            [[nodiscard]] QueryPoolHandle CreateQueryPool(const QueryPoolDescription& description) override;
            void DestroyQueryPool(QueryPoolHandle pool) override;
            [[nodiscard]] std::uint64_t GetTimestampFrequency() const override;

            [[nodiscard]] PipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDescription& description) override;
            [[nodiscard]] PipelineHandle CreateComputePipeline(const ComputePipelineDescription& description) override;
            void DestroyPipeline(PipelineHandle pipeline) override;

            [[nodiscard]] Scope<SwapChain> CreateSwapChain(const SwapChainSpecification& specification) override;

            [[nodiscard]] CommandList& BeginFrame() override;
            void EndFrame() override;
            void WaitIdle() override;

            [[nodiscard]] VulkanBuffer* GetBuffer(BufferHandle buffer) { return m_Buffers.Get(buffer); }
            [[nodiscard]] VulkanTexture* GetTexture(TextureHandle texture) { return m_Textures.Get(texture); }
            [[nodiscard]] VulkanPipeline* GetPipeline(PipelineHandle pipeline) { return m_Pipelines.Get(pipeline); }
            [[nodiscard]] VulkanQueryPool* GetQueryPool(QueryPoolHandle pool) { return m_QueryPools.Get(pool); }
            [[nodiscard]] VkPipelineLayout GetPipelineLayout() const { return m_PipelineLayout; }
            [[nodiscard]] VkDescriptorSet GetBindlessSet() const { return m_BindlessSet; }

            // For the swap chain: its images become textures, and its semaphores join this frame's submission
            [[nodiscard]] TextureHandle AddSwapChainImage(VkImage image, Format format, std::uint32_t width, std::uint32_t height);
            void RemoveSwapChainImage(TextureHandle texture);
            void WaitForSemaphore(VkSemaphore semaphore);
            void SignalSemaphore(VkSemaphore semaphore);

            [[nodiscard]] VkInstance GetInstance() const { return m_Instance.GetHandle(); }
            [[nodiscard]] VkPhysicalDevice GetPhysicalDevice() const { return m_Device.GetPhysicalDevice(); }
            [[nodiscard]] VkDevice GetHandle() const { return m_Device.GetHandle(); }
            [[nodiscard]] VkQueue GetQueue() const { return m_Device.GetQueue(); }
            [[nodiscard]] std::uint32_t GetQueueFamily() const { return m_Device.GetQueueFamily(); }
            [[nodiscard]] std::uint32_t GetFrameSlot() const { return static_cast<std::uint32_t>(m_FrameNumber % c_FramesInFlight); }
            [[nodiscard]] bool IsInFrame() const { return m_InFrame; }

            void SetDebugName(VkObjectType type, std::uint64_t handle, std::string_view name) const { m_Device.SetDebugName(type, handle, name); }

        private:
            struct VulkanRelease
            {
                VkBuffer Buffer = VK_NULL_HANDLE;
                VkImage Image = VK_NULL_HANDLE;
                VulkanImageViews Views;
                VmaAllocation Allocation = nullptr;
                VkImageView SampledView = VK_NULL_HANDLE;
                VkPipeline Pipeline = VK_NULL_HANDLE;
                VkSampler Sampler = VK_NULL_HANDLE;
                VkQueryPool QueryPool = VK_NULL_HANDLE;
                std::uint32_t ShaderResourceIndex = c_NoBindlessIndex;
                std::uint32_t UnorderedAccessIndex = c_NoBindlessIndex;
                std::uint32_t SamplerIndex = c_NoBindlessIndex;
            };

            struct FrameContext
            {
                VkCommandPool CommandPool = VK_NULL_HANDLE;
                VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
                std::uint64_t CompletionValue = 0;
            };

            [[nodiscard]] bool Initialize(const DeviceSpecification& specification, std::string& error);
            [[nodiscard]] bool CreateAllocator(std::string& error);
            [[nodiscard]] bool CreateFrames(std::string& error);
            [[nodiscard]] bool CreateBindless(std::string& error);

            [[nodiscard]] VkImageView CreateView(VkImage image, Format format, VkImageViewType type, std::uint32_t baseMipLevel, std::uint32_t mipLevels, std::uint32_t baseArrayLayer, std::uint32_t arrayLayers);
            [[nodiscard]] std::uint32_t AddBufferDescriptor(VkBuffer buffer);
            [[nodiscard]] std::uint32_t AddImageDescriptor(std::uint32_t binding, VkDescriptorType type, VkImageView view, VkImageLayout layout);
            [[nodiscard]] static VulkanRelease ToRelease(const VulkanBuffer& buffer);
            [[nodiscard]] static VulkanRelease ToRelease(const VulkanTexture& texture);
            [[nodiscard]] static VulkanRelease ToRelease(const VulkanSampler& sampler);
            void Release(const VulkanRelease& release);

            VulkanInstance m_Instance;
            VulkanDevice m_Device;
            VmaAllocator m_Allocator = nullptr;

            HandlePool<VulkanBuffer, BufferHandle> m_Buffers;
            HandlePool<VulkanTexture, TextureHandle> m_Textures;
            HandlePool<VulkanPipeline, PipelineHandle> m_Pipelines;
            HandlePool<VulkanSampler, SamplerHandle> m_Samplers;
            HandlePool<VulkanQueryPool, QueryPoolHandle> m_QueryPools;
            ReleaseQueue<VulkanRelease> m_Releases;

            VkDescriptorSetLayout m_BindlessLayout = VK_NULL_HANDLE;
            VkDescriptorPool m_BindlessPool = VK_NULL_HANDLE;
            VkDescriptorSet m_BindlessSet = VK_NULL_HANDLE;
            VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
            IndexAllocator m_ResourceIndices;
            IndexAllocator m_SamplerIndices;

            std::array<FrameContext, c_FramesInFlight> m_Frames{};
            VkSemaphore m_FrameTimeline = VK_NULL_HANDLE;
            std::uint64_t m_FrameNumber = 0;
            std::vector<VkSemaphoreSubmitInfo> m_FrameWaits;
            std::vector<VkSemaphoreSubmitInfo> m_FrameSignals;

            VulkanCommandList m_CommandList;
            bool m_InFrame = false;
        };
    }
}
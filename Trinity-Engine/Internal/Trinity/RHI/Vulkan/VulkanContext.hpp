#pragma once

#include "Trinity/Core/Memory.hpp"
#include "Trinity/RHI/Bindless.hpp"
#include "Trinity/RHI/Device.hpp"
#include "Trinity/RHI/HandlePool.hpp"
#include "Trinity/RHI/Vulkan/VulkanAllocator.hpp"
#include "Trinity/RHI/Vulkan/VulkanBindless.hpp"
#include "Trinity/RHI/Vulkan/VulkanBuffer.hpp"
#include "Trinity/RHI/Vulkan/VulkanCommandList.hpp"
#include "Trinity/RHI/Vulkan/VulkanCommandPool.hpp"
#include "Trinity/RHI/Vulkan/VulkanDevice.hpp"
#include "Trinity/RHI/Vulkan/VulkanFrameSync.hpp"
#include "Trinity/RHI/Vulkan/VulkanHeaders.hpp"
#include "Trinity/RHI/Vulkan/VulkanInstance.hpp"
#include "Trinity/RHI/Vulkan/VulkanPipeline.hpp"
#include "Trinity/RHI/Vulkan/VulkanQueryPool.hpp"
#include "Trinity/RHI/Vulkan/VulkanReleaseQueue.hpp"
#include "Trinity/RHI/Vulkan/VulkanSampler.hpp"
#include "Trinity/RHI/Vulkan/VulkanTexture.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Trinity
{
    namespace RHI
    {
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
            [[nodiscard]] VkPipelineLayout GetPipelineLayout() const { return m_Bindless.GetPipelineLayout(); }
            [[nodiscard]] VkDescriptorSet GetBindlessSet() const { return m_Bindless.GetSet(); }

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
            [[nodiscard]] bool Initialize(const DeviceSpecification& specification, std::string& error);
            [[nodiscard]] bool CreateFrames(std::string& error);

            VulkanInstance m_Instance;
            VulkanDevice m_Device;
            VulkanAllocator m_Allocator;

            HandlePool<VulkanBuffer, BufferHandle> m_Buffers;
            HandlePool<VulkanTexture, TextureHandle> m_Textures;
            HandlePool<VulkanPipeline, PipelineHandle> m_Pipelines;
            HandlePool<VulkanSampler, SamplerHandle> m_Samplers;
            HandlePool<VulkanQueryPool, QueryPoolHandle> m_QueryPools;

            VulkanBindless m_Bindless;
            VulkanReleaseQueue m_Releases;

            std::array<VulkanCommandPool, c_FramesInFlight> m_CommandPools{};
            VulkanFrameSync m_FrameSync;
            std::uint64_t m_FrameNumber = 0;

            VulkanCommandList m_CommandList;
            bool m_InFrame = false;
        };
    }
}
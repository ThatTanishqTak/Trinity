#pragma once

#include "Trinity/RHI/Device.hpp"
#include "Trinity/RHI/HandlePool.hpp"
#include "Trinity/RHI/ReleaseQueue.hpp"
#include "Trinity/RHI/Vulkan/VulkanHeaders.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Trinity
{
    namespace RHI
    {
        class VulkanCommandList final : public CommandList
        {
        public:
            VulkanCommandList() = default;

            void TextureBarrier(TextureHandle texture, ResourceState before, ResourceState after) override;
            void BufferBarrier(BufferHandle buffer, ResourceState before, ResourceState after) override;

            void BeginRendering(const RenderingDescription& description) override;
            void EndRendering() override;

            void SetPipeline(PipelineHandle pipeline) override;
            void SetViewport(const Viewport& viewport) override;
            void SetScissor(const Rect& scissor) override;
            void PushConstants(std::span<const std::byte> data) override;
            void Draw(std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex, std::uint32_t firstInstance) override;

            void CopyBuffer(BufferHandle source, std::uint64_t sourceOffset, BufferHandle destination, std::uint64_t destinationOffset, std::uint64_t size) override;
            void CopyTextureToBuffer(TextureHandle source, BufferHandle destination) override;
        };

        class VulkanDevice final : public Device
        {
        public:
            // Null, with the reason in error, when no physical device can run the engine
            [[nodiscard]] static Scope<VulkanDevice> Create(const DeviceSpecification& specification, std::string& error);

            VulkanDevice() = default;
            ~VulkanDevice() override;

            [[nodiscard]] const DeviceInfo& GetInfo() const override { return m_Info; }

            [[nodiscard]] BufferHandle CreateBuffer(const BufferDescription& description) override;
            void DestroyBuffer(BufferHandle buffer) override;
            [[nodiscard]] std::span<std::byte> GetMappedData(BufferHandle buffer) override;

            [[nodiscard]] TextureHandle CreateTexture(const TextureDescription& description) override;
            void DestroyTexture(TextureHandle texture) override;

            [[nodiscard]] PipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDescription& description) override;
            void DestroyPipeline(PipelineHandle pipeline) override;

            [[nodiscard]] Scope<SwapChain> CreateSwapChain(const SwapChainSpecification& specification) override;

            [[nodiscard]] CommandList& BeginFrame() override;
            void EndFrame() override;
            void WaitIdle() override;

        private:
            struct VulkanBuffer
            {
                VkBuffer Buffer = VK_NULL_HANDLE;
                VmaAllocation Allocation = nullptr;
                std::byte* Mapped = nullptr;
                std::uint64_t Size = 0;
            };

            struct VulkanTexture
            {
                VkImage Image = VK_NULL_HANDLE;
                VmaAllocation Allocation = nullptr;
                VkFormat ImageFormat = VK_FORMAT_UNDEFINED;
                std::uint32_t Width = 0;
                std::uint32_t Height = 0;
                std::uint32_t MipLevels = 0;
            };

            struct VulkanRelease
            {
                VkBuffer Buffer = VK_NULL_HANDLE;
                VkImage Image = VK_NULL_HANDLE;
                VmaAllocation Allocation = nullptr;
            };

            static VKAPI_ATTR VkBool32 VKAPI_CALL OnDebugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT types, const VkDebugUtilsMessengerCallbackDataEXT* data, void* context);

            [[nodiscard]] bool Initialize(const DeviceSpecification& specification, std::string& error);
            [[nodiscard]] bool CreateInstance(const DeviceSpecification& specification, std::uint32_t loaderVersion, std::string& error);
            [[nodiscard]] bool CreateLogicalDevice(std::string& error);
            [[nodiscard]] bool CreateAllocator(std::string& error);

            void Release(const VulkanRelease& release);
            void SetDebugName(VkObjectType type, std::uint64_t handle, std::string_view name) const;

            DeviceInfo m_Info;
            bool m_Validation = false;
            std::atomic<std::uint32_t> m_MessageCount{ 0 };

            VkInstance m_Instance = VK_NULL_HANDLE;
            VkDebugUtilsMessengerEXT m_Messenger = VK_NULL_HANDLE;
            VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
            VkDevice m_Device = VK_NULL_HANDLE;
            VkQueue m_Queue = VK_NULL_HANDLE;
            std::uint32_t m_QueueFamily = 0;
            VmaAllocator m_Allocator = nullptr;

            HandlePool<VulkanBuffer, BufferHandle> m_Buffers;
            HandlePool<VulkanTexture, TextureHandle> m_Textures;
            ReleaseQueue<VulkanRelease> m_Releases;

            VulkanCommandList m_CommandList;
            bool m_InFrame = false;
        };
    }
}
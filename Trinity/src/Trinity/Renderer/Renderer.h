#pragma once

#include <vector>
#include <cstddef>
#include <vulkan/vulkan.h>

namespace Trinity
{
    class VulkanContext;

    class Renderer
    {
    public:
        Renderer() = default;
        Renderer(VulkanContext* context);
        ~Renderer() = default;

        bool Initialize();
        void Shutdown();

        void DrawFrame();

    private:
        void CreateRenderPass();
        void CreateGraphicsPipeline();
        void CreateFramebuffers();
        void CreateCommandPool();
        void CreateCommandBuffer();
        void CreateSyncObjects();
        void RecordCommandBuffer(uint32_t imageIndex);

        //----------------------------------------------------------------------------------------------------------------------------------------------------//

        VkShaderModule CreateShaderModule(const std::vector<std::byte>& code);

    private:
        VulkanContext* m_Context = nullptr;

        VkRenderPass m_RenderPass = VK_NULL_HANDLE;
        VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
        VkPipeline m_GraphicsPipeline = VK_NULL_HANDLE;
        std::vector<VkFramebuffer> m_Framebuffers{};
        VkCommandPool m_CommandPool = VK_NULL_HANDLE;
        VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;
        VkSemaphore m_ImageAvailableSemaphore = VK_NULL_HANDLE;
        VkSemaphore m_RenderFinshedSemaphore = VK_NULL_HANDLE;
        VkFence m_InFlightFence = VK_NULL_HANDLE;
    };
}
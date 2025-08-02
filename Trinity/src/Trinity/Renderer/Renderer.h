#pragma once

#include <vulkan/vulkan.h>
#include <vector>

#include "Trinity/Renderer/VertexBuffer.h"
#include "Trinity/Renderer/IndexBuffer.h"
#include "Trinity/Renderer/UniformBuffer.h"
#include "Trinity/Renderer/Texture.h"

#include "Trinity/Camera/Camera.h"

namespace Trinity
{
    class VulkanContext;
    class Scene;

    class Renderer
    {
    public:
        Renderer() = default;
        Renderer(VulkanContext* context);
        ~Renderer() = default;

        bool Initialize();
        void Shutdown();

        void DrawFrame();

        Camera& GetCamera() { return m_Camera; }

        void SetScene(Scene* scene) { m_Scene = scene; }

        VertexBuffer& GetVertexBuffer() { return m_VertexBuffer; }
        IndexBuffer& GetIndexBuffer() { return m_IndexBuffer; }
        Texture& GetTexture() { return m_Texture; }

    private:
        void CreateRenderPass();
        void CreateDescriptorSetLayout();
        void CreateDescriptorPool();
        void CreateDescriptorSets();
        void CreateGraphicsPipeline();
        void CreateDepthResources();
        void CreateFramebuffers();
        void CreateCommandPool();
        void CreateTextureImage();
        void CreateVertexBuffer();
        void CreateIndexBuffer();
        void CreateUniformBuffers();
        void CreateCommandBuffer();
        void CreateSyncObjects();
        void RecordCommandBuffer(uint32_t imageIndex);
        void CleanupSwapChain();
        void RecreateSwapChain();

        //----------------------------------------------------------------------------------------------------------------------------------------------------//

        VkShaderModule CreateShaderModule(const std::vector<std::byte>& code);
        VkFormat FindDepthFormat();
        bool HasStencilComponent(VkFormat format);

    private:
        VulkanContext* m_Context = nullptr;

        VkRenderPass m_RenderPass = VK_NULL_HANDLE;
        VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
        VkPipeline m_GraphicsPipeline = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_DescriptorSets{};
        std::vector<VkFramebuffer> m_Framebuffers{};
        std::vector<VkImage> m_DepthImages{};
        std::vector<VkDeviceMemory> m_DepthImageMemory{};
        std::vector<VkImageView> m_DepthImageViews{};
        VkCommandPool m_CommandPool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> m_CommandBuffer{};
        std::vector<VkSemaphore> m_ImageAvailableSemaphore{};
        std::vector<VkSemaphore> m_RenderFinshedSemaphore{};
        std::vector<VkFence> m_InFlightFence{};
        std::vector<VkFence> m_ImagesInFlight{};

        VertexBuffer m_VertexBuffer{};
        IndexBuffer m_IndexBuffer{};
        std::vector<UniformBuffer> m_UniformBuffers{};
        Texture m_Texture;

        Camera m_Camera;
        Scene* m_Scene = nullptr;

        size_t m_CurrentFrame = 0;
    };
}
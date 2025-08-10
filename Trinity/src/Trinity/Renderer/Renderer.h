#pragma once

#include <functional>
#include <vector>
#include <string>
#include <memory>

#include <vulkan/vulkan.h>
#include <imgui.h>

#include "Trinity/Renderer/IndexBuffer.h"
#include "Trinity/Renderer/PostProcess/BloomPass.h"
#include "Trinity/Renderer/PostProcess/ToneMappingPass.h"
#include "Trinity/Renderer/RenderGraph.h"
#include "Trinity/Renderer/Texture.h"
#include "Trinity/Renderer/UniformBuffer.h"
#include "Trinity/Renderer/VertexBuffer.h"

#include "Trinity/Camera/Camera.h"

namespace Trinity
{
    class VulkanContext;
    class Scene;
    class ResourceManager;
    class Mesh;

    class Renderer
    {
    public:
        Renderer() = default;
        Renderer(VulkanContext* context);
        ~Renderer() = default;

        bool Initialize();
        void Shutdown();

        void DrawFrame(const std::function<void(VkCommandBuffer)>& recordCallback = nullptr);

        Camera& GetCamera() { return m_Camera; }

        void OnWindowResize();

        ImTextureID GetViewportImage();

        void SetScene(Scene* scene) { m_Scene = scene; }
        void SetResourceManager(ResourceManager* resourceManager) { m_ResourceManager = resourceManager; }
        Texture* RequestTexture(const std::string& path);
        std::shared_ptr<Mesh> RequestMesh(const std::string& path);

        VertexBuffer& GetVertexBuffer() { return m_VertexBuffer; }
        IndexBuffer& GetIndexBuffer() { return m_IndexBuffer; }
        Texture& GetTexture() { return m_Texture; }
        VkRenderPass GetRenderPass() const { return m_RenderPass; }

    private:
        void CreateRenderPass();
        void CreateDescriptorSetLayout();
        void CreateDescriptorPool();
        void CreateDescriptorSets();
        void CreateGraphicsPipeline(VkPrimitiveTopology topology);
        void CreateDepthResources();
        void CreateFramebuffers();
        void CreateCommandPool();
        void CreateTextureImage();
        void CreateVertexBuffer();
        void CreateIndexBuffer();
        void CreateUniformBuffers();
        void CreateShadowResources();
        void CreateCommandBuffer();
        void CreateSyncObjects();
        void CreateImGuiImageDescriptors();
        void CleanupImGuiImageDescriptors();
        void RenderMainPass(uint32_t imageIndex, const std::function<void(VkCommandBuffer)>& recordCallback = nullptr);
        void RenderShadowPass(uint32_t imageIndex);
        void CleanupSwapChain();
        void RecreateSwapChain();

        //----------------------------------------------------------------------------------------------------------------------------------------------------//

        VkFormat FindDepthFormat();
        bool HasStencilComponent(VkFormat format);

    private:
        VulkanContext* m_Context = nullptr;

        VkRenderPass m_RenderPass = VK_NULL_HANDLE;
        VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
        VkPipeline m_GraphicsPipeline = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
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
        struct FrameData
        {
            UniformBuffer GlobalUniform;
            UniformBuffer LightUniform;
            UniformBuffer MaterialUniform;
            VkDescriptorSet DescriptorSet = VK_NULL_HANDLE;
        };
        std::vector<FrameData> m_Frames{};
        Texture m_Texture;
        std::shared_ptr<Mesh> m_DefaultMesh;

        VkRenderPass m_ShadowRenderPass = VK_NULL_HANDLE;
        VkFramebuffer m_ShadowFramebuffer = VK_NULL_HANDLE;
        VkImage m_ShadowImage = VK_NULL_HANDLE;
        VkDeviceMemory m_ShadowImageMemory = VK_NULL_HANDLE;
        VkImageView m_ShadowImageView = VK_NULL_HANDLE;
        VkSampler m_ShadowSampler = VK_NULL_HANDLE;
        uint32_t m_ShadowMapSize = 1024;

        Camera m_Camera;
        Scene* m_Scene = nullptr;
        ResourceManager* m_ResourceManager = nullptr;

        size_t m_CurrentFrame = 0;
        VkPrimitiveTopology m_PrimitiveTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        RenderGraph m_RenderGraph;
        BloomPass m_BloomPass;
        ToneMappingPass m_ToneMappingPass;

        std::vector<VkDescriptorSet> m_ImGuiImageDescriptors{};
        VkSampler m_ImGuiImageSampler = VK_NULL_HANDLE;
        uint32_t m_LastImageIndex = 0;
    };
}
#pragma once

#include <functional>
#include <vector>
#include <vulkan/vulkan.h>

namespace Trinity
{
    class VulkanContext;
    class Scene;
    class AssetManager;
    class Camera;

    class Renderer
    {
    public:
        Renderer() = default;
        explicit Renderer(VulkanContext* context);
        ~Renderer() = default;

        bool Initialize();
        void Shutdown();

        void DrawFrame(const std::function<void(VkCommandBuffer)>& recordFunc);
        void OnWindowResize();

        void SetScene(Scene* scene) { m_Scene = scene; }
        void SetAssetManager(AssetManager* manager) { m_AssetManager = manager; }

        Camera& GetCamera() { return m_Camera; }
        VkRenderPass GetRenderPass() const { return m_RenderPass; }

    private:
        VulkanContext* m_Context = nullptr;

        VkRenderPass m_RenderPass = VK_NULL_HANDLE;
        VkCommandPool m_CommandPool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> m_CommandBuffers;

        Scene* m_Scene = nullptr;
        AssetManager* m_AssetManager = nullptr;
        Camera m_Camera;
    };
}
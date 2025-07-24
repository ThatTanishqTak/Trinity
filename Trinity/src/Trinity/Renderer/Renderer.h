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

        //----------------------------------------------------------------------------------------------------------------------------------------------------//

        VkShaderModule CreateShaderModule(const std::vector<std::byte>& code);

        VulkanContext* m_Context = nullptr;

        VkRenderPass m_RenderPass = VK_NULL_HANDLE;
        VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
        VkPipeline m_GraphicsPipeline = VK_NULL_HANDLE;
    };
}
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
        void CreateGraphicsPipeline();

        //----------------------------------------------------------------------------------------------------------------------------------------------------//

        VkShaderModule CreateShaderModule(const std::vector<std::byte>& code);

        VulkanContext* m_Context = nullptr;
    };
}
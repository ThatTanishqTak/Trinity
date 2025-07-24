#include "Trinity/trpch.h"

#include "Trinity/Renderer/Renderer.h"
#include "Trinity/Utilities/Utilities.h"
#include "Trinity/Vulkan/VulkanContext.h"

namespace Trinity
{
    Renderer::Renderer(VulkanContext* context) : m_Context(context)
    {

    }

    bool Renderer::Initialize()
    {
        TR_CORE_INFO("-------INITIALIZING RENDERER-------");

        CreateGraphicsPipeline();

        TR_CORE_INFO("-------RENDERER INITIALIZED-------");

        return true;
    }

    void Renderer::Shutdown()
    {
        TR_CORE_INFO("-------SHUTTING DOWN RENDERER-------");

        TR_CORE_INFO("-------RENDERER SHUTDOWN COMPLETE-------");
    }

    void Renderer::DrawFrame()
    {
        
    }

    //----------------------------------------------------------------------------------------------------------------------------------------------------//

    void Renderer::CreateGraphicsPipeline()
    {
        TR_CORE_TRACE("Creating graphics pipeline");

        if (!m_Context)
        {
            TR_CORE_ERROR("Vulkan context is not set");

            return;
        }

        auto vertextSource = Utilities::FileManagement::ReadFile("Assets/Shaders/Simple.vert.spv");
        auto fragmentSource = Utilities::FileManagement::ReadFile("Assets/Shaders/Simple.frag.spv");

        VkShaderModule vertShader = CreateShaderModule(vertextSource);
        VkShaderModule fragShader = CreateShaderModule(fragmentSource);

        TR_CORE_TRACE("Graphics pipeline created");

        vkDestroyShaderModule(m_Context->GetDevice(), fragShader, nullptr);
        vkDestroyShaderModule(m_Context->GetDevice(), vertShader, nullptr);
    }

    //----------------------------------------------------------------------------------------------------------------------------------------------------//

    VkShaderModule Renderer::CreateShaderModule(const std::vector<std::byte>& code)
    {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

        VkShaderModule shaderModule = VK_NULL_HANDLE;
        if (vkCreateShaderModule(m_Context->GetDevice(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to create shader module");
        }

        return shaderModule;
    }
}
#include "Trinity/trpch.h"

#include "Trinity/Renderer/Shader.h"
#include "Trinity/Utilities/Utilities.h"
#include "Trinity/Vulkan/VulkanContext.h"

namespace Trinity
{
    Shader::Shader(VulkanContext* context) : m_Context(context)
    {
    }

    bool Shader::Load(const std::string& path)
    {
        m_Path = std::filesystem::path(path);
        if (!std::filesystem::exists(m_Path))
        {
            TR_CORE_ERROR("Shader file not found: {}", m_Path.string());

            return false;
        }

        auto l_Code = Utilities::FileManagement::ReadFile(m_Path.string());
        if (l_Code.empty())
        {
            TR_CORE_ERROR("Failed to read shader file: {}", m_Path.string());

            return false;
        }

        VkShaderModuleCreateInfo l_CreateInfo{};
        l_CreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        l_CreateInfo.codeSize = l_Code.size();
        l_CreateInfo.pCode = reinterpret_cast<const uint32_t*>(l_Code.data());

        VkShaderModule l_Module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(m_Context->GetDevice(), &l_CreateInfo, nullptr, &l_Module) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to create shader module: {}", m_Path.string());

            return false;
        }

        m_ShaderModule = { l_Module, { m_Context } };

        return true;
    }

    bool Shader::Reload()
    {
        if (m_Path.empty())
        {
            TR_CORE_WARN("Shader path is empty");

            return false;
        }

        Destroy();

        return Load(m_Path.string());
    }

    void Shader::Destroy()
    {
        m_ShaderModule.reset();
    }

    void Shader::ShaderModuleDeleter::operator()(VkShaderModule module) const
    {
        if (module && context)
        {
            vkDestroyShaderModule(context->GetDevice(), module, nullptr);
        }
    }
}
#include "Trinity/trpch.h"

#include "Trinity/Renderer/Shader.h"
#include "Trinity/Vulkan/VulkanContext.h"
#include "Trinity/Utilities/Utilities.h"

namespace Trinity
{
    Shader::~Shader()
    {
        Destroy();
    }

    std::optional<std::string> Shader::LoadFromFile(const std::filesystem::path& path)
    {
        if (!m_Context)
        {
            return "Invalid Vulkan context";
        }

        m_Path = path;
        VkDevice l_Device = m_Context->GetDevice();

        Destroy();

        auto l_CreateModule = [&](const std::filesystem::path& file, VkShaderModule& module) -> std::optional<std::string>
            {
                auto l_Code = Utilities::FileManagement::ReadFile(file);
                if (l_Code.empty())
                {
                    return "Failed to read shader file";
                }

                VkShaderModuleCreateInfo l_CreateInfo{};
                l_CreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
                l_CreateInfo.codeSize = l_Code.size();
                l_CreateInfo.pCode = reinterpret_cast<const uint32_t*>(l_Code.data());

                if (vkCreateShaderModule(l_Device, &l_CreateInfo, nullptr, &module) != VK_SUCCESS)
                {
                    TR_CORE_ERROR("Failed to create shader module: {}", file.string());

                    return "Failed to create shader module";
                }

                return {};
            };

        std::filesystem::path l_VertexPath = path;
        l_VertexPath += ".vert.spv";

        std::filesystem::path l_FragmentPath = path;
        l_FragmentPath += ".frag.spv";

        if (auto l_Err = l_CreateModule(l_VertexPath, m_VertexModule))
        {
            return l_Err;
        }

        if (auto l_Err = l_CreateModule(l_FragmentPath, m_FragmentModule))
        {
            vkDestroyShaderModule(l_Device, m_VertexModule, nullptr);
            m_VertexModule = VK_NULL_HANDLE;

            return l_Err;
        }

        return {};
    }

    std::optional<std::string> Shader::Reload()
    {
        if (m_Path.empty())
        {
            return "Shader path not set";
        }

        return LoadFromFile(m_Path);
    }

    void Shader::Destroy()
    {
        if (!m_Context)
        {
            m_VertexModule = VK_NULL_HANDLE;
            m_FragmentModule = VK_NULL_HANDLE;

            return;
        }

        VkDevice l_Device = m_Context->GetDevice();

        if (m_VertexModule)
        {
            vkDestroyShaderModule(l_Device, m_VertexModule, nullptr);
            m_VertexModule = VK_NULL_HANDLE;
        }

        if (m_FragmentModule)
        {
            vkDestroyShaderModule(l_Device, m_FragmentModule, nullptr);
            m_FragmentModule = VK_NULL_HANDLE;
        }
    }
}
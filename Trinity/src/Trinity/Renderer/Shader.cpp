#include "Trinity/trpch.h"

#include "Trinity/Renderer/Shader.h"
#include "Trinity/Renderer/ShaderCompiler.h"
#include "Trinity/Vulkan/VulkanContext.h"
#include "Trinity/Utilities/Utilities.h"
#include "Trinity/Resources/ShaderWatcher.h"

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
        Resources::ShaderWatcher::Watch(this, path.parent_path());
        VkDevice l_Device = m_Context->GetDevice();

        Destroy();

        auto a_CreateModule = [l_Device](const std::vector<uint32_t>& code, VkShaderModule& module) -> std::optional<std::string>
            {
                VkShaderModuleCreateInfo l_CreateInfo{};
                l_CreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
                l_CreateInfo.codeSize = code.size() * sizeof(uint32_t);
                l_CreateInfo.pCode = code.data();

                if (vkCreateShaderModule(l_Device, &l_CreateInfo, nullptr, &module) != VK_SUCCESS)
                {
                    return "Failed to create shader module";
                }

                return {};
            };

        auto a_VertexCode = ShaderCompiler::CompileToSpv((path.string() + ".vert"));
        if (a_VertexCode.empty())
        {
            return "Vertex shader compilation failed";
        }

        auto a_FragmentCode = ShaderCompiler::CompileToSpv((path.string() + ".frag"));
        if (a_FragmentCode.empty())
        {
            return "Fragment shader compilation failed";
        }

        if (auto a_Error = a_CreateModule(a_VertexCode, m_VertexModule))
        {
            return a_Error;
        }

        if (auto a_Error = a_CreateModule(a_FragmentCode, m_FragmentModule))
        {
            vkDestroyShaderModule(l_Device, m_VertexModule, nullptr);
            m_VertexModule = VK_NULL_HANDLE;

            return a_Error;
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

    VkShaderModule Shader::LoadFromFile(VkDevice device, const std::filesystem::path& path)
    {
        auto a_Code = Utilities::FileManagement::ReadFile(path);
        if (a_Code.empty())
        {
            return VK_NULL_HANDLE;
        }

        VkShaderModuleCreateInfo l_CreateInfo{};
        l_CreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        l_CreateInfo.codeSize = a_Code.size();
        l_CreateInfo.pCode = reinterpret_cast<const uint32_t*>(a_Code.data());

        VkShaderModule l_Module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device, &l_CreateInfo, nullptr, &l_Module) != VK_SUCCESS)
        {
            return VK_NULL_HANDLE;
        }

        return l_Module;
    }
}
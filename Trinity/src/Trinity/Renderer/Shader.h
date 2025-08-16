#pragma once

#include <filesystem>
#include <optional>
#include <vulkan/vulkan.h>

namespace Trinity
{
    class VulkanContext;

    class Shader
    {
    public:
        explicit Shader(VulkanContext* context) : m_Context(context) {}
        ~Shader();

        std::optional<std::string> LoadFromFile(const std::filesystem::path& path);
        std::optional<std::string> Reload();
        void Destroy();

        VkShaderModule GetVertexModule() const { return m_VertexModule; }
        VkShaderModule GetFragmentModule() const { return m_FragmentModule; }

    private:
        VulkanContext* m_Context = nullptr;
        std::filesystem::path m_Path;
        VkShaderModule m_VertexModule = VK_NULL_HANDLE;
        VkShaderModule m_FragmentModule = VK_NULL_HANDLE;
    };
}
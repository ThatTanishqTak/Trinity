#pragma once

#include <memory>
#include <string>
#include <filesystem>

#include <vulkan/vulkan.h>

namespace Trinity
{
    class VulkanContext;

    class Shader
    {
    public:
        Shader() = default;
        explicit Shader(VulkanContext* context);
        ~Shader() = default;

        bool Load(const std::string& path);
        bool Reload();
        void Destroy();

        VkShaderModule GetModule() const { return m_ShaderModule.get(); }

    private:
        struct ShaderModuleDeleter
        {
            VulkanContext* context = nullptr;
            void operator()(VkShaderModule module) const;
        };

        VulkanContext* m_Context = nullptr;
        std::filesystem::path m_Path;
        std::unique_ptr<std::remove_pointer<VkShaderModule>::type, ShaderModuleDeleter> m_ShaderModule{ nullptr, { nullptr } };
    };
}
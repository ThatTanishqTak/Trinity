#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

#include "Trinity/Renderer/Shader.h"

namespace Trinity
{
    class VulkanContext;

    class ShaderLibrary
    {
    public:
        static ShaderLibrary& Get();

        std::shared_ptr<Shader> Load(const std::string& identifier, VulkanContext* context, const std::filesystem::path& path, VkShaderStageFlagBits stage);
        std::shared_ptr<Shader> Get(const std::string& identifier);

    private:
        std::shared_ptr<Shader> CreateFallback(VulkanContext* context);

        std::unordered_map<std::string, std::shared_ptr<Shader>> m_Shaders;
        std::shared_ptr<Shader> m_Fallback;
    };
}
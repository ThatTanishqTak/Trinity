#pragma once

#include <filesystem>
#include <memory>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan.h>

namespace Trinity
{
    class VulkanContext;

    class Shader
    {
    public:
        Shader() = default;
        explicit Shader(VulkanContext* context);
        ~Shader();

        bool Load(const std::filesystem::path& path, VkShaderStageFlagBits stage);
        bool Reload();
        void Destroy();

        VkShaderModule GetModule(VkShaderStageFlagBits stage) const;
        VkPipeline GetPipeline(const VkGraphicsPipelineCreateInfo& pipelineInfo, const void* specData = nullptr, size_t specSize = 0);

        bool Update();

    private:
        struct ShaderModuleDeleter
        {
            VkDevice Device = VK_NULL_HANDLE;
            void operator()(VkShaderModule module) const;
        };

        struct PipelineDeleter
        {
            VkDevice Device = VK_NULL_HANDLE;
            void operator()(VkPipeline pipeline) const;
        };

        struct Stage
        {
            VkShaderStageFlagBits StageBit = VK_SHADER_STAGE_VERTEX_BIT;
            std::filesystem::path SourcePath;
            std::filesystem::path SpirvPath;
            std::vector<uint32_t> SpirvCode;
            std::filesystem::file_time_type Timestamp{};
            std::unique_ptr<std::remove_pointer<VkShaderModule>::type, ShaderModuleDeleter> module{ nullptr, { VK_NULL_HANDLE } };
        };

        size_t CalculateSpecHash(const void* data, size_t size) const;
        bool CompileStage(Stage& stage);
        bool LoadStage(Stage& stage);

        VulkanContext* m_Context = nullptr;
        std::vector<Stage> m_Stages;

        std::unordered_map<size_t, std::unique_ptr<std::remove_pointer<VkPipeline>::type, PipelineDeleter>> m_Pipelines;
    };
}
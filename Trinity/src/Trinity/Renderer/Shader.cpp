#include "Trinity/trpch.h"

#include "Trinity/Renderer/Shader.h"
#include "Trinity/Utilities/Utilities.h"
#include "Trinity/Vulkan/VulkanContext.h"

#include <functional>
#include <string_view>
#include <system_error>

namespace Trinity
{
    Shader::Shader(VulkanContext* context) : m_Context(context)
    {

    }

    bool Shader::Load(const std::filesystem::path& path, VkShaderStageFlagBits stage)
    {
        Stage l_Stage;
        l_Stage.stage = stage;
        l_Stage.sourcePath = path;
        if (!std::filesystem::exists(path))
        {
            TR_CORE_ERROR("Shader file not found: {}", path.string());

            return false;
        }

        std::error_code l_Ec;
        l_Stage.timestamp = std::filesystem::last_write_time(path, l_Ec);
        if (l_Ec)
        {
            TR_CORE_ERROR("Failed to get last write time for {}: {}", path.string(), l_Ec.message());

            return false;
        }

        if (!CompileStage(l_Stage))
        {
            return false;
        }

        if (!LoadStage(l_Stage))
        {
            return false;
        }

        m_Stages.push_back(std::move(l_Stage));

        return true;
    }

    bool Shader::Reload()
    {
        Destroy();
        for (auto& it_Stage : m_Stages)
        {
            if (!std::filesystem::exists(it_Stage.sourcePath))
            {
                TR_CORE_ERROR("Shader file not found: {}", it_Stage.sourcePath.string());

                return false;
            }

            std::error_code l_Ec;
            it_Stage.timestamp = std::filesystem::last_write_time(it_Stage.sourcePath, l_Ec);
            if (l_Ec)
            {
                TR_CORE_ERROR("Failed to get last write time for {}: {}", it_Stage.sourcePath.string(), l_Ec.message());

                return false;
            }

            if (!CompileStage(it_Stage) || !LoadStage(it_Stage))
            {
                return false;
            }
        }
        m_Pipelines.clear();
        
        return true;
    }

    void Shader::Destroy()
    {
        for (auto& it_Stage : m_Stages)
        {
            it_Stage.module.reset();
        }
        m_Pipelines.clear();
    }

    VkShaderModule Shader::GetModule(VkShaderStageFlagBits stage) const
    {
        for (const auto& l_Stage : m_Stages)
        {
            if (l_Stage.stage == stage)
            {
                return l_Stage.module.get();
            }
        }
        
        return VK_NULL_HANDLE;
    }

    VkPipeline Shader::GetPipeline(const VkGraphicsPipelineCreateInfo& pipelineInfo, const void* specData, size_t specSize)
    {
        size_t l_Hash = CalculateSpecHash(specData, specSize);
        auto a_Lt = m_Pipelines.find(l_Hash);
        if (a_Lt != m_Pipelines.end())
        {
            return a_Lt->second.get();
        }

        VkGraphicsPipelineCreateInfo l_Info = pipelineInfo;
        std::vector<VkPipelineShaderStageCreateInfo> l_StagesInfo;
        l_StagesInfo.reserve(m_Stages.size());

        VkSpecializationInfo l_SpecInfo{};
        std::vector<VkSpecializationMapEntry> l_MapEntries;
        if (specData && specSize > 0)
        {
            l_MapEntries.push_back({ 0, 0, static_cast<uint32_t>(specSize) });
            l_SpecInfo.mapEntryCount = static_cast<uint32_t>(l_MapEntries.size());
            l_SpecInfo.pMapEntries = l_MapEntries.data();
            l_SpecInfo.dataSize = specSize;
            l_SpecInfo.pData = specData;
        }

        for (const auto& it_Stage : m_Stages)
        {
            VkPipelineShaderStageCreateInfo l_StageInfo{};
            l_StageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            l_StageInfo.stage = it_Stage.stage;
            l_StageInfo.module = it_Stage.module.get();
            l_StageInfo.pName = "main";
            if (specData && specSize > 0)
            {
                l_StageInfo.pSpecializationInfo = &l_SpecInfo;
            }
            l_StagesInfo.push_back(l_StageInfo);
        }

        l_Info.stageCount = static_cast<uint32_t>(l_StagesInfo.size());
        l_Info.pStages = l_StagesInfo.data();

        VkPipeline l_Pipeline = VK_NULL_HANDLE;
        if (vkCreateGraphicsPipelines(m_Context->GetDevice(), VK_NULL_HANDLE, 1, &l_Info, nullptr, &l_Pipeline) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to create graphics pipeline");
        
            return VK_NULL_HANDLE;
        }

        m_Pipelines[l_Hash] = { l_Pipeline, { m_Context } };
        
        return l_Pipeline;
    }

    bool Shader::Update()
    {
        bool l_Reload = false;
        for (auto& it_Stage : m_Stages)
        {
            if (!std::filesystem::exists(it_Stage.sourcePath))
            {
                TR_CORE_ERROR("Shader file not found: {}", it_Stage.sourcePath.string());

                continue;
            }

            std::error_code l_Ec;
            auto a_Time = std::filesystem::last_write_time(it_Stage.sourcePath, l_Ec);
            if (l_Ec)
            {
                TR_CORE_ERROR("Failed to get last write time for {}: {}", it_Stage.sourcePath.string(), l_Ec.message());

                continue;
            }

            if (a_Time != it_Stage.timestamp)
            {
                l_Reload = true;
                it_Stage.timestamp = a_Time;
            }
        }
        if (l_Reload)
        {
            Reload();
        }
        
        return l_Reload;
    }

    size_t Shader::CalculateSpecHash(const void* data, size_t size) const
    {
        if (!data || size == 0)
        {
            return 0;
        }
        const char* l_Bytes = static_cast<const char*>(data);
        
        return std::hash<std::string_view>{}(std::string_view{ l_Bytes, size });
    }

    bool Shader::CompileStage(Stage& stage)
    {
        std::filesystem::path l_Source = stage.sourcePath;
        if (l_Source.extension() == ".spv")
        {
            stage.spirvPath = l_Source;
            
            return true;
        }

        std::filesystem::path l_Output = l_Source;
        l_Output += ".spv";

        std::string l_Command = "glslc " + l_Source.string() + " -o " + l_Output.string();
        int l_Result = std::system(l_Command.c_str());
        if (l_Result != 0)
        {
            TR_CORE_ERROR("Failed to compile shader: {}", l_Source.string());
        
            return false;
        }

        stage.spirvPath = l_Output;
        
        return true;
    }

    bool Shader::LoadStage(Stage& stage)
    {
        if (!std::filesystem::exists(stage.spirvPath))
        {
            TR_CORE_ERROR("Shader file not found: {}", stage.spirvPath.string());
            
            return false;
        }

        auto a_Code = Utilities::FileManagement::ReadFile(stage.spirvPath);
        if (a_Code.empty())
        {
            TR_CORE_ERROR("Failed to read shader file: {}", stage.spirvPath.string());
            
            return false;
        }

        VkShaderModuleCreateInfo l_CreateInfo{};
        l_CreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        l_CreateInfo.codeSize = a_Code.size();
        l_CreateInfo.pCode = reinterpret_cast<const uint32_t*>(a_Code.data());

        VkShaderModule l_Module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(m_Context->GetDevice(), &l_CreateInfo, nullptr, &l_Module) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to create shader module: {}", stage.spirvPath.string());
        
            return false;
        }

        stage.module = { l_Module, { m_Context } };
        
        return true;
    }

    void Shader::ShaderModuleDeleter::operator()(VkShaderModule module) const
    {
        if (module && context)
        {
            vkDestroyShaderModule(context->GetDevice(), module, nullptr);
        }
    }

    void Shader::PipelineDeleter::operator()(VkPipeline pipeline) const
    {
        if (pipeline && context)
        {
            vkDestroyPipeline(context->GetDevice(), pipeline, nullptr);
        }
    }
}
#include "Trinity/trpch.h"

#include "Trinity/Renderer/Shader.h"
#include "Trinity/Utilities/Utilities.h"
#include "Trinity/Vulkan/VulkanContext.h"
#include "Trinity/Renderer/ShaderCompiler.h"
#include "Trinity/Renderer/ShaderCache.h"
#include "Trinity/Resources/ShaderWatcher.h"
#include "Trinity/Renderer/UniformBuffer.h"

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
        l_Stage.StageBit = stage;
        l_Stage.SourcePath = path;
        if (!std::filesystem::exists(path))
        {
            TR_CORE_ERROR("Shader file not found: {}", path.string());

            return false;
        }

        std::error_code l_ErrorCode;
        l_Stage.Timestamp = std::filesystem::last_write_time(path, l_ErrorCode);
        if (l_ErrorCode)
        {
            TR_CORE_ERROR("Failed to get last write time for {}: {}", path.string(), l_ErrorCode.message());

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

        Resources::ShaderWatcher::Watch(this, path.parent_path());

        TR_CORE_TRACE("Shader loaded: {}", path.string());

        return true;
    }

    bool Shader::Reload()
    {
        Destroy();

        for (auto& it_Stage : m_Stages)
        {
            if (!std::filesystem::exists(it_Stage.SourcePath))
            {
                TR_CORE_ERROR("Shader file not found: {}", it_Stage.SourcePath.string());

                return false;
            }

            std::error_code l_ErrorCode;
            it_Stage.Timestamp = std::filesystem::last_write_time(it_Stage.SourcePath, l_ErrorCode);
            if (l_ErrorCode)
            {
                TR_CORE_ERROR("Failed to get last write time for {}: {}", it_Stage.SourcePath.string(), l_ErrorCode.message());

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
        for (const auto& it_Stage : m_Stages)
        {
            if (it_Stage.StageBit == stage)
            {
                return it_Stage.module.get();
            }
        }
        
        return VK_NULL_HANDLE;
    }

    VkPipeline Shader::GetPipeline(const VkGraphicsPipelineCreateInfo& pipelineInfo, const void* specData, size_t specSize)
    {
        if (!m_Context)
        {
            TR_CORE_ERROR("Attempted to create graphics pipeline with null context");

            return VK_NULL_HANDLE;
        }

        VkDevice l_Device = m_Context->GetDevice();
        if (l_Device == VK_NULL_HANDLE)
        {
            TR_CORE_ERROR("Attempted to create graphics pipeline with null device");

            return VK_NULL_HANDLE;
        }

        const void* l_Data = specData;
        size_t l_Size = specSize;
        if (!l_Data || l_Size == 0)
        {
            static const uint32_t l_Default = MaxLights;
            l_Data = &l_Default;
            l_Size = sizeof(uint32_t);
        }

        size_t l_Hash = CalculateSpecHash(l_Data, l_Size);
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
        if (l_Data && l_Size > 0)
        {
            l_MapEntries.push_back({ 0, 0, static_cast<uint32_t>(l_Size) });
            l_SpecInfo.mapEntryCount = static_cast<uint32_t>(l_MapEntries.size());
            l_SpecInfo.pMapEntries = l_MapEntries.data();
            l_SpecInfo.dataSize = l_Size;
            l_SpecInfo.pData = l_Data;
        }

        for (const auto& it_Stage : m_Stages)
        {
            if (it_Stage.module.get() == VK_NULL_HANDLE)
            {
                TR_CORE_ERROR("Shader stage module is null for stage {}", static_cast<uint32_t>(it_Stage.StageBit));

                continue;
            }

            VkPipelineShaderStageCreateInfo l_StageInfo{};
            l_StageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            l_StageInfo.stage = it_Stage.StageBit;
            l_StageInfo.module = it_Stage.module.get();
            l_StageInfo.pName = "main";
            if (l_Data && l_Size > 0)
            {
                l_StageInfo.pSpecializationInfo = &l_SpecInfo;
            }
            l_StagesInfo.push_back(l_StageInfo);
        }

        if (l_StagesInfo.empty())
        {
            TR_CORE_ERROR("Attempted to create graphics pipeline with no shader stages");

            return VK_NULL_HANDLE;
        }

        l_Info.stageCount = static_cast<uint32_t>(l_StagesInfo.size());
        l_Info.pStages = l_StagesInfo.data();

        VkPipeline l_Pipeline = VK_NULL_HANDLE;
        if (vkCreateGraphicsPipelines(l_Device, VK_NULL_HANDLE, 1, &l_Info, nullptr, &l_Pipeline) != VK_SUCCESS)
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
            if (!std::filesystem::exists(it_Stage.SourcePath))
            {
                TR_CORE_ERROR("Shader file not found: {}", it_Stage.SourcePath.string());

                continue;
            }

            std::error_code l_Ec;
            auto a_Time = std::filesystem::last_write_time(it_Stage.SourcePath, l_Ec);
            if (l_Ec)
            {
                TR_CORE_ERROR("Failed to get last write time for {}: {}", it_Stage.SourcePath.string(), l_Ec.message());

                continue;
            }

            if (a_Time != it_Stage.Timestamp)
            {
                l_Reload = true;
                it_Stage.Timestamp = a_Time;
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
        std::filesystem::path l_Source = stage.SourcePath;
        if (l_Source.extension() == ".spv")
        {
            auto a_Data = Utilities::FileManagement::ReadFile(l_Source);
            if (a_Data.empty())
            {
                TR_CORE_ERROR("Failed to read shader file: {}", l_Source.string());
                return false;
            }
            stage.SpirvCode.resize(a_Data.size() / sizeof(uint32_t));
            std::memcpy(stage.SpirvCode.data(), a_Data.data(), a_Data.size());

            return true;
        }

#ifdef TR_RUNTIME_SHADER_COMPILE
        auto a_SourceData = Utilities::FileManagement::ReadFile(l_Source);
        size_t l_Hash = std::hash<std::string_view>{}(std::string_view{
            reinterpret_cast<const char*>(a_SourceData.data()), a_SourceData.size() });

        ShaderCache& l_Cache = ShaderCache::Get();
        if (const CachedStage* l_Cached = l_Cache.GetStage(l_Source.string()))
        {
            if (l_Cached->Hash == l_Hash && l_Cached->Timestamp == stage.Timestamp)
            {
                stage.SpirvCode = l_Cached->Spirv;

                return true;
            }
        }

        auto a_Spirv = ShaderCompiler::CompileToSpv(l_Source.string());
        if (a_Spirv.empty())
        {
            TR_CORE_ERROR("Failed to compile shader: {}", l_Source.string());

            return false;
        }
        stage.SpirvCode = a_Spirv;
        l_Cache.StoreStage(l_Source.string(), l_Hash, stage.Timestamp, stage.SpirvCode);
#else
        std::filesystem::path l_Output = l_Source;
        l_Output += ".spv";
        stage.SpirvPath = l_Output;
#endif

        return true;
    }

    bool Shader::LoadStage(Stage& stage)
    {
        std::vector<uint32_t> l_Code;
        if (!stage.SpirvCode.empty())
        {
            l_Code = stage.SpirvCode;
        }

        else
        {
            if (!std::filesystem::exists(stage.SpirvPath))
            {
#ifdef TR_RUNTIME_SHADER_COMPILE
                TR_CORE_ERROR("Shader file not found: {}", stage.SpirvPath.string());
#else
                TR_CORE_WARN("Precompiled shader not found: {}. Enable TR_RUNTIME_SHADER_COMPILE or run the shader build step.", stage.SpirvPath.string());
#endif

                return false;
            }

            auto a_Code = Utilities::FileManagement::ReadFile(stage.SpirvPath);
            if (a_Code.empty())
            {
                TR_CORE_ERROR("Failed to read shader file: {}", stage.SpirvPath.string());

                return false;
            }

            l_Code.resize(a_Code.size() / sizeof(uint32_t));
            std::memcpy(l_Code.data(), a_Code.data(), a_Code.size());
        }

        VkShaderModuleCreateInfo l_CreateInfo{};
        l_CreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        l_CreateInfo.codeSize = l_Code.size() * sizeof(uint32_t);
        l_CreateInfo.pCode = l_Code.data();

        VkShaderModule l_Module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(m_Context->GetDevice(), &l_CreateInfo, nullptr, &l_Module) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to create shader module: {}", stage.SpirvPath.string());

            return false;
        }

        stage.module = { l_Module, { m_Context } };

        return true;
    }

    void Shader::ShaderModuleDeleter::operator()(VkShaderModule module) const
    {
        if (module && Context)
        {
            vkDestroyShaderModule(Context->GetDevice(), module, nullptr);
        }
    }

    void Shader::PipelineDeleter::operator()(VkPipeline pipeline) const
    {
        if (pipeline && Context)
        {
            vkDestroyPipeline(Context->GetDevice(), pipeline, nullptr);
        }
    }
}
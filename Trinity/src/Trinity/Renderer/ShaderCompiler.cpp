#include "Trinity/trpch.h"

#include "Trinity/Renderer/ShaderCompiler.h"
#include "Trinity/Utilities/Utilities.h"

#include <shaderc/shaderc.hpp>

namespace Trinity
{
    std::vector<uint32_t> ShaderCompiler::CompileToSpv(const std::string& path)
    {
        std::filesystem::path l_Path = path;
        std::string l_Ext = l_Path.extension().string();

        shaderc_shader_kind l_Stage;
        if (l_Ext == ".vert")
        {
            l_Stage = shaderc_glsl_vertex_shader;
        }

        else if (l_Ext == ".frag")
        {
            l_Stage = shaderc_glsl_fragment_shader;
        }

        else
        {
            TR_CORE_WARN("Unsupported shader extension: {}", l_Path.string());

            return {};
        }

        auto l_SourceBuffer = Utilities::FileManagement::ReadFile(l_Path);
        if (l_SourceBuffer.empty())
        {
            TR_CORE_ERROR("Failed to read shader {}", l_Path.string());

            return {};
        }

        std::string l_Source(reinterpret_cast<const char*>(l_SourceBuffer.data()), l_SourceBuffer.size());

        shaderc::Compiler l_Compiler;
        shaderc::CompileOptions l_Options;
        l_Options.SetOptimizationLevel(shaderc_optimization_level_performance);

        shaderc::SpvCompilationResult l_Result = l_Compiler.CompileGlslToSpv(l_Source, l_Stage, l_Path.string().c_str(), l_Options);
        if (l_Result.GetCompilationStatus() != shaderc_compilation_status_success)
        {
            TR_CORE_ERROR("Shader compilation failed {}: {}", l_Path.string(), l_Result.GetErrorMessage());

            return {};
        }

        return { l_Result.cbegin(), l_Result.cend() };
    }

    void ShaderCompiler::CompileAllShaders(const std::filesystem::path& directory)
    {
        if (!std::filesystem::exists(directory))
        {
            TR_CORE_WARN("Shader directory {} does not exist", directory.string());

            return;
        }

        for (const auto& it_Entry : std::filesystem::recursive_directory_iterator(directory))
        {
            if (!it_Entry.is_regular_file())
            {
                continue;
            }

            auto l_Spv = CompileToSpv(it_Entry.path().string());
            if (l_Spv.empty())
            {
                continue;
            }

            std::filesystem::path l_OutputPath = it_Entry.path();
            l_OutputPath += ".spv";

            std::ofstream l_File(l_OutputPath, std::ios::binary | std::ios::out);
            l_File.write(reinterpret_cast<const char*>(l_Spv.data()), l_Spv.size() * sizeof(uint32_t));
            l_File.close();

            TR_CORE_INFO("Compiled shader {}", l_OutputPath.string());
        }
    }
}
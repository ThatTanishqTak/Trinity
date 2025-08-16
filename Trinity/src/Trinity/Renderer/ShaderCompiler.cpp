#include "Trinity/trpch.h"

#include "Trinity/Renderer/ShaderCompiler.h"
#include "Trinity/Utilities/Utilities.h"

#include <shaderc/shaderc.hpp>

namespace Trinity
{
    void CompileAllShaders(const std::filesystem::path& a_Directory)
    {
        if (!std::filesystem::exists(a_Directory))
        {
            TR_CORE_WARN("Shader directory {} does not exist", a_Directory.string());

            return;
        }

        for (const auto& it_Entry : std::filesystem::recursive_directory_iterator(a_Directory))
        {
            if (!it_Entry.is_regular_file())
            {
                continue;
            }

            std::string l_Ext = it_Entry.path().extension().string();
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
                continue;
            }

            auto l_SourceBuffer = Utilities::FileManagement::ReadFile(it_Entry.path());
            if (l_SourceBuffer.empty())
            {
                TR_CORE_ERROR("Failed to read shader {}", it_Entry.path().string());

                continue;
            }

            std::string l_Source(reinterpret_cast<const char*>(l_SourceBuffer.data()), l_SourceBuffer.size());

            shaderc::Compiler l_Compiler;
            shaderc::CompileOptions l_Options;
            l_Options.SetOptimizationLevel(shaderc_optimization_level_performance);

            shaderc::SpvCompilationResult l_Result = l_Compiler.CompileGlslToSpv(l_Source, l_Stage, it_Entry.path().string().c_str(), l_Options);

            if (l_Result.GetCompilationStatus() != shaderc_compilation_status_success)
            {
                TR_CORE_ERROR("Shader compilation failed {}: {}", it_Entry.path().string(), l_Result.GetErrorMessage());

                continue;
            }

            std::filesystem::path l_OutputPath = it_Entry.path();
            l_OutputPath += ".spv";

            std::ofstream l_File(l_OutputPath, std::ios::binary | std::ios::out);
            l_File.write(reinterpret_cast<const char*>(l_Result.cbegin()), std::distance(l_Result.cbegin(), l_Result.cend()) * sizeof(uint32_t));
            l_File.close();

            TR_CORE_INFO("Compiled shader {}", l_OutputPath.string());
        }
    }
}
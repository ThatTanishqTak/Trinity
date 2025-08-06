#include "Trinity/trpch.h"

#include "Trinity/Renderer/ShaderCompiler.h"
#include "Trinity/Utilities/Utilities.h"

#include <shaderc/shaderc.hpp>
#include <fstream>

namespace Trinity
{
    std::vector<uint32_t> ShaderCompiler::CompileToSpv(const std::string& sourcePath)
    {
        std::ifstream l_File(sourcePath, std::ios::in);
        if (!l_File)
        {
            TR_CORE_ERROR("Failed to open shader file: {}", sourcePath);

            return {};
        }

        std::string l_Source((std::istreambuf_iterator<char>(l_File)), std::istreambuf_iterator<char>());

        shaderc::Compiler l_Compiler;
        shaderc::CompileOptions l_Options;

        shaderc::SpvCompilationResult l_Result = l_Compiler.CompileGlslToSpv(l_Source, shaderc_glsl_infer_from_source, sourcePath.c_str(), l_Options);
        if (l_Result.GetCompilationStatus() != shaderc_compilation_status_success)
        {
            TR_CORE_ERROR("Failed to compile: {}", l_Result.GetErrorMessage());

            std::cout << l_Result.GetErrorMessage();

            return {};
        }

        return { l_Result.cbegin(), l_Result.cend() };
    }
}
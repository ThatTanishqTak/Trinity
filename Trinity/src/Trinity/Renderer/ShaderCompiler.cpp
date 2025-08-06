#include "Trinity/trpch.h"

#include "Trinity/Renderer/ShaderCompiler.h"
#include "Trinity/Utilities/Utilities.h"

#include <shaderc/shaderc.hpp>
#include <filesystem>
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

        shaderc_shader_kind l_Kind = shaderc_glsl_infer_from_source;
        const std::string& l_Extension = std::filesystem::path(sourcePath).extension().string();
        if (l_Extension == ".vert")
        {
            l_Kind = shaderc_glsl_vertex_shader;
        }

        else if (l_Extension == ".frag")
        {
            l_Kind = shaderc_glsl_fragment_shader;
        }
        
        else if (l_Extension == ".comp")
        {
            l_Kind = shaderc_glsl_compute_shader;
        }

        else if (l_Extension == ".geom")
        {
            l_Kind = shaderc_glsl_geometry_shader;
        }

        else if (l_Extension == ".tesc")
        {
            l_Kind = shaderc_glsl_tess_control_shader;
        }

        else if (l_Extension == ".tese")
        {
            l_Kind = shaderc_glsl_tess_evaluation_shader;
        }

        shaderc::SpvCompilationResult l_Result = l_Compiler.CompileGlslToSpv(l_Source, l_Kind, sourcePath.c_str(), l_Options);
        if (l_Result.GetCompilationStatus() != shaderc_compilation_status_success)
        {
            std::string l_ErrorMessage = l_Result.GetErrorMessage();
            if (l_ErrorMessage.empty())
            {
                switch (l_Result.GetCompilationStatus())
                {
                case shaderc_compilation_status_invalid_stage:
                    l_ErrorMessage = "invalid shader stage";
                    break;
                case shaderc_compilation_status_compilation_error:
                    l_ErrorMessage = "compilation error";
                    break;
                case shaderc_compilation_status_internal_error:
                    l_ErrorMessage = "internal error";
                    break;
                case shaderc_compilation_status_null_result_object:
                    l_ErrorMessage = "null result object";
                    break;
                case shaderc_compilation_status_invalid_assembly:
                    l_ErrorMessage = "invalid assembly";
                    break;
                case shaderc_compilation_status_validation_error:
                    l_ErrorMessage = "validation error";
                    break;
                case shaderc_compilation_status_transformation_error:
                    l_ErrorMessage = "transformation error";
                    break;
                case shaderc_compilation_status_configuration_error:
                    l_ErrorMessage = "configuration error";
                    break;
                default:
                    l_ErrorMessage = "unknown error";
                    break;
                }
            }

            TR_CORE_ERROR("Failed to compile {}: {}", sourcePath, l_ErrorMessage);

            return {};
        }

        return { l_Result.cbegin(), l_Result.cend() };
    }
}
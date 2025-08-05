#include "Trinity/trpch.h"

#include "Trinity/BuildSystem/BuildSystem.h"
#include "Trinity/Renderer/ShaderCompiler.h"
#include "Trinity/Utilities/Utilities.h"

#include <fstream>

namespace Trinity
{
    void BuildSystem::Build(const BuildConfig& a_Config)
    {
        std::filesystem::create_directories(a_Config.OutputDir);

        CompileAllShaders(a_Config.OutputDir / "Assets/Shaders");

        std::filesystem::path l_SourceAssets = "Assets";
        std::filesystem::path l_DestAssets = a_Config.OutputDir / "Assets";
        std::filesystem::create_directories(l_DestAssets);
        std::filesystem::copy(l_SourceAssets, l_DestAssets, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
    }

    void BuildSystem::CompileAllShaders(const std::filesystem::path& a_OutputDir)
    {
        std::filesystem::path l_ShaderDir = "Assets/Shaders";
        if (!std::filesystem::exists(l_ShaderDir))
        {
            TR_CORE_WARN("Shader directory {} does not exist", l_ShaderDir.string());

            return;
        }

        std::filesystem::create_directories(a_OutputDir);

        for (const auto& l_Entry : std::filesystem::recursive_directory_iterator(l_ShaderDir))
        {
            if (!l_Entry.is_regular_file())
            {
                continue;
            }

            std::string l_Ext = l_Entry.path().extension().string();
            if (l_Ext != ".vert" && l_Ext != ".frag" && l_Ext != ".comp")
            {
                continue;
            }

            TR_CORE_INFO("Compiling shader {}", l_Entry.path().string());

            auto l_Spv = ShaderCompiler::CompileToSpv(l_Entry.path().string());
            if (l_Spv.empty())
            {
                continue;
            }

            std::filesystem::path l_OutputPath = a_OutputDir / l_Entry.path().filename();
            l_OutputPath.replace_extension("spv");

            std::ofstream l_File(l_OutputPath, std::ios::binary | std::ios::out);
            l_File.write(reinterpret_cast<const char*>(l_Spv.data()), l_Spv.size() * sizeof(uint32_t));
        }
    }
}
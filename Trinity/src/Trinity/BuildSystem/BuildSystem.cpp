#include "Trinity/trpch.h"

#include "Trinity/BuildSystem/BuildSystem.h"
#include "Trinity/Renderer/ShaderCompiler.h"
#include "Trinity/Utilities/Utilities.h"

#include <yaml-cpp/yaml.h>
#include <regex>
#include <fstream>
#include <set>

namespace Trinity
{
    BuildConfig BuildSystem::ParseBuildConfig(const std::filesystem::path& a_ConfigPath)
    {
        BuildConfig l_Config;

        YAML::Node l_Root = YAML::LoadFile(a_ConfigPath.string());

        if (l_Root["OutputDir"])
        {
            l_Config.OutputDir = l_Root["OutputDir"].as<std::string>();
        }

        if (l_Root["Executable"])
        {
            l_Config.Executable = l_Root["Executable"].as<std::string>();
        }

        if (l_Root["Scenes"])
        {
            for (auto l_Node : l_Root["Scenes"])
            {
                l_Config.Scenes.emplace_back(l_Node.as<std::string>());
            }
        }

        if (l_Root["Plugins"])
        {
            for (auto l_Node : l_Root["Plugins"])
            {
                l_Config.Plugins.emplace_back(l_Node.as<std::string>());
            }
        }

        if (l_Root["TargetPlatforms"])
        {
            for (auto l_Node : l_Root["TargetPlatforms"])
            {
                l_Config.TargetPlatforms.emplace_back(l_Node.as<std::string>());
            }
        }

        return l_Config;
    }

    void BuildSystem::BuildPackage(const std::filesystem::path& a_OutputDir)
    {
        std::filesystem::create_directories(a_OutputDir);

        std::filesystem::path l_SourceAssets = std::filesystem::current_path() / "Assets";
        if (std::filesystem::exists(l_SourceAssets))
        {
            std::filesystem::path l_DestAssets = a_OutputDir / "Assets";
            std::filesystem::create_directories(l_DestAssets);
            std::filesystem::copy(l_SourceAssets, l_DestAssets,
                std::filesystem::copy_options::recursive |
                std::filesystem::copy_options::overwrite_existing);
        }

        std::filesystem::path l_DefaultAssets = std::filesystem::current_path() / "TrinityForge/Resources/DefaultAssets";
        if (std::filesystem::exists(l_DefaultAssets))
        {
            std::filesystem::path l_DefaultDest = a_OutputDir / "Resources/DefaultAssets";
            std::filesystem::create_directories(l_DefaultDest);
            for (const auto& it_Entry : std::filesystem::recursive_directory_iterator(l_DefaultAssets))
            {
                if (!it_Entry.is_regular_file())
                {
                    continue;
                }

                std::filesystem::path l_Rel = std::filesystem::relative(it_Entry.path(), l_DefaultAssets);
                std::filesystem::path l_Target = l_DefaultDest / l_Rel;
                std::filesystem::create_directories(l_Target.parent_path());
                std::filesystem::copy_file(it_Entry.path(), l_Target, std::filesystem::copy_options::overwrite_existing);

                std::filesystem::path l_FlatTarget = a_OutputDir / "Assets/Resources" / it_Entry.path().filename();
                std::filesystem::create_directories(l_FlatTarget.parent_path());
                std::filesystem::copy_file(it_Entry.path(), l_FlatTarget, std::filesystem::copy_options::overwrite_existing);
            }
        }

        std::filesystem::path l_WorkingDir = std::filesystem::current_path();
        for (const auto& it_Entry : std::filesystem::directory_iterator(l_WorkingDir))
        {
            if (!it_Entry.is_regular_file())
            {
                continue;
            }

            std::string l_Extension = it_Entry.path().extension().string();
            if (l_Extension == ".dll" || l_Extension == ".so" || l_Extension == ".dylib" || l_Extension == ".exe" || l_Extension.empty())
            {
                std::filesystem::copy_file(it_Entry.path(), a_OutputDir / it_Entry.path().filename(), std::filesystem::copy_options::overwrite_existing);
            }
        }
    }

    std::vector<std::filesystem::path> BuildSystem::GetReferencedAssets(const std::filesystem::path& a_ScenePath)
    {
        std::vector<std::filesystem::path> l_Assets;

        std::ifstream l_Stream(a_ScenePath);
        if (!l_Stream.is_open())
        {
            TR_CORE_WARN("Failed to open scene {}", a_ScenePath.string());
            return l_Assets;
        }

        std::regex l_Pattern("\"(Assets/[^\"]+)\"");
        std::string l_Line;
        while (std::getline(l_Stream, l_Line))
        {
            std::smatch l_Match;
            auto l_Begin = l_Line.cbegin();
            while (std::regex_search(l_Begin, l_Line.cend(), l_Match, l_Pattern))
            {
                l_Assets.emplace_back(l_Match[1].str());
                l_Begin = l_Match.suffix().first;
            }
        }

        return l_Assets;
    }

    void BuildSystem::Build(const BuildConfig& a_Config)
    {
        std::filesystem::create_directories(a_Config.OutputDir);

        CompileAllShaders(a_Config.OutputDir / "Assets/Shaders");

        // Copy executable
        if (!a_Config.Executable.empty() && std::filesystem::exists(a_Config.Executable))
        {
            std::filesystem::path l_Dest = a_Config.OutputDir / a_Config.Executable.filename();
            std::filesystem::copy_file(a_Config.Executable, l_Dest, std::filesystem::copy_options::overwrite_existing);
        }

        // Collect assets from scenes
        std::set<std::filesystem::path> l_Assets;
        std::filesystem::path l_SceneOutDir = a_Config.OutputDir / "Assets/Scenes";
        std::filesystem::create_directories(l_SceneOutDir);
        for (const auto& it_Scene : a_Config.Scenes)
        {
            if (!std::filesystem::exists(it_Scene))
            {
                TR_CORE_WARN("Scene {} does not exist", it_Scene.string());
                continue;
            }

            std::filesystem::copy_file(it_Scene, l_SceneOutDir / it_Scene.filename(), std::filesystem::copy_options::overwrite_existing);

            auto a_Refs = GetReferencedAssets(it_Scene);
            l_Assets.insert(a_Refs.begin(), a_Refs.end());
        }

        // Copy referenced assets
        for (const auto& it_Asset : l_Assets)
        {
            if (!std::filesystem::exists(it_Asset))
            {
                TR_CORE_WARN("Asset {} not found", it_Asset.string());
                continue;
            }

            std::filesystem::path l_Dest = a_Config.OutputDir / it_Asset;
            std::filesystem::create_directories(l_Dest.parent_path());
            std::filesystem::copy_file(it_Asset, l_Dest, std::filesystem::copy_options::overwrite_existing);
        }

        // Copy plugins
        if (!a_Config.Plugins.empty())
        {
            std::filesystem::path l_PluginDir = a_Config.OutputDir / "Plugins";
            std::filesystem::create_directories(l_PluginDir);
            for (const auto& it_Plugin : a_Config.Plugins)
            {
                if (!std::filesystem::exists(it_Plugin))
                {
                    TR_CORE_WARN("Plugin {} does not exist", it_Plugin.string());
                    continue;
                }

                std::filesystem::copy_file(it_Plugin, l_PluginDir / it_Plugin.filename(), std::filesystem::copy_options::overwrite_existing);
            }
        }
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

        for (const auto& it_Entry : std::filesystem::recursive_directory_iterator(l_ShaderDir))
        {
            if (!it_Entry.is_regular_file())
            {
                continue;
            }

            std::string l_Ext = it_Entry.path().extension().string();
            if (l_Ext != ".vert" && l_Ext != ".frag" && l_Ext != ".comp")
            {
                continue;
            }

            TR_CORE_INFO("Compiling shader {}", it_Entry.path().string());

            auto a_Spv = ShaderCompiler::CompileToSpv(it_Entry.path().string());
            if (a_Spv.empty())
            {
                continue;
            }

            std::filesystem::path l_OutputPath = a_OutputDir / it_Entry.path().filename();
            l_OutputPath.replace_extension("spv");

            std::ofstream l_File(l_OutputPath, std::ios::binary | std::ios::out);
            l_File.write(reinterpret_cast<const char*>(a_Spv.data()), a_Spv.size() * sizeof(uint32_t));
        }
    }
}
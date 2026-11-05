#include "Export/PackCommand.hpp"

#include "EditorSession.hpp"
#include "Export/BuildWalk.hpp"
#include "Export/PackBuilder.hpp"
#include "Modules/ModuleBuild.hpp"

#include <format>
#include <string>
#include <span>
#include <system_error>
#include <type_traits>
#include <variant>
#include <vector>

namespace
{
    constexpr std::string_view c_PackFileName = "Game.trpak";

    std::string ToUtf8(const std::filesystem::path& path)
    {
        const std::u8string l_Text = path.u8string();

        return std::string(reinterpret_cast<const char*>(l_Text.data()), l_Text.size());
    }

    bool IsUnderway(BuildState state)
    {
        return state == BuildState::Pending || state == BuildState::Building;
    }
}

void PackCommand::Start(const std::filesystem::path& folder, bool closeWhenDone)
{
    if (m_Folder)
    {
        return;
    }

    m_Folder = folder;
    m_CloseWhenDone = closeWhenDone;
    m_Compiled.reset();
    TR_INFO("Pack: the project is written to {} once it has imported and built", ToUtf8(folder));
}

std::vector<std::string> PackCommand::CheckSettings(const Trinity::Project& project, const Trinity::AssetRegistry& registry)
{
    const Trinity::ExportSettings& l_Settings = project.GetExportSettings();
    std::vector<std::string> l_Problems;
    for (const Trinity::ExportScene& it_Scene : l_Settings.Scenes)
    {
        const Trinity::AssetRecord* l_Record = registry.Find(it_Scene.Scene);
        if (l_Record == nullptr)
        {
            l_Problems.push_back(std::format("Scene {} of the build is missing: it was deleted, or its .meta file was. Remove it from the build or restore it", it_Scene.Scene));
        }
        else if (l_Record->Importer != "Scene")
        {
            l_Problems.push_back(std::format("{} of the build is not a scene. Remove it from the build", l_Record->Path));
        }
    }

    if (l_Settings.GetBuildScenes().empty())
    {
        l_Problems.emplace_back("No scene of the build is ticked, so the game has none to start with");
    }

    if (l_Settings.Product.empty() || l_Settings.Company.empty())
    {
        l_Problems.emplace_back("The product and the company each need a name, since the game's saves and log go under them");
    }

    if (l_Settings.GraphicsAPIs.empty())
    {
        l_Problems.emplace_back("No graphics API is chosen, so the game could draw with none");
    }

    if (l_Settings.Output.empty())
    {
        l_Problems.emplace_back("No output folder is chosen");
    }

    return l_Problems;
}

// A project that did not open, or a module or scripts that did not build, ends it with the reason, as a failed write does. The scripts compile ahead of time once Forge's own build of them has ended, so the two never run at once
void PackCommand::Update()
{
    if (!m_Folder)
    {
        return;
    }

    const BuildState l_Module = m_Session.GetModule().GetStatus().State;
    const BuildState l_Scripts = m_Session.GetAssembly().GetStatus().State;
    if (m_Session.HasProject() && (m_Session.IsImportingAssets() || IsUnderway(l_Module) || IsUnderway(l_Scripts)))
    {
        return;
    }

    if (m_Session.HasProject() && l_Module != BuildState::Failed && l_Scripts != BuildState::Failed && CompileScripts())
    {
        return;
    }

    const std::filesystem::path l_Folder = *std::exchange(m_Folder, std::nullopt);
    if (!m_Session.HasProject())
    {
        TR_ERROR("Pack: no project is open, so nothing is written. Pass --project=<path> with --pack");
    }
    else if (l_Module == BuildState::Failed)
    {
        TR_ERROR("Pack: {}'s C++ module did not build, so nothing is written", m_Session.GetProject()->GetName());
    }
    else if (l_Scripts == BuildState::Failed)
    {
        TR_ERROR("Pack: {}'s C# scripts did not build, so nothing is written", m_Session.GetProject()->GetName());
    }
    else if (m_Compiled && !m_Compiled->Succeeded)
    {
        TR_ERROR("Pack: {}'s C# scripts did not compile ahead of time, so nothing is written", m_Session.GetProject()->GetName());
    }
    else if (Write(l_Folder))
    {
        TR_INFO("Pack: wrote {}", ToUtf8(l_Folder / c_PackFileName));
    }

    if (m_CloseWhenDone)
    {
        Trinity::Application::Get().Close();
    }
}

bool PackCommand::CompileScripts()
{
    const Trinity::Project& l_Project = *m_Session.GetProject();
    if (!l_Project.HasAssembly() || m_Compiled)
    {
        return false;
    }

    if (!m_Compiler.IsRunning())
    {
        AssemblyBuild::Request l_Request;
        l_Request.Name = l_Project.GetAssemblyName();
        l_Request.ProjectDirectory = l_Project.GetDirectory();
        l_Request.ScriptsDirectory = l_Project.GetScriptsDirectory();
        l_Request.AheadOfTime = true;
        m_Compiler.Start(std::move(l_Request));

        return true;
    }

    m_Compiled = m_Compiler.Update();

    return !m_Compiled;
}

bool PackCommand::Write(const std::filesystem::path& folder)
{
    const Trinity::Project& l_Project = *m_Session.GetProject();
    const Trinity::AssetRegistry& l_Registry = *m_Session.GetRegistry();

    const std::vector<std::string> l_Problems = CheckSettings(l_Project, l_Registry);
    for (const std::string& it_Problem : l_Problems)
    {
        TR_ERROR("Pack: {}, so nothing is written", it_Problem);
    }

    if (!l_Problems.empty())
    {
        return false;
    }

    const Trinity::ExportSettings& l_Export = l_Project.GetExportSettings();
    Trinity::GameSettings l_Game;
    l_Game.Product = l_Export.Product;
    l_Game.Company = l_Export.Company;
    l_Game.Version = l_Export.Version;
    l_Game.Scenes = l_Export.GetBuildScenes();
    l_Game.GraphicsAPIs = l_Export.GraphicsAPIs;
    l_Game.Window = l_Export.Window;
    l_Game.Physics = l_Project.GetPhysicsSettings();
    l_Game.Audio = l_Project.GetAudioSettings();
    l_Game.InputActions = l_Project.GetInputActions();

    const BuildContents l_Contents = BuildWalk::Gather(l_Project, l_Registry, l_Game.Scenes);
    // A UI file's link names a path, not an asset
    for (const BrokenReference& it_Broken : l_Contents.Broken)
    {
        if (it_Broken.Asset)
        {
            TR_WARN("Pack: {} names {}, which is left out: {}", it_Broken.NamedBy, it_Broken.Asset, it_Broken.Reason);
        }
        else
        {
            TR_WARN("Pack: {} {}", it_Broken.NamedBy, it_Broken.Reason);
        }
    }

    // A player with the shared engine loads the module's build from beside the pack, and a Distribution Forge builds none
    std::filesystem::path l_ModuleBuild;
    if (l_Project.HasModule() && ModuleBuild::IsSupported())
    {
        l_Game.Module = l_Project.GetModuleName();
        l_ModuleBuild = ModuleBuild::GetLibraryPath(ModuleBuild::GetBuildDirectory(l_Project.GetBuildDirectory()), l_Game.Module);
    }

    // Every player runs the scripts from their NativeAOT library, so none needs .NET installed
    std::filesystem::path l_Compiled;
    if (m_Compiled)
    {
        l_Game.Scripts = l_Project.GetAssemblyName();
        l_Compiled = m_Compiled->Assembly;
    }

    PackBuilder l_Builder;
    for (const BuildFile& it_File : l_Contents.Files)
    {
        std::visit([&l_Builder, &it_File](const auto& source)
        {
            if constexpr (std::is_same_v<std::decay_t<decltype(source)>, std::filesystem::path>)
            {
                static_cast<void>(l_Builder.AddFile(it_File.PackPath, source));
            }
            else
            {
                static_cast<void>(l_Builder.AddData(it_File.PackPath, source));
            }
        }, it_File.Source);
    }

    const std::string l_Settings = Trinity::WriteGameSettings(l_Game);
    static_cast<void>(l_Builder.AddData(Trinity::GameSettings::c_FileName, std::as_bytes(std::span(l_Settings))));

    std::error_code l_Error;
    std::filesystem::create_directories(folder, l_Error);
    const Trinity::Expected<PackSummary, std::string> l_Written = l_Builder.Write(folder / c_PackFileName);
    if (!l_Written)
    {
        TR_ERROR("Pack: {}", l_Written.GetError());

        return false;
    }

    TR_INFO("Pack: {} asset(s) from {} scene(s) in {} file(s), {} of them compressed, {} in {} on disk", l_Contents.Records.size(), l_Game.Scenes.size(), l_Written->Files, l_Written->Compressed, Trinity::Memory::FormatBytes(l_Written->Bytes), Trinity::Memory::FormatBytes(l_Written->PackBytes));

    if (!l_ModuleBuild.empty())
    {
        const std::filesystem::path l_Copy = folder / l_ModuleBuild.filename();
        if (!std::filesystem::copy_file(l_ModuleBuild, l_Copy, std::filesystem::copy_options::overwrite_existing, l_Error))
        {
            TR_ERROR("Pack: the module's build {} could not be copied beside the pack: {}", ToUtf8(l_ModuleBuild), l_Error.message());

            return false;
        }
    }

    if (!l_Compiled.empty() && !std::filesystem::copy_file(l_Compiled, folder / Trinity::GetScriptsLibraryName(), std::filesystem::copy_options::overwrite_existing, l_Error))
    {
        TR_ERROR("Pack: the scripts' library {} could not be copied beside the pack: {}", ToUtf8(l_Compiled), l_Error.message());

        return false;
    }

    return true;
}
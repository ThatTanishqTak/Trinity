#include "Export/ExportCommand.hpp"

#include "EditorSession.hpp"
#include "Export/BuildWalk.hpp"
#include "Export/PlayerResources.hpp"
#include "Modules/PortableExecutable.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <iterator>
#include <span>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>

namespace
{
    constexpr std::string_view c_PackFileName = "Game.trpak";
    constexpr std::string_view c_PrebuiltPlayer = "Trinity-Runtime";
#if defined(TR_PLATFORM_WINDOWS)
    constexpr std::string_view c_ExecutableExtension = ".exe";
#else
    constexpr std::string_view c_ExecutableExtension = "";
#endif
    // Forge's own shaders and the RHI's tests, which Forge's CMakeLists names, are compiled beside the engine's, and no game draws with them. Nor does a Distribution player draw ImGui, which it has none of
#if defined(TR_UNSHIPPED_SHADERS)
    constexpr std::string_view c_UnshippedShaders = TR_UNSHIPPED_SHADERS;
#else
    constexpr std::string_view c_UnshippedShaders = "";
#endif
    constexpr std::string_view c_ImGuiShader = "ImGui";

    std::filesystem::path FromUtf8(std::string_view text)
    {
        return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
    }

    std::string ToUtf8(const std::filesystem::path& path)
    {
        const std::u8string l_Text = path.u8string();

        return std::string(reinterpret_cast<const char*>(l_Text.data()), l_Text.size());
    }

    bool IsUnderway(BuildState state)
    {
        return state == BuildState::Pending || state == BuildState::Building;
    }

    // Whether the path is the folder or lies in it, compared as written, the paths being whole and normal
    bool IsWithin(const std::filesystem::path& path, const std::filesystem::path& folder)
    {
        const std::filesystem::path::iterator l_Differs = std::mismatch(folder.begin(), folder.end(), path.begin(), path.end()).first;

        return l_Differs == folder.end() || (std::next(l_Differs) == folder.end() && l_Differs->empty());
    }

    // A folder named as the output folder is, and as a staging or previous one beside it: its last part, without the separator a typed path may end with
    std::filesystem::path WithoutTrailingSeparator(std::filesystem::path path)
    {
        return path.has_filename() || !path.has_parent_path() || path == path.root_path() ? path : path.parent_path();
    }

    std::filesystem::path Beside(const std::filesystem::path& folder, std::string_view suffix)
    {
        std::filesystem::path l_Name = folder.filename();
        l_Name += FromUtf8(suffix);

        return folder.parent_path() / l_Name;
    }

    // By the shader's name, before its entry point and extension
    bool IsShipped(std::string_view fileName)
    {
        const std::string_view l_Name = fileName.substr(0, fileName.find('.'));
        if (l_Name.empty() || l_Name == c_ImGuiShader)
        {
            return false;
        }

        for (std::size_t l_Start = 0; l_Start <= c_UnshippedShaders.size();)
        {
            const std::size_t l_End = std::min(c_UnshippedShaders.find(',', l_Start), c_UnshippedShaders.size());
            if (c_UnshippedShaders.substr(l_Start, l_End - l_Start) == l_Name)
            {
                return false;
            }

            l_Start = l_End + 1;
        }

        return true;
    }

    // The shaders each graphics API draws with, as Trinity's build compiles them
    std::string_view GetShaderExtension(Trinity::GraphicsAPI api)
    {
        switch (api)
        {
            case Trinity::GraphicsAPI::D3D12:
            {
                return ".dxil";
            }
            case Trinity::GraphicsAPI::Vulkan:
            {
                return ".spv";
            }
            case Trinity::GraphicsAPI::None:
            case Trinity::GraphicsAPI::Metal:
            {
                break;
            }
        }

        return {};
    }

    std::string JoinAPIs(std::span<const Trinity::GraphicsAPI> apis)
    {
        std::string l_Text;
        for (const Trinity::GraphicsAPI it_API : apis)
        {
            l_Text += l_Text.empty() ? "" : " and ";
            l_Text += Trinity::ToString(it_API);
        }

        return l_Text;
    }
}

ExportCommand::~ExportCommand()
{
    Stop();
}

void ExportCommand::Start(const std::filesystem::path& folder, bool closeWhenDone)
{
    if (m_Folder)
    {
        return;
    }

    std::error_code l_Error;
    const std::filesystem::path l_Folder = WithoutTrailingSeparator(std::filesystem::absolute(folder, l_Error).lexically_normal());
    m_Folder = l_Folder;
    m_CloseWhenDone = closeWhenDone;
    m_Stage = Stage::Waiting;
    m_Started = std::chrono::steady_clock::now();
    m_Player.clear();
    m_Compiled.clear();
    m_PackKinds.clear();
    m_Written.clear();
    TR_INFO("Export: the project is exported to {} once it has imported and built", ToUtf8(l_Folder));
}

void ExportCommand::Cancel()
{
    if (!m_Folder)
    {
        return;
    }

    Stop();
    TR_WARN("Export: cancelled, so {} is as the last export left it", ToUtf8(*m_Folder));
    Finish(false);
}

std::vector<std::string> ExportCommand::CheckSettings(const Trinity::Project& project, const Trinity::AssetRegistry& registry)
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

    if (l_Settings.Icon)
    {
        const Trinity::AssetRecord* l_Icon = registry.Find(l_Settings.Icon);
        if (l_Icon == nullptr)
        {
            l_Problems.push_back(std::format("The icon {} is missing: it was deleted, or its .meta file was. Choose another or clear it", l_Settings.Icon));
        }
        else if (l_Icon->Importer != Trinity::TextureAsset::c_AssetType)
        {
            l_Problems.push_back(std::format("{} is not a texture, so it cannot be the icon. Choose a texture or clear it", l_Icon->Path));
        }
    }

    if (l_Settings.GraphicsAPIs.empty())
    {
        l_Problems.emplace_back("No graphics API is chosen, so the game could draw with none");
    }

    if (l_Settings.Output.empty())
    {
        l_Problems.emplace_back("No output folder is chosen");

        return l_Problems;
    }

    // An export replaces its folder whole, so it may be neither the project's folder, nor one that holds it, nor one of the folders the project keeps its own files in
    const std::filesystem::path l_Output = WithoutTrailingSeparator(project.GetExportDirectory());
    const std::filesystem::path l_Project = WithoutTrailingSeparator(project.GetDirectory().lexically_normal());
    if (IsWithin(l_Project, l_Output))
    {
        l_Problems.push_back(std::format("The output folder {} holds the project, and an export replaces its folder whole. Choose a folder of its own, such as Builds", ToUtf8(l_Output)));
    }
    else
    {
        const std::array<std::filesystem::path, 5> l_ProjectFolders{ project.GetAssetsDirectory(), l_Project / "Cache", project.GetBuildDirectory(), project.GetSourceDirectory(), project.GetScriptsDirectory() };
        for (const std::filesystem::path& it_Folder : l_ProjectFolders)
        {
            if (IsWithin(l_Output, WithoutTrailingSeparator(it_Folder.lexically_normal())))
            {
                l_Problems.push_back(std::format("The output folder {} is in the project's {} folder, and an export replaces its folder whole. Choose a folder of its own, such as Builds", ToUtf8(l_Output), ToUtf8(it_Folder.filename())));

                break;
            }
        }
    }

    return l_Problems;
}

std::filesystem::path ExportCommand::GetDistributionDirectory()
{
    const std::filesystem::path l_Forge = Trinity::Process::GetExecutableDirectory();
    const std::filesystem::path l_Build = l_Forge.parent_path();
    const std::string l_Name = ToUtf8(l_Build.filename());
    const std::size_t l_Dash = l_Name.find('-');
    if (l_Dash == std::string::npos)
    {
        return l_Forge;
    }

    return l_Build.parent_path() / FromUtf8(std::format("Distribution{}", std::string_view(l_Name).substr(l_Dash))) / l_Forge.filename();
}

std::string ExportCommand::GetExecutableName(std::string_view product)
{
    constexpr std::string_view c_Refused = "<>:\"/\\|?*";
    std::string l_Name;
    for (const char it_Character : product)
    {
        if (static_cast<unsigned char>(it_Character) >= 0x20 && static_cast<unsigned char>(it_Character) != 0x7F && c_Refused.find(it_Character) == std::string_view::npos)
        {
            l_Name += it_Character;
        }
    }

    const std::size_t l_First = l_Name.find_first_not_of(' ');
    l_Name.erase(0, l_First == std::string::npos ? l_Name.size() : l_First);
    while (!l_Name.empty() && (l_Name.back() == ' ' || l_Name.back() == '.'))
    {
        l_Name.pop_back();
    }

    if (l_Name.empty())
    {
        l_Name = "Game";
    }

    // Windows opens the device for these names, whatever follows the first dot
    std::string l_Device = l_Name.substr(0, l_Name.find('.'));
    while (!l_Device.empty() && l_Device.back() == ' ')
    {
        l_Device.pop_back();
    }

    std::ranges::transform(l_Device, l_Device.begin(), [](char character) { return static_cast<char>(std::toupper(static_cast<unsigned char>(character))); });
    constexpr std::array<std::string_view, 4> c_Devices{ "CON", "PRN", "AUX", "NUL" };
    const bool l_Numbered = l_Device.size() == 4 && (l_Device.starts_with("COM") || l_Device.starts_with("LPT")) && l_Device[3] >= '0' && l_Device[3] <= '9';
    if (l_Numbered || std::ranges::find(c_Devices, l_Device) != c_Devices.end())
    {
        l_Name.insert(0, "_");
    }

    return l_Name.append(c_ExecutableExtension);
}

std::string ExportCommand::GetStatus() const
{
    switch (m_Stage)
    {
        case Stage::Waiting:
        {
            return "Waiting for the project to import and build";
        }
        case Stage::BuildingPlayer:
        {
            return m_PlayerBuild.IsRunning() ? std::format("Building the Distribution player with {}", m_Session.GetProject()->GetModuleName()) : std::string("Taking the prebuilt player");
        }
        case Stage::CompilingScripts:
        {
            return m_Compiler.IsRunning() ? std::format("Compiling {} ahead of time", m_Session.GetProject()->GetAssemblyName()) : std::string("Cooking what the build reaches");
        }
        case Stage::Packing:
        {
            return std::format("Packing {} of {} file(s)", m_Packing ? m_Packing->Written.load(std::memory_order_relaxed) : m_PackFiles, m_PackFiles);
        }
    }

    return {};
}

std::optional<float> ExportCommand::GetProgress() const
{
    if (m_Stage != Stage::Packing || m_PackFiles == 0)
    {
        return std::nullopt;
    }

    const std::size_t l_Written = m_Packing ? m_Packing->Written.load(std::memory_order_relaxed) : m_PackFiles;

    return static_cast<float>(l_Written) / static_cast<float>(m_PackFiles);
}

// A project that did not open, a module or scripts that did not build, and settings that cannot make a game end it with the reason, as a failed stage does. Its own builds start once Forge's have ended, so the two never run at once
void ExportCommand::Update()
{
    if (!m_Folder)
    {
        return;
    }

    switch (m_Stage)
    {
        case Stage::Waiting:
        {
            const BuildState l_Module = m_Session.GetModule().GetStatus().State;
            const BuildState l_Scripts = m_Session.GetAssembly().GetStatus().State;
            if (m_Session.HasProject() && (m_Session.IsImportingAssets() || IsUnderway(l_Module) || IsUnderway(l_Scripts)))
            {
                return;
            }

            if (!m_Session.HasProject())
            {
                Fail("no project is open. Pass --project=<path> with --export");

                return;
            }

            const Trinity::Project& l_Project = *m_Session.GetProject();
            if (l_Module == BuildState::Failed)
            {
                Fail(std::format("{}'s C++ module did not build", l_Project.GetName()));

                return;
            }

            if (l_Scripts == BuildState::Failed)
            {
                Fail(std::format("{}'s C# scripts did not build", l_Project.GetName()));

                return;
            }

            const std::vector<std::string> l_Problems = CheckSettings(l_Project, *m_Session.GetRegistry());
            for (const std::string& it_Problem : l_Problems)
            {
                TR_ERROR("Export: {}", it_Problem);
            }

            if (!l_Problems.empty())
            {
                Fail("the build's settings cannot make a game");

                return;
            }

            if (l_Project.GetExportSettings().Development)
            {
                TR_WARN("Export: a Development Build cannot be exported yet, so {} is exported as players get it, in Distribution", l_Project.GetName());
            }

            // What a failed export left beside the output folder goes first
            std::error_code l_Error;
            m_Staging = Beside(*m_Folder, ".exporting");
            std::filesystem::remove_all(m_Staging, l_Error);
            std::filesystem::create_directories(m_Staging, l_Error);
            if (l_Error)
            {
                Fail(std::format("its staging folder {} could not be made: {}", ToUtf8(m_Staging), l_Error.message()));

                return;
            }

            StartPlayer();

            return;
        }
        case Stage::BuildingPlayer:
        {
            if (m_PlayerBuild.IsRunning())
            {
                const std::optional<ModuleBuild::Result> l_Built = m_PlayerBuild.Update();
                if (!l_Built)
                {
                    return;
                }

                if (!l_Built->Succeeded)
                {
                    Fail(std::format("{}'s player did not build", m_Session.GetProject()->GetName()));

                    return;
                }

                m_Player = l_Built->Output;
            }

            StartScripts();

            return;
        }
        case Stage::CompilingScripts:
        {
            if (m_Compiler.IsRunning())
            {
                const std::optional<AssemblyBuild::Result> l_Compiled = m_Compiler.Update();
                if (!l_Compiled)
                {
                    return;
                }

                if (!l_Compiled->Succeeded)
                {
                    Fail(std::format("{}'s C# scripts did not compile ahead of time", m_Session.GetProject()->GetName()));

                    return;
                }

                m_Compiled = l_Compiled->Assembly;
            }

            StartPacking();

            return;
        }
        case Stage::Packing:
        {
            if (!m_Packing->Done.load(std::memory_order_acquire))
            {
                return;
            }

            Trinity::JobSystem::Wait(m_PackJobs);
            const Trinity::Expected<PackSummary, std::string> l_Packed = std::move(*m_Packing->Result);
            m_Packing.reset();
            if (!l_Packed)
            {
                Fail(std::format("{} could not be written: {}", c_PackFileName, l_Packed.GetError()));

                return;
            }

            m_Written.push_back({ FromUtf8(c_PackFileName), "Pack" });
            if (!CopyFiles() || !ReplaceOutput())
            {
                return;
            }

            Report(*l_Packed);
            Finish(true);

            return;
        }
    }
}

// The module linked into a Distribution player against Trinity's Distribution package, in its own build tree beside Forge's, or the prebuilt player for a project without one. Configuring Trinity writes the package whether or not Distribution was built, so the prebuilt player is what shows the build is there
void ExportCommand::StartPlayer()
{
    m_Stage = Stage::BuildingPlayer;
    const Trinity::Project& l_Project = *m_Session.GetProject();
    const std::filesystem::path l_Distribution = GetDistributionDirectory();
    const std::filesystem::path l_Prebuilt = l_Distribution / FromUtf8(std::format("{}{}", c_PrebuiltPlayer, c_ExecutableExtension));
    std::error_code l_Error;
    if (!std::filesystem::exists(l_Prebuilt, l_Error))
    {
        Fail(std::format("Trinity's Distribution build, which holds the players, is not at {}. Build Trinity's Distribution configuration", ToUtf8(l_Distribution)));

        return;
    }

    if (!l_Project.HasModule())
    {
        m_Player = l_Prebuilt;

        return;
    }

    ModuleBuild::Request l_Request;
    l_Request.Name = l_Project.GetModuleName();
    l_Request.ProjectDirectory = l_Project.GetDirectory();
    l_Request.SourceDirectory = l_Project.GetSourceDirectory();
    l_Request.BuildDirectory = l_Project.GetBuildDirectory() / "Distribution";
    l_Request.Configuration = "Distribution";
    l_Request.EngineDirectory = l_Distribution;
    l_Request.Output = l_Request.BuildDirectory / "Player" / FromUtf8(std::format("{}{}", l_Request.Name, c_ExecutableExtension));
    m_PlayerBuild.Start(std::move(l_Request));
}

// Every player runs the scripts from their NativeAOT library, so none needs .NET installed
void ExportCommand::StartScripts()
{
    m_Stage = Stage::CompilingScripts;
    const Trinity::Project& l_Project = *m_Session.GetProject();
    if (!l_Project.HasAssembly())
    {
        return;
    }

    AssemblyBuild::Request l_Request;
    l_Request.Name = l_Project.GetAssemblyName();
    l_Request.ProjectDirectory = l_Project.GetDirectory();
    l_Request.ScriptsDirectory = l_Project.GetScriptsDirectory();
    l_Request.AheadOfTime = true;
    m_Compiler.Start(std::move(l_Request));
}

// What the build reaches is gathered and cooked here, as templates are found through the serializer's resolver on the main thread, and written on a job
void ExportCommand::StartPacking()
{
    m_Stage = Stage::Packing;
    const Trinity::Project& l_Project = *m_Session.GetProject();
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
    // Linked into the player, which says so should a player that cannot load it be given the pack
    l_Game.Module = l_Project.GetModuleName();
    if (!m_Compiled.empty())
    {
        l_Game.Scripts = l_Project.GetAssemblyName();
    }

    const BuildContents l_Contents = BuildWalk::Gather(l_Project, *m_Session.GetRegistry(), l_Game.Scenes);
    // A UI file's link names a path, not an asset
    for (const BrokenReference& it_Broken : l_Contents.Broken)
    {
        if (it_Broken.Asset)
        {
            TR_WARN("Export: {} names {}, which is left out: {}", it_Broken.NamedBy, it_Broken.Asset, it_Broken.Reason);
        }
        else
        {
            TR_WARN("Export: {} {}", it_Broken.NamedBy, it_Broken.Reason);
        }
    }

    m_Packing = std::make_unique<Packing>();
    PackBuilder& l_Builder = m_Packing->Builder;
    m_PackKinds.clear();
    const auto a_Count = [this](std::string_view packPath, std::uint64_t size)
    {
        const std::size_t l_Slash = packPath.find('/');
        std::pair<std::size_t, std::uint64_t>& l_Kind = m_PackKinds[l_Slash == std::string_view::npos ? std::string("Game") : std::string(packPath.substr(0, l_Slash))];
        ++l_Kind.first;
        l_Kind.second += size;
    };

    for (const BuildFile& it_File : l_Contents.Files)
    {
        std::visit([&](const auto& source)
        {
            if constexpr (std::is_same_v<std::decay_t<decltype(source)>, std::filesystem::path>)
            {
                std::error_code l_Error;
                const std::uintmax_t l_Size = std::filesystem::file_size(source, l_Error);
                a_Count(it_File.PackPath, l_Error ? 0 : static_cast<std::uint64_t>(l_Size));
                static_cast<void>(l_Builder.AddFile(it_File.PackPath, source));
            }
            else
            {
                a_Count(it_File.PackPath, source.size());
                static_cast<void>(l_Builder.AddData(it_File.PackPath, source));
            }
        }, it_File.Source);
    }

    const std::string l_Settings = Trinity::WriteGameSettings(l_Game);
    a_Count(Trinity::GameSettings::c_FileName, l_Settings.size());
    static_cast<void>(l_Builder.AddData(Trinity::GameSettings::c_FileName, std::as_bytes(std::span(l_Settings))));

    m_PackFiles = l_Builder.GetFileCount();
    m_Assets = l_Contents.Records.size();
    m_Scenes = l_Game.Scenes.size();
    m_Packing->Output = m_Staging / FromUtf8(c_PackFileName);
    m_Packing->Options.CacheDirectory = l_Project.GetBuildDirectory() / "Export" / "Pack";
    m_Packing->Options.Cancel = &m_Packing->Cancel;
    m_Packing->Options.Written = &m_Packing->Written;

    Packing* l_Packing = m_Packing.get();
    Trinity::JobSystem::Submit([l_Packing]
    {
        l_Packing->Result = l_Packing->Builder.Write(l_Packing->Output, l_Packing->Options);
        l_Packing->Done.store(true, std::memory_order_release);
    }, &m_PackJobs);
}

// The player named for the product, with its icon and version on Windows, the scripts' library and the shaders for the export's graphics APIs, then on Windows the Agility SDK runtime where D3D12SDKPath points and GameInput's redistributable, which Microsoft asks a game to ship, all from Trinity's Distribution build
bool ExportCommand::CopyFiles()
{
    const Trinity::ExportSettings& l_Export = m_Session.GetProject()->GetExportSettings();
    const std::filesystem::path l_Distribution = GetDistributionDirectory();
    if (!CopyPlayer(FromUtf8(GetExecutableName(l_Export.Product))))
    {
        return false;
    }

    if (!m_Compiled.empty() && !CopyFile(m_Compiled, FromUtf8(Trinity::GetScriptsLibraryName()), "C# scripts"))
    {
        return false;
    }

    const std::filesystem::path l_Shaders = l_Distribution / "Engine" / "shaders";
    std::vector<std::filesystem::path> l_Found;
    std::error_code l_Error;
    for (std::filesystem::directory_iterator it_Entry(l_Shaders, l_Error), it_End; !l_Error && it_Entry != it_End; it_Entry.increment(l_Error))
    {
        const std::string l_Name = ToUtf8(it_Entry->path().filename());
        const std::string l_Extension = ToUtf8(it_Entry->path().extension());
        const bool l_Drawn = std::ranges::any_of(l_Export.GraphicsAPIs, [&l_Extension](Trinity::GraphicsAPI api) { return GetShaderExtension(api) == l_Extension; });
        if (l_Drawn && IsShipped(l_Name) && it_Entry->is_regular_file())
        {
            l_Found.push_back(it_Entry->path());
        }
    }

    if (l_Found.empty())
    {
        Fail(std::format("Trinity's Distribution build has no shaders for {} in {}", JoinAPIs(l_Export.GraphicsAPIs), ToUtf8(l_Shaders)));

        return false;
    }

    std::ranges::sort(l_Found);
    for (const std::filesystem::path& it_Shader : l_Found)
    {
        if (!CopyFile(it_Shader, std::filesystem::path("Engine") / "shaders" / it_Shader.filename(), "Shaders"))
        {
            return false;
        }
    }

#if defined(TR_PLATFORM_WINDOWS)
    if (std::ranges::find(l_Export.GraphicsAPIs, Trinity::GraphicsAPI::D3D12) != l_Export.GraphicsAPIs.end())
    {
        const std::filesystem::path l_Agility = l_Distribution / "D3D12" / "D3D12Core.dll";
        if (!std::filesystem::exists(l_Agility, l_Error))
        {
            TR_WARN("Export: Trinity's Distribution build has no D3D12 Agility SDK runtime at {}, so the game draws with the D3D12 runtime Windows has. Scripts/Bootstrap.ps1 downloads it", ToUtf8(l_Agility));
        }
        else if (!CopyFile(l_Agility, std::filesystem::path("D3D12") / "D3D12Core.dll", "D3D12 Agility SDK"))
        {
            return false;
        }
    }

    const std::filesystem::path l_GameInput = l_Distribution / "Redist" / "GameInputRedist.msi";
    if (!std::filesystem::exists(l_GameInput, l_Error))
    {
        TR_WARN("Export: Trinity's Distribution build has no GameInput redistributable at {}, so a player without GameInput installed has no gamepads. Scripts/Bootstrap.ps1 downloads it", ToUtf8(l_GameInput));
    }
    else if (!CopyFile(l_GameInput, std::filesystem::path("Redist") / "GameInputRedist.msi", "GameInput redistributable"))
    {
        return false;
    }
#endif

    return true;
}

// A Windows player, a PE image, is given its version and the icon the settings name, which a player of another platform has no place for, so it goes in as it is
bool ExportCommand::CopyPlayer(const std::filesystem::path& to)
{
    std::optional<std::vector<char>> l_Image = PortableExecutable::ReadImage(m_Player);
    if (!l_Image)
    {
        Fail(std::format("the player {} could not be read", ToUtf8(m_Player)));

        return false;
    }

    if (!PortableExecutable::IsImage(*l_Image))
    {
        return CopyFile(m_Player, to, "Player");
    }

    const Trinity::Project& l_Project = *m_Session.GetProject();
    const Trinity::ExportSettings& l_Export = l_Project.GetExportSettings();
    if (!PlayerResources::ParseVersion(l_Export.Version).Numeric)
    {
        TR_WARN("Export: the version {} does not start with a number, so Windows shows the file's version as 0.0.0.0. A version such as 1.0 or 1.2.3-beta gives its numbers", l_Export.Version);
    }

    std::vector<PortableExecutable::Resource> l_Resources{ PlayerResources::MakeVersion(l_Export, ToUtf8(to.filename())) };
    if (l_Export.Icon)
    {
        // The settings were checked as the export began, so the icon is a texture of the project
        const Trinity::AssetRecord& l_Record = *m_Session.GetRegistry()->Find(l_Export.Icon);
        Trinity::Expected<std::vector<PortableExecutable::Resource>, std::string> l_Icon = PlayerResources::MakeIcon(l_Project.ToNativePath(l_Record.Path));
        if (!l_Icon)
        {
            Fail(std::format("the icon {} could not be made: {}", l_Record.Path, l_Icon.GetError()));

            return false;
        }

        l_Resources.insert(l_Resources.end(), std::make_move_iterator(l_Icon->begin()), std::make_move_iterator(l_Icon->end()));
    }

    const Trinity::Expected<void, std::string> l_Written = PortableExecutable::SetResources(*l_Image, l_Resources);
    if (!l_Written)
    {
        Fail(std::format("the player's icon and version could not be written into {}: {}", ToUtf8(m_Player), l_Written.GetError()));

        return false;
    }

    if (!PortableExecutable::WriteImage(m_Staging / to, *l_Image))
    {
        Fail(std::format("{} could not be written", ToUtf8(m_Staging / to)));

        return false;
    }

    m_Written.push_back({ to, "Player" });

    return true;
}

bool ExportCommand::CopyFile(const std::filesystem::path& from, const std::filesystem::path& to, std::string kind)
{
    const std::filesystem::path l_Target = m_Staging / to;
    std::error_code l_Error;
    std::filesystem::create_directories(l_Target.parent_path(), l_Error);
    if (!std::filesystem::copy_file(from, l_Target, std::filesystem::copy_options::overwrite_existing, l_Error))
    {
        Fail(std::format("{} could not be copied into it: {}", ToUtf8(from), l_Error.message()));

        return false;
    }

    m_Written.push_back({ to, std::move(kind) });

    return true;
}

// The output folder is moved aside, the staging folder takes its place, and only then is the old one removed, so a rename that fails, as Windows refuses one while the game runs from the folder, puts the old one back
bool ExportCommand::ReplaceOutput()
{
    const std::filesystem::path& l_Output = *m_Folder;
    const std::filesystem::path l_Previous = Beside(l_Output, ".previous");
    std::error_code l_Error;
    std::filesystem::remove_all(l_Previous, l_Error);
    const bool l_Replacing = std::filesystem::exists(l_Output, l_Error);
    if (l_Replacing)
    {
        std::filesystem::rename(l_Output, l_Previous, l_Error);
        if (l_Error)
        {
            Fail(std::format("{} could not be replaced, as something holds it open, such as the game running from it: {}", ToUtf8(l_Output), l_Error.message()));

            return false;
        }
    }

    std::filesystem::rename(m_Staging, l_Output, l_Error);
    if (l_Error)
    {
        const std::string l_Reason = l_Error.message();
        if (l_Replacing)
        {
            std::filesystem::rename(l_Previous, l_Output, l_Error);
        }

        Fail(std::format("the staging folder could not take the place of {}: {}", ToUtf8(l_Output), l_Reason));

        return false;
    }

    m_Staging.clear();
    if (l_Replacing && std::filesystem::remove_all(l_Previous, l_Error) == static_cast<std::uintmax_t>(-1))
    {
        TR_WARN("Export: the last export, moved to {}, could not be removed: {}", ToUtf8(l_Previous), l_Error.message());
    }

    return true;
}

// The folder's files by kind, in the order they were written, then what the pack holds by the folder each file lies in
void ExportCommand::Report(const PackSummary& pack) const
{
    const std::filesystem::path& l_Output = *m_Folder;
    std::vector<std::pair<std::string, std::pair<std::size_t, std::uint64_t>>> l_Kinds;
    std::uint64_t l_Total = 0;
    for (const WrittenFile& it_File : m_Written)
    {
        std::error_code l_Error;
        const std::uintmax_t l_Size = std::filesystem::file_size(l_Output / it_File.Path, l_Error);
        const std::uint64_t l_Bytes = l_Error ? 0 : static_cast<std::uint64_t>(l_Size);
        auto it_Kind = std::ranges::find(l_Kinds, it_File.Kind, &decltype(l_Kinds)::value_type::first);
        if (it_Kind == l_Kinds.end())
        {
            it_Kind = l_Kinds.insert(l_Kinds.end(), { it_File.Kind,{ 0, 0 } });
        }

        ++it_Kind->second.first;
        it_Kind->second.second += l_Bytes;
        l_Total += l_Bytes;
    }

    TR_INFO("Export: {} exported to {} in {:.1f} s, {} file(s) in {}", m_Session.GetProject()->GetExportSettings().Product, ToUtf8(l_Output), GetSeconds(), m_Written.size(), Trinity::Memory::FormatBytes(l_Total));
    for (const auto& [it_Kind, it_Count] : l_Kinds)
    {
        TR_INFO("Export:   {}: {} in {} file(s)", it_Kind, Trinity::Memory::FormatBytes(it_Count.second), it_Count.first);
    }

    TR_INFO("Export: {} holds {} asset(s) from {} scene(s), {} file(s) of {} in {}, {} of them compressed, {} from the cache", c_PackFileName, m_Assets, m_Scenes, pack.Files, Trinity::Memory::FormatBytes(pack.Bytes), Trinity::Memory::FormatBytes(pack.PackBytes), pack.Compressed, pack.Cached);
    for (const auto& [it_Kind, it_Count] : m_PackKinds)
    {
        TR_INFO("Export:   {}: {} in {} file(s)", it_Kind, Trinity::Memory::FormatBytes(it_Count.second), it_Count.first);
    }
}

void ExportCommand::Fail(std::string_view reason)
{
    TR_ERROR("Export: {}, so nothing is exported and {} is as the last export left it", reason, m_Folder ? ToUtf8(*m_Folder) : std::string());
    Stop();
    Finish(false);
}

void ExportCommand::Finish(bool succeeded)
{
    m_Folder.reset();
    m_Stage = Stage::Waiting;
    m_Written.clear();
    if (m_CloseWhenDone)
    {
        Trinity::Application::Get().Close(succeeded ? 0 : 1);
    }
}

void ExportCommand::Stop()
{
    m_PlayerBuild.Cancel();
    m_Compiler.Cancel();
    if (m_Packing)
    {
        m_Packing->Cancel.store(true, std::memory_order_relaxed);
        Trinity::JobSystem::Wait(m_PackJobs);
        m_Packing.reset();
    }

    if (!m_Staging.empty())
    {
        std::error_code l_Error;
        std::filesystem::remove_all(m_Staging, l_Error);
        m_Staging.clear();
    }
}
#include "Modules/AssemblyHost.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace
{
    constexpr std::string_view c_ProjectTemplate = R"Template(<Project Sdk="Microsoft.NET.Sdk">

  <!-- @NAME@, the C# scripts of @PROJECT@. Forge builds it into Build/Scripts and loads it after each good build. An IDE builds it the same way, against the Trinity-ScriptCore of the Forge that last opened the project, which Trinity.Local.props names -->
  <PropertyGroup>
    <TargetFramework>net10.0</TargetFramework>
    <AssemblyName>@NAME@</AssemblyName>
    <RootNamespace>@NAME@</RootNamespace>
    <Nullable>enable</Nullable>
    <ImplicitUsings>disable</ImplicitUsings>
    <IsAotCompatible>true</IsAotCompatible>
    <EnableDynamicLoading>true</EnableDynamicLoading>
    <SatelliteResourceLanguages>en</SatelliteResourceLanguages>
  </PropertyGroup>

  <!-- The engine's API, which the engine gives the game as it runs, so it is not copied beside it, and the source generator that describes each script class to the engine -->
  <ItemGroup>
    <Reference Include="Trinity-ScriptCore" HintPath="$(TrinityScriptingDirectory)/Trinity-ScriptCore.dll" Private="false" />
    <Analyzer Include="$(TrinityScriptingDirectory)/Trinity-ScriptGenerator.dll" />
  </ItemGroup>

</Project>
)Template";

    constexpr std::string_view c_BuildPropsTemplate = R"Template(<Project>

  <!-- Every build of the scripts, Forge's and an IDE's alike, goes to Build/Scripts beside the project, which git leaves out -->
  <PropertyGroup>
    <ArtifactsPath>$(MSBuildThisFileDirectory)../Build/Scripts</ArtifactsPath>
  </PropertyGroup>

  <!-- Written by Forge as it opens the project, with the folder of its Trinity-ScriptCore, which Forge's own builds name instead -->
  <Import Project="$(MSBuildThisFileDirectory)Trinity.Local.props" Condition="Exists('$(MSBuildThisFileDirectory)Trinity.Local.props')" />

  <Target Name="TrinityCheckEngine" BeforeTargets="ResolveAssemblyReferences" Condition="'$(TrinityScriptingDirectory)' == ''">
    <Error Text="Trinity.Local.props is missing, so the scripts do not know where Trinity-ScriptCore is. Open the project in Forge once, which writes it" />
  </Target>

</Project>
)Template";

    constexpr std::string_view c_SourceTemplate = R"Template(using Trinity;

namespace @NAME@
{
    // A script class derives from Trinity.Script. Its fields are its public fields and those marked [Field], [Hide] keeps a public one out, and their defaults are the values a new instance has. Add Script in Properties lists it once the scripts are built
    //
    // public class Mover : Script
    // {
    //     public float Speed = 1.0f;
    //
    //     public override void OnUpdate(float timestep)
    //     {
    //         Log.Info($"{Entity} moves at {Speed}");
    //     }
    // }
}
)Template";

    constexpr std::string_view c_SolutionTemplate = R"Template(<Solution>
  <Project Path="Scripts/@NAME@.csproj" />
</Solution>
)Template";

    constexpr std::string_view c_GitIgnore = "Trinity.Local.props\n";

    std::string FillTemplate(std::string_view text, std::string_view name, std::string_view project)
    {
        std::string l_Text(text);
        for (const auto& [it_Key, it_Value] : { std::pair<std::string_view, std::string_view>{ "@NAME@", name },{ "@PROJECT@", project } })
        {
            for (std::size_t l_At = l_Text.find(it_Key); l_At != std::string::npos; l_At = l_Text.find(it_Key, l_At + it_Value.size()))
            {
                l_Text.replace(l_At, it_Key.size(), it_Value);
            }
        }

        return l_Text;
    }

    std::string ToUtf8(const std::filesystem::path& path)
    {
        const std::u8string l_Text = path.u8string();

        return std::string(reinterpret_cast<const char*>(l_Text.data()), l_Text.size());
    }

    std::string EscapeXml(std::string_view text)
    {
        std::string l_Escaped;
        for (const char it_Character : text)
        {
            switch (it_Character)
            {
                case '&': l_Escaped += "&amp;"; break;
                case '<': l_Escaped += "&lt;"; break;
                case '>': l_Escaped += "&gt;"; break;
                default: l_Escaped += it_Character; break;
            }
        }

        return l_Escaped;
    }

    // A save often writes a file more than once, and an editor may save several at once, so a build waits this long after the last change. A build from an IDE writes its assembly and then its PDB, which a load waits for too
    constexpr std::chrono::milliseconds c_SettleTime{ 500 };

    // A file dotnet build reads: the sources, the project and the props and targets beside it, but not the props Forge writes itself
    bool IsBuildInput(const std::filesystem::path& path)
    {
        constexpr std::array<std::string_view, 4> c_Extensions{ ".cs", ".csproj", ".props", ".targets" };
        if (path.filename() == "Trinity.Local.props")
        {
            return false;
        }

        std::string l_Extension = path.extension().string();
        std::ranges::transform(l_Extension, l_Extension.begin(), [](char character) { return static_cast<char>(std::tolower(static_cast<unsigned char>(character))); });

        return std::ranges::find(c_Extensions, l_Extension) != c_Extensions.end();
    }

    // The copy and its PDB. Windows keeps a copy open while a context that was not collected still holds it, or a debugger its PDB, and both go with a later project open
    void RemoveCopy(const std::filesystem::path& copy)
    {
        if (copy.empty())
        {
            return;
        }

        std::error_code l_Error;
        std::filesystem::remove(copy, l_Error);
        std::filesystem::remove(std::filesystem::path(copy).replace_extension(".pdb"), l_Error);
    }
}

AssemblyHost::~AssemblyHost()
{
    Close();
}

// A project with scripts builds them once as it opens, which does nothing but load them when nothing changed since the last build. Copies an earlier Forge left are removed first
void AssemblyHost::Open(Trinity::Project& project)
{
    Close();
    m_Project = &project;
    if (!project.HasAssembly())
    {
        return;
    }

    RemoveStaleCopies();
    if (!IsSupported())
    {
        TR_WARN("Forge: {} has the C# scripts {}, which cannot build or load, since C# is off. The log says why when Forge starts", project.GetName(), project.GetAssemblyName());

        return;
    }

    WriteLocalProps();
    Watch();
    m_BuildRequested = true;
}

void AssemblyHost::Close()
{
    m_Build.Cancel();
    if (IsLoaded() && Unload())
    {
        RemoveCopy(m_LoadedCopy);
    }

    m_LoadedCopy.clear();
    m_TriedBuildTime.reset();
    m_SourceWatcher.Stop();
    m_OutputWatcher.Stop();
    m_Project = nullptr;
    m_SourcesChangedAt.reset();
    m_OutputChangedAt.reset();
    m_BuildRequested = false;
}

void AssemblyHost::Update()
{
    if (m_Project == nullptr || !m_Project->HasAssembly() || !IsSupported())
    {
        return;
    }

    const Clock::time_point l_Now = Clock::now();
    if (const Trinity::DirectoryChanges l_Changes = m_SourceWatcher.Poll(); l_Changes.Overflowed || std::ranges::any_of(l_Changes.Paths, IsBuildInput))
    {
        m_SourcesChangedAt = l_Now;
    }

    const std::filesystem::path l_Built = GetBuiltPath();
    if (const Trinity::DirectoryChanges l_Changes = m_OutputWatcher.Poll(); l_Changes.Overflowed || std::ranges::any_of(l_Changes.Paths, [&l_Built](const std::filesystem::path& path) { return path.filename() == l_Built.filename(); }))
    {
        m_OutputChangedAt = l_Now;
    }

    if (const std::optional<AssemblyBuild::Result> l_Result = m_Build.Update(); l_Result && l_Result->Succeeded)
    {
        Load(l_Result->Assembly);
    }

    if (m_SourcesChangedAt && l_Now - *m_SourcesChangedAt >= c_SettleTime)
    {
        m_SourcesChangedAt.reset();
        m_BuildRequested = true;
    }

    // A change while a build runs builds again once it ends
    if (m_BuildRequested && !m_Build.IsRunning())
    {
        m_BuildRequested = false;
        StartBuild();
    }

    // A build Forge did not run, such as an IDE's, once its files have settled. Forge's own builds load as they end, so the output they leave is the build loaded already
    if (m_OutputChangedAt && l_Now - *m_OutputChangedAt >= c_SettleTime && !m_Build.IsRunning())
    {
        m_OutputChangedAt.reset();
        std::error_code l_Error;
        const std::filesystem::file_time_type l_Time = std::filesystem::last_write_time(l_Built, l_Error);
        if (!l_Error && l_Time != m_TriedBuildTime)
        {
            TR_INFO("Forge: {} was built outside Forge, so it reloads", m_Project->GetAssemblyName());
            Load(l_Built);
        }
    }
}

bool AssemblyHost::Create(Trinity::Project& project)
{
    if (!IsSupported())
    {
        TR_ERROR("Forge: C# is off, so a C# project cannot be created. The log says why when Forge starts");

        return false;
    }

    if (project.HasAssembly())
    {
        return false;
    }

    const std::string l_Name = Trinity::Project::MakeModuleName(project.GetName());
    const std::array<std::pair<std::string, std::string_view>, 5> l_Files{ {
        { std::format("Scripts/{}.csproj", l_Name), c_ProjectTemplate },
        { "Scripts/Directory.Build.props", c_BuildPropsTemplate },
        { std::format("Scripts/{}.cs", l_Name), c_SourceTemplate },
        { "Scripts/.gitignore", c_GitIgnore },
        { std::format("{}.slnx", l_Name), c_SolutionTemplate }
    } };

    for (const auto& [it_File, it_Template] : l_Files)
    {
        const std::string l_Path = std::format("{}/{}", Trinity::Project::c_ProjectMount, it_File);
        if (Trinity::FileSystem::Exists(l_Path))
        {
            TR_INFO("Forge: {} is already there, so it is kept", it_File);

            continue;
        }

        if (const Trinity::Expected<void, Trinity::FileError> l_Written = Trinity::FileSystem::WriteText(l_Path, FillTemplate(it_Template, l_Name, project.GetName())); !l_Written)
        {
            TR_ERROR("Forge: {} could not be written: {}", it_File, Trinity::ToString(l_Written.GetError()));

            return false;
        }
    }

    project.SetAssemblyName(l_Name);
    TR_INFO("Forge: created the C# project {} in {}, with the solution {}.slnx", l_Name, ToUtf8(project.GetScriptsDirectory()), l_Name);
    Open(project);

    return true;
}

void AssemblyHost::RequestBuild()
{
    if (m_Project != nullptr && m_Project->HasAssembly() && IsSupported())
    {
        m_BuildRequested = true;
    }
}

// Scripts that cannot be watched still build when asked, and an output that cannot be watched still loads Forge's own builds
void AssemblyHost::Watch()
{
    if (Trinity::Expected<Trinity::DirectoryWatcher, std::string> l_Sources = Trinity::DirectoryWatcher::Watch(m_Project->GetScriptsDirectory()))
    {
        m_SourceWatcher = std::move(*l_Sources);
    }
    else
    {
        TR_WARN("Forge: {}, so {} is not built again when its sources change", l_Sources.GetError(), m_Project->GetAssemblyName());
    }

    const std::filesystem::path l_Output = GetBuiltPath().parent_path();
    std::error_code l_Error;
    std::filesystem::create_directories(l_Output, l_Error);
    if (Trinity::Expected<Trinity::DirectoryWatcher, std::string> l_Built = Trinity::DirectoryWatcher::Watch(l_Output))
    {
        m_OutputWatcher = std::move(*l_Built);
    }
    else
    {
        TR_WARN("Forge: {}, so a build of {} from an IDE does not reload", l_Built.GetError(), m_Project->GetAssemblyName());
    }
}

// Only when it changes, so an IDE that watches the project does not reload it each time Forge opens
void AssemblyHost::WriteLocalProps() const
{
    const std::filesystem::path l_Path = m_Project->GetScriptsDirectory() / "Trinity.Local.props";
    const std::string l_Text = std::format("<Project>\n  <!-- Written by Forge as it opens the project: the Trinity-ScriptCore the scripts build against when an IDE builds them -->\n  <PropertyGroup>\n    <TrinityScriptingDirectory Condition=\"'$(TrinityScriptingDirectory)' == ''\">{}</TrinityScriptingDirectory>\n  </PropertyGroup>\n</Project>\n", EscapeXml(ToUtf8(AssemblyBuild::GetScriptingDirectory())));

    std::ifstream l_Existing(l_Path, std::ios::binary);
    if (l_Existing && std::string(std::istreambuf_iterator<char>(l_Existing), std::istreambuf_iterator<char>()) == l_Text)
    {
        return;
    }

    l_Existing.close();
    std::error_code l_Error;
    std::filesystem::create_directories(l_Path.parent_path(), l_Error);
    std::ofstream l_File(l_Path, std::ios::binary | std::ios::trunc);
    l_File << l_Text;
    if (!l_File)
    {
        TR_WARN("Forge: {} could not be written, so an IDE does not know which Trinity-ScriptCore to build against", ToUtf8(l_Path));
    }
}

void AssemblyHost::StartBuild()
{
    AssemblyBuild::Request l_Request;
    l_Request.Name = m_Project->GetAssemblyName();
    l_Request.ProjectDirectory = m_Project->GetDirectory();
    l_Request.ScriptsDirectory = m_Project->GetScriptsDirectory();
    m_Build.Start(std::move(l_Request));
}

// The scripts running now go only once the build is copied, and are loaded again when the build cannot load in their place. A build tried already, such as one that changed nothing, or Forge's own build seen again in the output, is not tried again
void AssemblyHost::Load(const std::filesystem::path& built)
{
    std::error_code l_Error;
    const std::filesystem::file_time_type l_Time = std::filesystem::last_write_time(built, l_Error);
    if (!l_Error && l_Time == m_TriedBuildTime)
    {
        return;
    }

    m_TriedBuildTime = l_Error ? std::nullopt : std::optional(l_Time);
    const std::optional<std::filesystem::path> l_Copy = CopyBuild(built);
    if (!l_Copy)
    {
        return;
    }

    const std::filesystem::path l_Previous = std::exchange(m_LoadedCopy, {});
    const bool l_Reload = IsLoaded();
    const bool l_Collected = !l_Reload || Unload();
    if (LoadCopy(*l_Copy, built))
    {
        if (l_Collected)
        {
            RemoveCopy(l_Previous);
        }

        return;
    }

    RemoveCopy(*l_Copy);
    if (!l_Previous.empty() && LoadCopy(l_Previous, built))
    {
        TR_WARN("Forge: the new build of {} could not be loaded, so the one before it runs again", m_Project->GetAssemblyName());

        return;
    }

    RemoveCopy(l_Previous);
}

// Under the next number no file there has, since a context that was not collected, or a debugger, may still hold an older copy
std::optional<std::filesystem::path> AssemblyHost::CopyBuild(const std::filesystem::path& built)
{
    const std::filesystem::path l_Directory = GetCopyDirectory();
    std::error_code l_Error;
    std::filesystem::create_directories(l_Directory, l_Error);

    std::filesystem::path l_Copy;
    std::filesystem::path l_Pdb;
    do
    {
        const std::string l_Stem = std::format("{}-{}", m_Project->GetAssemblyName(), m_NextCopy++);
        l_Copy = l_Directory / (l_Stem + ".dll");
        l_Pdb = l_Directory / (l_Stem + ".pdb");
    } while (std::filesystem::exists(l_Copy, l_Error) || std::filesystem::exists(l_Pdb, l_Error));

    if (!std::filesystem::copy_file(built, l_Copy, l_Error))
    {
        TR_ERROR("Forge: {} could not be copied to {}: {}", ToUtf8(built), ToUtf8(l_Directory), l_Error.message());

        return std::nullopt;
    }

    const std::filesystem::path l_BuiltPdb = std::filesystem::path(built).replace_extension(".pdb");
    if (std::filesystem::exists(l_BuiltPdb, l_Error) && !std::filesystem::copy_file(l_BuiltPdb, l_Pdb, l_Error))
    {
        TR_WARN("Forge: the PDB of {} could not be copied beside the scripts, so their stack traces have no lines", m_Project->GetAssemblyName());
    }

    return l_Copy;
}

bool AssemblyHost::LoadCopy(const std::filesystem::path& copy, const std::filesystem::path& built)
{
    if (!Trinity::ManagedScripts::LoadAssembly(copy, built))
    {
        return false;
    }

    m_LoadedCopy = copy;
    if (m_AfterLoad)
    {
        m_AfterLoad();
    }

    return true;
}

bool AssemblyHost::Unload()
{
    if (m_BeforeUnload)
    {
        m_BeforeUnload();
    }

    return Trinity::ManagedScripts::UnloadAssembly();
}

// Every copy of these scripts there, which a debugger may still hold, so those that cannot be removed stay until a later open
void AssemblyHost::RemoveStaleCopies() const
{
    std::error_code l_Error;
    const std::string l_Prefix = m_Project->GetAssemblyName() + "-";
    for (std::filesystem::directory_iterator it_Entry(GetCopyDirectory(), l_Error); !l_Error && it_Entry != std::filesystem::directory_iterator(); it_Entry.increment(l_Error))
    {
        std::error_code l_RemoveError;
        if (it_Entry->is_regular_file(l_RemoveError) && ToUtf8(it_Entry->path().filename()).starts_with(l_Prefix))
        {
            std::filesystem::remove(it_Entry->path(), l_RemoveError);
        }
    }
}

std::filesystem::path AssemblyHost::GetCopyDirectory() const
{
    return m_Project->GetDirectory() / "Cache" / "Scripts";
}

std::filesystem::path AssemblyHost::GetBuiltPath() const
{
    return AssemblyBuild::GetAssemblyPath(m_Project->GetDirectory(), m_Project->GetAssemblyName());
}
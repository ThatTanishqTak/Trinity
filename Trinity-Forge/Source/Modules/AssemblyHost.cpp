#include "Modules/AssemblyHost.hpp"

#include <array>
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
}

AssemblyHost::~AssemblyHost()
{
    Close();
}

// A project with scripts builds them once as it opens, which does nothing but load them when nothing changed since the last build
void AssemblyHost::Open(Trinity::Project& project)
{
    Close();
    m_Project = &project;
    if (!project.HasAssembly())
    {
        return;
    }

    if (!IsSupported())
    {
        TR_WARN("Forge: {} has the C# scripts {}, which cannot build or load, since C# is off. The log says why when Forge starts", project.GetName(), project.GetAssemblyName());

        return;
    }

    WriteLocalProps();
    m_BuildRequested = true;
}

void AssemblyHost::Close()
{
    m_Build.Cancel();
    if (std::exchange(m_Loaded, false))
    {
        Trinity::ManagedScripts::UnloadAssembly();
    }

    m_Project = nullptr;
    m_BuildRequested = false;
}

void AssemblyHost::Update()
{
    if (m_Project == nullptr || !m_Project->HasAssembly() || !IsSupported())
    {
        return;
    }

    if (const std::optional<AssemblyBuild::Result> l_Result = m_Build.Update(); l_Result && l_Result->Succeeded)
    {
        Load(l_Result->Assembly);
    }

    if (m_BuildRequested && !m_Build.IsRunning())
    {
        m_BuildRequested = false;
        StartBuild();
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

// The first good build loads. A later one is built and checked, and the one loaded keeps running, since C# scripts do not reload yet
void AssemblyHost::Load(const std::filesystem::path& built)
{
    if (m_Loaded)
    {
        TR_INFO("Forge: {} built, and the build that loaded as the project opened keeps running. Open the project again to load this one", m_Project->GetAssemblyName());

        return;
    }

    if (!Trinity::ManagedScripts::LoadAssembly(built))
    {
        return;
    }

    m_Loaded = true;
    if (m_AfterLoad)
    {
        m_AfterLoad();
    }
}
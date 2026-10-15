#include "Modules/ModuleHost.hpp"

#include "Modules/PortableExecutable.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace
{
    // A save often writes a file more than once, and an editor may save several at once, so a build waits this long after the last change
    constexpr std::chrono::milliseconds c_SettleTime{ 500 };

    constexpr std::string_view c_CMakeListsTemplate = R"(# @NAME@, the C++ game module of @PROJECT@. Forge configures and builds it into Build/<configuration> with the compiler Trinity was built with, and loads it after each good build. To build it elsewhere, such as from an IDE, set Trinity_DIR to the folder of the Forge that opens the project, which holds TrinityConfig.cmake, and CMAKE_TOOLCHAIN_FILE to TrinityToolchain.cmake in it
cmake_minimum_required(VERSION 3.30)
project(@NAME@ LANGUAGES CXX)

find_package(Trinity CONFIG REQUIRED)

# Every source below this folder, found again whenever one is added or removed
file(GLOB_RECURSE MODULE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp")
trinity_add_game_module(@NAME@ ${MODULE_SOURCES})
)";

    constexpr std::string_view c_SourceTemplate = R"(#include <Trinity.hpp>

// The module's exports, which register every TR_SCRIPT and TR_COMPONENT of the module as Forge loads it, after each good build, and unregister them before Forge unloads it, as a new build takes its place and as the project closes
TR_GAME_MODULE();

// A script class: its fields are public members, listed in a TR_SCRIPT block at global scope, and their defaults are the values a new instance has. Add Script in Properties lists it once the module is built
//
// namespace @NAME@
// {
//     class Mover : public Trinity::Script
//     {
//     public:
//         float Speed = 1.0f;
//
//         void OnUpdate(Trinity::Timestep timestep) override
//         {
//             GetEntity().Patch<Trinity::TransformComponent>([&](Trinity::TransformComponent& transform) { transform.Position.y += Speed * timestep; });
//         }
//     };
// }
//
// TR_SCRIPT(@NAME@::Mover)
// {
//     TR_FIELD(Speed);
// }
//
// A component, which names itself, saves and loads through the fields its TR_COMPONENT block lists, and is kept in the scene while the module is not loaded. Add Component in Properties lists it
//
// namespace @NAME@
// {
//     struct Health
//     {
//         static constexpr std::string_view c_TypeName = "@NAME@.Health";
//
//         float Current = 100.0f;
//         float Max = 100.0f;
//     };
// }
//
// TR_COMPONENT(@NAME@::Health)
// {
//     TR_FIELD(Current);
//     TR_FIELD(Max);
// }
)";

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

    // A file the build reads, which CMake's own files and the C and C++ sources and headers are
    bool IsBuildInput(const std::filesystem::path& path)
    {
        constexpr std::array<std::string_view, 11> c_Extensions{ ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl", ".ipp", ".cmake" };
        if (path.filename() == "CMakeLists.txt")
        {
            return true;
        }

        std::string l_Extension = path.extension().string();
        std::ranges::transform(l_Extension, l_Extension.begin(), [](char character) { return static_cast<char>(std::tolower(static_cast<unsigned char>(character))); });

        return std::ranges::find(c_Extensions, l_Extension) != c_Extensions.end();
    }

    std::string ToUtf8(const std::filesystem::path& path)
    {
        const std::u8string l_Text = path.u8string();

        return std::string(reinterpret_cast<const char*>(l_Text.data()), l_Text.size());
    }

    // The copy and its PDB. Windows keeps a PDB a debugger holds, which goes with a later project open
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

    std::vector<std::string> GetVariableNames()
    {
        std::vector<std::string> l_Names;
        for (const Trinity::ConsoleVariableBase* it_Variable = Trinity::ConsoleVariableBase::GetFirst(); it_Variable != nullptr; it_Variable = it_Variable->GetNext())
        {
            l_Names.emplace_back(it_Variable->GetName());
        }

        return l_Names;
    }
}

ModuleHost::~ModuleHost()
{
    Close();
}

// A project with a module builds it once as it opens, which does nothing but load it when nothing changed since the last build. Copies an earlier Forge left are removed first
void ModuleHost::Open(Trinity::Project& project)
{
    Close();
    m_Project = &project;
    if (!project.HasModule())
    {
        return;
    }

    RemoveStaleCopies();

    if (!IsSupported())
    {
        TR_WARN("Forge: {} has the C++ module {}, which this Forge cannot build or load, since its engine is linked statically. Open it in a Debug or Release Forge", project.GetName(), project.GetModuleName());

        return;
    }

    Watch();
    m_BuildRequested = true;
}

void ModuleHost::Close()
{
    m_Build.Cancel();
    Unload();
    RemoveCopy(std::exchange(m_LoadedCopy, {}));
    m_KeptValues.clear();
    m_Watcher.Stop();
    m_Project = nullptr;
    m_ChangedAt.reset();
    m_BuildRequested = false;
    m_LastBuild = {};
    m_LastErrorLines.clear();
}

void ModuleHost::Update()
{
    if (m_Project == nullptr || !m_Project->HasModule() || !IsSupported())
    {
        return;
    }

    const Trinity::DirectoryChanges l_Changes = m_Watcher.Poll();
    if (l_Changes.Overflowed || std::ranges::any_of(l_Changes.Paths, IsBuildInput))
    {
        m_ChangedAt = Clock::now();
    }

    if (const std::optional<ModuleBuild::Result> l_Result = m_Build.Update())
    {
        if (!l_Result->Cancelled)
        {
            m_LastBuild = { l_Result->Succeeded ? BuildState::UpToDate : BuildState::Failed, m_RunningFirstLine, Trinity::LogHistory::GetNextSequence(), l_Result->Errors, l_Result->Warnings };
            m_LastErrorLines = l_Result->ErrorLines;
        }

        if (l_Result->Succeeded)
        {
            Load(l_Result->Library);
        }
    }

    if (m_ChangedAt && Clock::now() - *m_ChangedAt >= c_SettleTime)
    {
        m_ChangedAt.reset();
        m_BuildRequested = true;
    }

    // A change while a build runs builds again once it ends
    if (m_BuildRequested && !m_Build.IsRunning())
    {
        m_BuildRequested = false;
        StartBuild();
    }
}

bool ModuleHost::Create(Trinity::Project& project)
{
    if (!IsSupported())
    {
        TR_ERROR("Forge: this Forge's engine is linked statically, so it cannot build or load a C++ module. Create the module in a Debug or Release Forge");

        return false;
    }

    if (project.HasModule())
    {
        return false;
    }

    const std::string l_Name = Trinity::Project::MakeModuleName(project.GetName());
    const std::array<std::pair<std::string, std::string_view>, 2> l_Files{ { { "CMakeLists.txt", c_CMakeListsTemplate },{ l_Name + ".cpp", c_SourceTemplate } } };
    for (const auto& [it_File, it_Template] : l_Files)
    {
        const std::string l_Path = std::format("{}/Source/{}", Trinity::Project::c_ProjectMount, it_File);
        if (Trinity::FileSystem::Exists(l_Path))
        {
            TR_INFO("Forge: Source/{} is already there, so it is kept", it_File);

            continue;
        }

        if (const Trinity::Expected<void, Trinity::FileError> l_Written = Trinity::FileSystem::WriteText(l_Path, FillTemplate(it_Template, l_Name, project.GetName())); !l_Written)
        {
            TR_ERROR("Forge: Source/{} could not be written: {}", it_File, Trinity::ToString(l_Written.GetError()));

            return false;
        }
    }

    project.SetModuleName(l_Name);
    TR_INFO("Forge: created the C++ module {} in {}", l_Name, ToUtf8(project.GetSourceDirectory()));
    Open(project);

    return true;
}

BuildStatus ModuleHost::GetStatus() const
{
    if (m_Build.IsRunning())
    {
        return { BuildState::Building, m_RunningFirstLine, Trinity::LogHistory::GetNextSequence(), 0, 0 };
    }

    if (m_Project != nullptr && (m_ChangedAt || m_BuildRequested))
    {
        BuildStatus l_Status = m_LastBuild;
        l_Status.State = BuildState::Pending;

        return l_Status;
    }

    return m_LastBuild;
}

void ModuleHost::RequestBuild()
{
    if (m_Project != nullptr && m_Project->HasModule() && IsSupported())
    {
        m_BuildRequested = true;
    }
}

// A module whose sources cannot be watched still builds when asked
void ModuleHost::Watch()
{
    Trinity::Expected<Trinity::DirectoryWatcher, std::string> l_Watcher = Trinity::DirectoryWatcher::Watch(m_Project->GetSourceDirectory());
    if (!l_Watcher)
    {
        TR_WARN("Forge: {}, so {} is not built again when its sources change", l_Watcher.GetError(), m_Project->GetModuleName());

        return;
    }

    m_Watcher = std::move(*l_Watcher);
}

void ModuleHost::StartBuild()
{
    ModuleBuild::Request l_Request;
    l_Request.Name = m_Project->GetModuleName();
    l_Request.ProjectDirectory = m_Project->GetDirectory();
    l_Request.SourceDirectory = m_Project->GetSourceDirectory();
    l_Request.BuildDirectory = ModuleBuild::GetBuildDirectory(m_Project->GetBuildDirectory());
    m_RunningFirstLine = Trinity::LogHistory::GetNextSequence();
    m_Build.Start(std::move(l_Request));
}

// The module running now goes only once the build is copied, and is loaded again when the build cannot load in its place
void ModuleHost::Load(const std::filesystem::path& built)
{
    const std::optional<std::filesystem::path> l_Copy = CopyBuild(built);
    if (!l_Copy)
    {
        return;
    }

    const std::filesystem::path l_Previous = std::exchange(m_LoadedCopy, {});
    Unload();
    if (LoadCopy(*l_Copy))
    {
        RemoveCopy(l_Previous);

        return;
    }

    RemoveCopy(*l_Copy);
    if (!l_Previous.empty() && LoadCopy(l_Previous))
    {
        TR_WARN("Forge: the new build of {} could not be loaded, so the one before it runs again", m_Project->GetModuleName());

        return;
    }

    RemoveCopy(l_Previous);
}

// Under the next number no file there has, since a debugger may still hold an older copy's PDB. The copy's record of its PDB names the PDB copied beside it, which the debugger finds there
std::optional<std::filesystem::path> ModuleHost::CopyBuild(const std::filesystem::path& built)
{
    const std::filesystem::path l_Directory = GetCopyDirectory();
    std::error_code l_Error;
    std::filesystem::create_directories(l_Directory, l_Error);

    std::filesystem::path l_Copy;
    std::filesystem::path l_Pdb;
    do
    {
        const std::string l_Stem = std::format("{}-{}", m_Project->GetModuleName(), m_NextCopy++);
        l_Copy = l_Directory / (l_Stem + built.extension().string());
        l_Pdb = l_Directory / (l_Stem + ".pdb");
    } while (std::filesystem::exists(l_Copy, l_Error) || std::filesystem::exists(l_Pdb, l_Error));

    std::optional<std::vector<char>> l_Image = PortableExecutable::ReadImage(built);
    if (!l_Image)
    {
        TR_ERROR("Forge: {} could not be read to copy it to {}", ToUtf8(built), ToUtf8(l_Directory));

        return std::nullopt;
    }

    const std::filesystem::path l_BuiltPdb = std::filesystem::path(built).replace_extension(".pdb");
    if (built.extension() == ".dll" && std::filesystem::exists(l_BuiltPdb, l_Error))
    {
        if (!PortableExecutable::SetPdbName(*l_Image, ToUtf8(l_Pdb.filename())) || !std::filesystem::copy_file(l_BuiltPdb, l_Pdb, l_Error))
        {
            TR_WARN("Forge: the PDB of {} could not be copied beside the module, so a debugger finds no symbols for this build", m_Project->GetModuleName());
        }
    }

    if (!PortableExecutable::WriteImage(l_Copy, *l_Image))
    {
        TR_ERROR("Forge: {} could not be written, so the build is not loaded", ToUtf8(l_Copy));
        RemoveCopy(l_Copy);

        return std::nullopt;
    }

    return l_Copy;
}

// The variables the module made are those there are now and were not before it loaded. The module's own load function runs before whatever runs its code is told it is back
bool ModuleHost::LoadCopy(const std::filesystem::path& copy)
{
    const std::vector<std::string> l_Before = GetVariableNames();
    Trinity::Expected<Trinity::SharedLibrary, std::string> l_Library = Trinity::SharedLibrary::Load(copy);
    if (!l_Library)
    {
        TR_ERROR("Forge: {}", l_Library.GetError());

        return false;
    }

    // A module that says which version of the module API it was built against must have this one, or what it registers could not be read the way it was written
    if (const Trinity::GameModule::ApiVersionFunction l_Version = l_Library->GetFunction<Trinity::GameModule::ApiVersionFunction>(Trinity::GameModule::c_ApiVersionFunction))
    {
        if (const std::uint32_t l_Built = l_Version(); l_Built != Trinity::GameModule::c_ApiVersion)
        {
            TR_ERROR("Forge: {} was built for version {} of the module API, and this Forge has version {}, so it is not loaded. Building it again builds it for this one", m_Project->GetModuleName(), l_Built, Trinity::GameModule::c_ApiVersion);

            return false;
        }
    }

    m_Library = std::move(*l_Library);
    m_LoadedCopy = copy;
    const std::unordered_set<std::string> l_Engine(l_Before.begin(), l_Before.end());
    m_ModuleVariables = GetVariableNames();
    std::erase_if(m_ModuleVariables, [&l_Engine](const std::string& name) { return l_Engine.contains(name); });
    RestoreVariables();

    m_Unload = m_Library.GetFunction<Trinity::GameModule::UnloadFunction>(Trinity::GameModule::c_UnloadFunction);
    TR_INFO("Forge: loaded the C++ module {} from Cache/Modules/{}", m_Project->GetModuleName(), ToUtf8(copy.filename()));
    if (const Trinity::GameModule::LoadFunction l_Load = m_Library.GetFunction<Trinity::GameModule::LoadFunction>(Trinity::GameModule::c_LoadFunction))
    {
        l_Load();
    }
    else
    {
        TR_WARN("Forge: {} exports no {}, so nothing in it runs", m_Project->GetModuleName(), Trinity::GameModule::c_LoadFunction);
    }

    if (m_AfterLoad)
    {
        m_AfterLoad();
    }

    return true;
}

// What the module's own code allocated is counted under Game, so a module that frees all it took leaves nothing there once it is gone
void ModuleHost::Unload()
{
    if (!m_Library.IsLoaded())
    {
        return;
    }

    if (m_BeforeUnload)
    {
        m_BeforeUnload();
    }

    KeepVariables();
    if (m_Unload != nullptr)
    {
        m_Unload();
    }

    m_Unload = nullptr;
    m_Library.Unload();
    m_ModuleVariables.clear();

    const Trinity::MemoryTagStats l_Game = Trinity::Memory::GetStats(Trinity::MemoryTag::Game);
    if (l_Game.LiveAllocations != 0)
    {
        TR_WARN("Forge: the C++ module left {} in {} allocation(s) under Game as it unloaded", Trinity::Memory::FormatBytes(l_Game.CurrentBytes), l_Game.LiveAllocations);
    }
}

// Read-only variables take their values from the command line as they are made, so only the others are kept
void ModuleHost::KeepVariables()
{
    m_KeptValues.clear();
    for (const std::string& it_Name : m_ModuleVariables)
    {
        const Trinity::ConsoleVariableBase* l_Variable = Trinity::ConsoleVariables::Find(it_Name);
        if (l_Variable != nullptr && !Trinity::HasFlag(l_Variable->GetFlags(), Trinity::ConsoleVariableFlags::ReadOnly) && l_Variable->ToString() != l_Variable->DefaultToString())
        {
            m_KeptValues.emplace_back(it_Name, l_Variable->ToString());
        }
    }
}

// A kept value goes to the new module's variable of the same name, unless it no longer reads as that variable's type
void ModuleHost::RestoreVariables()
{
    std::size_t l_Restored = 0;
    for (const auto& [it_Name, it_Value] : std::exchange(m_KeptValues, {}))
    {
        Trinity::ConsoleVariableBase* l_Variable = std::ranges::find(m_ModuleVariables, it_Name) != m_ModuleVariables.end() ? Trinity::ConsoleVariables::Find(it_Name) : nullptr;
        if (l_Variable == nullptr || Trinity::HasFlag(l_Variable->GetFlags(), Trinity::ConsoleVariableFlags::ReadOnly))
        {
            continue;
        }

        if (!l_Variable->SetFromString(it_Value))
        {
            TR_WARN("Forge: {} was {}, which it can no longer hold, so it starts at {}", it_Name, it_Value, l_Variable->DefaultToString());

            continue;
        }

        ++l_Restored;
    }

    if (l_Restored != 0)
    {
        TR_INFO("Forge: {} console variable(s) of {} kept their values through the reload", l_Restored, m_Project->GetModuleName());
    }
}

// Every copy of this module there, which a debugger may still hold, so those that cannot be removed stay until a later open
void ModuleHost::RemoveStaleCopies() const
{
    std::error_code l_Error;
    const std::string l_Prefix = m_Project->GetModuleName() + "-";
    for (std::filesystem::directory_iterator it_Entry(GetCopyDirectory(), l_Error); !l_Error && it_Entry != std::filesystem::directory_iterator(); it_Entry.increment(l_Error))
    {
        std::error_code l_RemoveError;
        if (it_Entry->is_regular_file(l_RemoveError) && ToUtf8(it_Entry->path().filename()).starts_with(l_Prefix))
        {
            std::filesystem::remove(it_Entry->path(), l_RemoveError);
        }
    }
}

std::filesystem::path ModuleHost::GetCopyDirectory() const
{
    return m_Project->GetDirectory() / "Cache" / "Modules";
}
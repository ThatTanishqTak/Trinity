#include "Modules/ModuleHost.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <string>
#include <string_view>
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

    constexpr std::string_view c_SourceTemplate = R"(#include <Trinity/Core/Log.hpp>
#include <Trinity/Scripting/GameModule.hpp>

// Forge calls this after it loads the module, which it does after each good build
TR_GAME_MODULE_EXPORT void TrinityModuleLoad()
{
    TR_INFO("@NAME@: loaded");
}

// And this before it unloads the module, which it does before each build and as the project closes
TR_GAME_MODULE_EXPORT void TrinityModuleUnload()
{
    TR_INFO("@NAME@: unloaded");
}
)";

    std::string FillTemplate(std::string_view text, std::string_view name, std::string_view project)
    {
        std::string l_Text(text);
        for (const auto& [it_Key, it_Value] : { std::pair<std::string_view, std::string_view>{ "@NAME@", name }, { "@PROJECT@", project } })
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
}

ModuleHost::~ModuleHost()
{
    Close();
}

// A project with a module builds it once as it opens, which does nothing but load it when nothing changed since the last build
void ModuleHost::Open(Trinity::Project& project)
{
    Close();
    m_Project = &project;
    if (!project.HasModule())
    {
        return;
    }

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
    m_Watcher.Stop();
    m_Project = nullptr;
    m_ChangedAt.reset();
    m_BuildRequested = false;
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

    if (const std::optional<ModuleBuild::Result> l_Result = m_Build.Update(); l_Result && l_Result->Succeeded)
    {
        Load(l_Result->Library);
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
    const std::array<std::pair<std::string, std::string_view>, 2> l_Files{ { { "CMakeLists.txt", c_CMakeListsTemplate }, { l_Name + ".cpp", c_SourceTemplate } } };
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
    Unload();

    ModuleBuild::Request l_Request;
    l_Request.Name = m_Project->GetModuleName();
    l_Request.ProjectDirectory = m_Project->GetDirectory();
    l_Request.SourceDirectory = m_Project->GetSourceDirectory();
    l_Request.BuildDirectory = ModuleBuild::GetBuildDirectory(m_Project->GetBuildDirectory());
    m_Build.Start(std::move(l_Request));
}

void ModuleHost::Load(const std::filesystem::path& library)
{
    Unload();

    Trinity::Expected<Trinity::SharedLibrary, std::string> l_Library = Trinity::SharedLibrary::Load(library);
    if (!l_Library)
    {
        TR_ERROR("Forge: {}", l_Library.GetError());

        return;
    }

    m_Library = std::move(*l_Library);
    m_Unload = m_Library.GetFunction<Trinity::GameModule::UnloadFunction>(Trinity::GameModule::c_UnloadFunction);
    TR_INFO("Forge: loaded the C++ module {}", m_Project->GetModuleName());
    if (const Trinity::GameModule::LoadFunction l_Load = m_Library.GetFunction<Trinity::GameModule::LoadFunction>(Trinity::GameModule::c_LoadFunction))
    {
        l_Load();
    }
    else
    {
        TR_WARN("Forge: {} exports no {}, so nothing in it runs", m_Project->GetModuleName(), Trinity::GameModule::c_LoadFunction);
    }
}

// What the module's own code allocated is counted under Game, so a module that frees all it took leaves nothing there once it is gone
void ModuleHost::Unload()
{
    if (!m_Library.IsLoaded())
    {
        return;
    }

    if (m_Unload != nullptr)
    {
        m_Unload();
    }

    m_Unload = nullptr;
    m_Library.Unload();

    const Trinity::MemoryTagStats l_Game = Trinity::Memory::GetStats(Trinity::MemoryTag::Game);
    if (l_Game.LiveAllocations != 0)
    {
        TR_WARN("Forge: the C++ module left {} in {} allocation(s) under Game as it unloaded", Trinity::Memory::FormatBytes(l_Game.CurrentBytes), l_Game.LiveAllocations);
    }
}
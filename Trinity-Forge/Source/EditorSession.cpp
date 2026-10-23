#include "EditorSession.hpp"

#include "EditorCommands.hpp"
#include "Modules/DebuggerSettings.hpp"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <format>
#include <fstream>
#include <optional>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
    constexpr std::string_view c_ApplicationTitle = "Trinity Forge";
    constexpr const char* c_UnsavedPopup = "Unsaved Changes###ForgeUnsaved";
    constexpr const char* c_PathPopup = "Choose a Path###ForgePath";
    constexpr const char* c_ImportPopup = "Importing Textures###ForgeImport";
    constexpr const char* c_DebuggerPopup = "Waiting for a Debugger###ForgeDebugger";
    constexpr const char* c_RecoveryPopup = "Recover Scene###ForgeRecoverScene";
    // The open scene's backup, under the project's Cache, which is never committed. Its first lines name the scene and when it was written, as YAML comments the scene's reader passes over
    constexpr std::string_view c_BackupFile = "Editor/Backup/Scene.trscene";
    // The projects opened lately, per user, and the scenes, per project, ten of each
    constexpr std::string_view c_RecentProjectsPath = "/saves/RecentProjects.txt";
    constexpr std::string_view c_RecentScenesFile = "Editor/RecentScenes.txt";
    constexpr std::size_t c_RecentLimit = 10;
    constexpr std::string_view c_BackupScenePrefix = "# Scene: ";
    constexpr std::string_view c_BackupWrittenPrefix = "# Written: ";
    constexpr std::string_view c_SceneExtension = ".trscene";

    constexpr std::array<Trinity::FileDialogFilter, 1> c_ProjectFilters{ { { "Trinity project", "trproj" } } };
    constexpr std::array<Trinity::FileDialogFilter, 1> c_SceneFilters{ { { "Trinity scene", "trscene" } } };

    // A batch that finishes sooner, or finds every texture in the cache, never shows its popup
    constexpr double c_ImportPopupDelay = 0.3;

    // An estimate for the texture being encoded stops short of it, since the encoder reports nothing until it is done
    constexpr double c_MaximumEstimate = 0.95;

    // How long Play waits after one debugger is seen attached, for the other half of one that debugs native and managed code at once to attach too
    constexpr std::chrono::milliseconds c_DebuggerSettle{ 1000 };

    // The missing references a scene's warning lists, which keeps it within a line of the Console's history
    constexpr std::size_t c_MissingReferenceLimit = 50;

    std::string FormatDuration(double seconds)
    {
        const long long l_Seconds = std::max(0ll, std::llround(seconds));

        return std::format("{}:{:02}", l_Seconds / 60, l_Seconds % 60);
    }

    std::string GetBackupPath()
    {
        return std::format("{}/{}", Trinity::Project::c_CacheMount, c_BackupFile);
    }

    std::string GetRecentScenesPath()
    {
        return std::format("{}/{}", Trinity::Project::c_CacheMount, c_RecentScenesFile);
    }

    // Each line of the text, without its end
    std::vector<std::string_view> SplitLines(std::string_view text)
    {
        std::vector<std::string_view> l_Lines;
        while (!text.empty())
        {
            const std::size_t l_End = text.find('\n');
            std::string_view l_Line = text.substr(0, l_End);
            if (l_Line.ends_with('\r'))
            {
                l_Line.remove_suffix(1);
            }

            l_Lines.push_back(l_Line);
            text = l_End == std::string_view::npos ? std::string_view() : text.substr(l_End + 1);
        }

        return l_Lines;
    }

    // As many finite numbers as there are values, apart by spaces, or false with the values as they were
    bool ReadNumbers(std::string_view text, std::span<float> values)
    {
        std::array<float, 4> l_Read{};
        const char* l_Position = text.data();
        const char* const l_End = text.data() + text.size();
        for (std::size_t it_Index = 0; it_Index < values.size(); ++it_Index)
        {
            while (l_Position != l_End && *l_Position == ' ')
            {
                ++l_Position;
            }

            const std::from_chars_result l_Parsed = std::from_chars(l_Position, l_End, l_Read[it_Index]);
            if (l_Parsed.ec != std::errc() || !std::isfinite(l_Read[it_Index]))
            {
                return false;
            }

            l_Position = l_Parsed.ptr;
        }

        std::copy_n(l_Read.begin(), values.size(), values.begin());

        return true;
    }

    // The texels a texture import has done, the one being encoded counted by the rate the encoder has kept so far. Nothing before that can be told
    std::optional<double> GetImportedTexels(const TextureImportBatch::Progress& progress)
    {
        if (!progress.Planned || progress.TotalTexels == 0 || (!progress.TexelsPerSecond && progress.Encoding != 0))
        {
            return std::nullopt;
        }

        const double l_Current = progress.TexelsPerSecond && progress.Encoding != 0 ? std::min(progress.CurrentSeconds * *progress.TexelsPerSecond, static_cast<double>(progress.CurrentTexels) * c_MaximumEstimate) : 0.0;

        return static_cast<double>(progress.DoneTexels) + l_Current;
    }

    std::filesystem::path FromUtf8(std::string_view text)
    {
        return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size()));
    }

    std::string ToUtf8(const std::filesystem::path& path)
    {
        const std::u8string l_Text = path.u8string();

        return std::string(reinterpret_cast<const char*>(l_Text.data()), l_Text.size());
    }

    // A single file or folder name, never hidden, and never one the registry would take for a .meta
    bool IsValidName(std::string_view name)
    {
        return !name.empty() && name.find_first_of("/\\:*?\"<>|") == std::string_view::npos && !name.starts_with('.') && !name.ends_with(' ') && !name.ends_with(Trinity::AssetRegistry::c_MetaExtension);
    }

    bool IsSameOrInside(std::string_view path, std::string_view folder)
    {
        return path == folder || (path.starts_with(folder) && path.size() > folder.size() && path[folder.size()] == '/');
    }

    // New scripts, with @NAMESPACE@ the assembly's or the module's name and @CLASS@ the class's
    constexpr std::string_view c_CSharpScriptTemplate = R"(using Trinity;

namespace @NAMESPACE@
{
    // Its fields are its public fields and those marked [Field], with the values a new instance has, which Properties shows once the scripts are built
    public class @CLASS@ : Script
    {
        public float Speed = 1.0f;

        public override void OnCreate()
        {

        }

        public override void OnUpdate(float timestep)
        {

        }
    }
}
)";

    constexpr std::string_view c_CppScriptTemplate = R"(#include <Trinity.hpp>

namespace @NAMESPACE@
{
    // Its fields are the members its TR_SCRIPT block lists, with the values a new instance has, which Properties shows once the module is built
    class @CLASS@ : public Trinity::Script
    {
    public:
        float Speed = 1.0f;

        void OnCreate() override
        {

        }

        void OnUpdate([[maybe_unused]] Trinity::Timestep timestep) override
        {

        }
    };
}

TR_SCRIPT(@NAMESPACE@::@CLASS@)
{
    TR_FIELD(Speed);
}
)";

    std::string FillScriptTemplate(std::string_view text, std::string_view nameSpace, std::string_view className)
    {
        std::string l_Text(text);
        for (const auto& [it_Key, it_Value] : { std::pair<std::string_view, std::string_view>{ "@NAMESPACE@", nameSpace }, std::pair<std::string_view, std::string_view>{ "@CLASS@", className } })
        {
            for (std::size_t l_At = l_Text.find(it_Key); l_At != std::string::npos; l_At = l_Text.find(it_Key, l_At + it_Value.size()))
            {
                l_Text.replace(l_At, it_Key.size(), it_Value);
            }
        }

        return l_Text;
    }

    // A letter, a digit or _, as a C++ or C# name is made of
    bool IsNameCharacter(char character)
    {
        return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_';
    }

    // The line, counted from 1, of the first "class Name" or "struct Name" in the file, passing over one after // on its line, and one that only declares it ahead of its definition, as "class Name;" does
    std::optional<std::uint32_t> FindClassLine(const std::filesystem::path& file, std::string_view name)
    {
        constexpr std::array<std::string_view, 2> c_Keywords{ "class", "struct" };

        std::ifstream l_Stream(file);
        std::string l_Line;
        for (std::uint32_t l_Number = 1; std::getline(l_Stream, l_Line); ++l_Number)
        {
            const std::size_t l_Comment = l_Line.find("//");
            for (const std::string_view it_Keyword : c_Keywords)
            {
                for (std::size_t l_At = l_Line.find(it_Keyword); l_At != std::string::npos && l_At < l_Comment; l_At = l_Line.find(it_Keyword, l_At + 1))
                {
                    const std::size_t l_KeywordEnd = l_At + it_Keyword.size();
                    if ((l_At > 0 && IsNameCharacter(l_Line[l_At - 1])) || l_KeywordEnd >= l_Line.size() || IsNameCharacter(l_Line[l_KeywordEnd]))
                    {
                        continue;
                    }

                    const std::size_t l_NameAt = l_Line.find_first_not_of(" \t", l_KeywordEnd);
                    if (l_NameAt == std::string::npos || std::string_view(l_Line).substr(l_NameAt, name.size()) != name)
                    {
                        continue;
                    }

                    const std::size_t l_NameEnd = l_NameAt + name.size();
                    const std::size_t l_Next = l_Line.find_first_not_of(" \t\r", l_NameEnd);
                    if ((l_NameEnd < l_Line.size() && IsNameCharacter(l_Line[l_NameEnd])) || (l_Next != std::string::npos && l_Line[l_Next] == ';'))
                    {
                        continue;
                    }

                    return l_Number;
                }
            }
        }

        return std::nullopt;
    }

    // A script class's file in the folder or below it, with the line that declares the class, as Edit Script opens it: the file named after the class, as Unity has it, in the order of the extensions given, then any file that declares it, then the named file at its top. Hidden and build output folders are passed over
    std::optional<std::pair<std::filesystem::path, std::uint32_t>> FindScriptSource(const std::filesystem::path& folder, std::span<const std::string_view> extensions, std::string_view name)
    {
        constexpr std::array<std::string_view, 4> c_SkippedFolders{ "bin", "obj", "build", "out" };

        std::vector<std::filesystem::path> l_Files;
        std::error_code l_Error;
        for (std::filesystem::recursive_directory_iterator it_Entry(folder, std::filesystem::directory_options::skip_permission_denied, l_Error); !l_Error && it_Entry != std::filesystem::recursive_directory_iterator(); it_Entry.increment(l_Error))
        {
            std::error_code l_TypeError;
            if (it_Entry->is_directory(l_TypeError))
            {
                std::string l_Name = ToUtf8(it_Entry->path().filename());
                std::ranges::transform(l_Name, l_Name.begin(), [](char character) { return static_cast<char>(std::tolower(static_cast<unsigned char>(character))); });
                if (l_Name.starts_with('.') || std::ranges::find(c_SkippedFolders, l_Name) != c_SkippedFolders.end())
                {
                    it_Entry.disable_recursion_pending();
                }

                continue;
            }

            if (std::ranges::find(extensions, ToUtf8(it_Entry->path().extension())) != extensions.end())
            {
                l_Files.push_back(it_Entry->path());
            }
        }

        std::ranges::sort(l_Files);

        std::vector<std::filesystem::path> l_Named;
        for (const std::string_view it_Extension : extensions)
        {
            for (const std::filesystem::path& it_File : l_Files)
            {
                if (ToUtf8(it_File.stem()) == name && ToUtf8(it_File.extension()) == it_Extension)
                {
                    l_Named.push_back(it_File);
                }
            }
        }

        for (const std::vector<std::filesystem::path>* it_Files : { &l_Named, &l_Files })
        {
            for (const std::filesystem::path& it_File : *it_Files)
            {
                if (const std::optional<std::uint32_t> l_Line = FindClassLine(it_File, name))
                {
                    return std::pair(it_File, *l_Line);
                }
            }
        }

        if (!l_Named.empty())
        {
            return std::pair(l_Named.front(), 0u);
        }

        return std::nullopt;
    }

    // The first of Name, Name 1, Name 2 and on, before the extension, that names nothing in the folder yet. A copy starts at 1, or after the number its original's name ends in, as Unity numbers duplicates
    std::string GetFreePath(const Trinity::Project& project, std::string_view folder, std::string_view stem, std::string_view extension, bool copy)
    {
        std::string_view l_Base = stem;
        std::uint32_t l_Number = copy ? 1 : 0;
        const std::size_t l_Space = stem.find_last_of(' ');
        std::uint32_t l_Ending = 0;
        if (copy && l_Space != std::string_view::npos && l_Space + 1 < stem.size() && std::from_chars(stem.data() + l_Space + 1, stem.data() + stem.size(), l_Ending).ptr == stem.data() + stem.size())
        {
            l_Base = stem.substr(0, l_Space);
            l_Number = l_Ending + 1;
        }

        for (;; ++l_Number)
        {
            const std::string l_Path = l_Number == 0 ? std::format("{}/{}{}", folder, l_Base, extension) : std::format("{}/{} {}{}", folder, l_Base, l_Number, extension);
            std::error_code l_Error;
            if (!std::filesystem::exists(project.ToNativePath(l_Path), l_Error))
            {
                return l_Path;
            }
        }
    }

    // Everything an importer cooked for the asset into /cache
    void RemoveCooked(const Trinity::AssetRecord& record)
    {
        std::vector<std::string> l_Paths;
        if (record.Importer == TextureImporter::c_Importer)
        {
            l_Paths = { Trinity::GetCookedTexturePath(record.ID), TextureImporter::GetCacheKeyPath(record.ID) };
        }
        else if (record.Importer == Trinity::MeshAsset::c_AssetType)
        {
            l_Paths = { Trinity::GetCookedMeshPath(record.ID) };
        }
        else if (record.Importer == Trinity::ConvexHullAsset::c_AssetType || record.Importer == Trinity::CollisionMeshAsset::c_AssetType)
        {
            l_Paths = { Trinity::GetCookedCollisionShapePath(record.ID) };
        }
        else if (record.Importer == ModelImporter::c_MaterialImporter && record.Parent.IsValid())
        {
            l_Paths = { Trinity::GetCookedMaterialPath(record.ID), ModelImporter::GetImportedMaterialPath(record.ID) };
        }
        else if (record.Importer == ModelImporter::c_Importer)
        {
            l_Paths = { Trinity::GetCookedModelPath(record.ID), ModelImporter::GetCacheKeyPath(record.ID) };
        }
        else if (record.Importer == EnvironmentImporter::c_Importer)
        {
            l_Paths = { Trinity::GetCookedEnvironmentPath(record.ID), EnvironmentImporter::GetCacheKeyPath(record.ID) };
        }

        for (const std::string& it_Path : l_Paths)
        {
            static_cast<void>(Trinity::FileSystem::RemoveFile(it_Path));
        }
    }

    // The key an importer checks its cache against, so the next import cooks the asset again. A model's embedded textures have keys of their own. Whether the asset is one an importer cooks
    bool RemoveCacheKey(const Trinity::AssetRecord& record)
    {
        std::string l_Path;
        if (record.Importer == TextureImporter::c_Importer)
        {
            l_Path = TextureImporter::GetCacheKeyPath(record.ID);
        }
        else if (record.Importer == ModelImporter::c_Importer)
        {
            l_Path = ModelImporter::GetCacheKeyPath(record.ID);
        }
        else if (record.Importer == EnvironmentImporter::c_Importer)
        {
            l_Path = EnvironmentImporter::GetCacheKeyPath(record.ID);
        }

        if (l_Path.empty())
        {
            return false;
        }

        static_cast<void>(Trinity::FileSystem::RemoveFile(l_Path));

        return true;
    }

    // Each slot of the scene naming an asset the project does not have, or an entity the scene does not have, as a line naming the entity, its component, the slot and what it named. Built-in assets are in every project
    std::vector<std::string> FindMissingReferences(Trinity::Scene& scene, const Trinity::AssetRegistry& registry)
    {
        std::vector<std::string> l_Lines;
        for (Trinity::Entity it_Entity = scene.GetFirstRoot(); it_Entity; it_Entity = scene.GetNextInHierarchyOrder(it_Entity))
        {
            const std::string l_Entity = GetEntityLabel(it_Entity);
            ForEachSlotReference(it_Entity, [&](const SlotReference& reference)
            {
                const bool l_Missing = reference.IsEntity ? !scene.FindEntityByUUID(reference.Value) : registry.Find(reference.Value) == nullptr && !Trinity::AssetManager::IsBuiltIn(reference.Value);
                if (!reference.Value || !l_Missing)
                {
                    return;
                }

                const std::string l_Component = reference.Script != nullptr ? GetScriptLabel(*reference.Script) : std::string(GetComponentLabel(reference.Component));
                const std::string l_Slot = reference.Index ? std::format("{} {}", reference.Slot, *reference.Index) : std::string(reference.Slot);
                const std::string_view l_Type = reference.IsEntity ? std::string_view("Entity") : reference.AssetType.empty() ? std::string_view("Asset") : reference.AssetType;
                l_Lines.push_back(std::format("{} > {} > {}: {} {}", l_Entity, l_Component, l_Slot, l_Type, reference.Value));
            });
        }

        return l_Lines;
    }
}

// Before the asset manager shuts down, which must not be left holding this project's registry. A preference changed by a drag Forge closed during is saved too
EditorSession::~EditorSession()
{
    if (m_PreferencesChanged)
    {
        WritePreferences();
    }

    CloseProject();
    Trinity::SceneSerializer::SetPrefabResolver({});
}

void EditorSession::Start(const Trinity::ApplicationCommandLineArgs& args)
{
    // A scene opens at once, before the asset manager could load its prefabs, so Forge reads them itself, the playing scene's too
    Trinity::SceneSerializer::SetPrefabResolver([this](Trinity::UUID prefab) { return m_Prefabs.Find(prefab, m_Registry.get()); });

    m_Module.SetBuildStarting([this]
    {
        if (m_BeforeBuild)
        {
            m_BeforeBuild();
        }
    });
    m_Assembly.SetBuildStarting([this]
    {
        if (m_BeforeBuild)
        {
            m_BeforeBuild();
        }
    });

    // A running scene plays on through a reload: its C++ instances write their fields back and go before the module does, and are made again from those fields once the new module has registered. Its components are kept as YAML meanwhile, as the edited scene's are
    m_Module.SetReloadCallbacks([this]
    {
        if (Trinity::SceneRuntime* l_Runtime = GetRuntime())
        {
            const std::uint32_t l_Released = l_Runtime->GetScripts().ReleaseInstances(Trinity::ScriptLanguage::Cpp);
            TR_INFO("Forge: {} C++ script instance(s) wrote their fields back for the reload", l_Released);
        }
    },
        [this]
    {
        if (Trinity::SceneRuntime* l_Runtime = GetRuntime())
        {
            const std::uint32_t l_Made = l_Runtime->GetScripts().RecreateInstances(Trinity::ScriptLanguage::Cpp);
            TR_INFO("Forge: {} C++ script instance(s) were made again, and play goes on", l_Made);
        }
    });

    // C# scripts reload as a C++ module does, and their instances made as they first load while a scene plays
    m_Assembly.SetReloadCallbacks([this]
    {
        if (Trinity::SceneRuntime* l_Runtime = GetRuntime())
        {
            const std::uint32_t l_Released = l_Runtime->GetScripts().ReleaseInstances(Trinity::ScriptLanguage::CSharp);
            TR_INFO("Forge: {} C# script instance(s) wrote their fields back for the reload", l_Released);
        }
    },
        [this]
    {
        if (Trinity::SceneRuntime* l_Runtime = GetRuntime())
        {
            const std::uint32_t l_Made = l_Runtime->GetScripts().RecreateInstances(Trinity::ScriptLanguage::CSharp);
            TR_INFO("Forge: {} C# script instance(s) were made, and play goes on", l_Made);
        }
    });

    ReadPreferences(args);
    ReadRecentProjects();
    if (const auto it_Directory = args.GetOption("new-project"))
    {
        CreateProject(FromUtf8(*it_Directory));
    }
    else if (const auto it_Project = args.GetOption("project"))
    {
        OpenProject(FromUtf8(*it_Project));
    }
    else
    {
        TR_INFO("No project given. Use File > New Project or Open Project, start Forge from Trinity-Hub, or pass --project=<path>.");
    }

    UpdateTitle();
}

// From the UI, which is mid-frame. The work waits for Update, where a native dialog can block without an ImGui frame open
void EditorSession::Request(Command command)
{
    m_Pending.push_back([this, command] { Run(command); });
}

void EditorSession::Update()
{
    // Once a drag or a typed value is let go, so a slider does not write the file every frame
    if (m_PreferencesChanged && !ImGui::IsAnyItemActive())
    {
        WritePreferences();
    }

    m_Module.Update();
    m_Assembly.Update();
    m_Reimporter.Update();
    if (const std::optional<ModelImportReport> l_Report = m_ModelImports.Update())
    {
        FinishModelImports(*l_Report);
    }

    if (m_TexturesAfterModels && !m_ModelImports.IsPlanning())
    {
        m_TexturesAfterModels = false;
        StartTextureImports();
    }

    if (const std::optional<TextureImportReport> l_Report = m_Imports.Update())
    {
        FinishImports(*l_Report);
    }

    if (const std::optional<EnvironmentImportReport> l_Report = m_EnvironmentImports.Update())
    {
        FinishEnvironmentImports(*l_Report);
    }

    ImportAgainIfAsked();

    std::vector<Action> l_Pending = std::exchange(m_Pending, {});
    for (Action& it_Action : l_Pending)
    {
        if (it_Action)
        {
            it_Action();
        }
    }

    UpdateDebuggerWait();

    // The builds Play waited for have ended, each loading what it built first
    if (m_PlayAfterBuild && !IsBuildingScripts())
    {
        m_PlayAfterBuild = false;
        BeginPlay();
    }

    // A backup is kept only while the scene has changes no file holds, so it goes once they are saved, undone, or left behind for another scene
    if (m_HasBackup && !IsDirty())
    {
        RemoveBackup();
    }
}

void EditorSession::DrawPopups()
{
    DrawUnsavedPopup();
    DrawPathPopup();
    DrawImportPopup();
    DrawDebuggerPopup();
    DrawRecoveryPopup();
}

// The project, the scene and an asterisk while unsaved. The renderer adds the backend and frame rate
void EditorSession::UpdateTitle()
{
    const std::string l_Title = m_Project ? std::format("{} - {}{} - {}", m_Project->GetName(), GetSceneName(), IsDirty() ? "*" : "", c_ApplicationTitle) : std::string(c_ApplicationTitle);
    if (l_Title != m_Title)
    {
        m_Title = l_Title;
        Trinity::Application::Get().GetRenderer().SetTitle(m_Title);
    }
}

// Import settings not applied are settled first, after which Forge is asked to close again
bool EditorSession::RequestClose()
{
    StopPlay();
    Action l_Close = [] { Trinity::Application::Get().RequestClose(); };
    if (SettleUnappliedEdits(l_Close))
    {
        return false;
    }

    if (!IsDirty())
    {
        return true;
    }

    ConfirmDiscard([] { Trinity::Application::Get().Close(); });

    return false;
}

// Refresh comes whenever Forge regains focus, and a build reloads the module or loads the scripts, and each leaves a playing scene running
void EditorSession::Run(Command command)
{
    if (command != Command::Refresh && command != Command::BuildModule && command != Command::BuildAssembly)
    {
        StopPlay();
    }

    const bool l_HasProject = m_Project != nullptr;
    switch (command)
    {
        case Command::NewProject:
        {
            ConfirmCloseProject([this]
            {
                AskPath({ PathKind::Folder, "Choose an empty folder for the new project",{},{},{}, [this](const std::filesystem::path& path) { CreateProject(path); } });
            });
            break;
        }
        case Command::OpenProject:
        {
            ConfirmCloseProject([this]
            {
                AskPath({ PathKind::OpenFile, "Open Project", c_ProjectFilters, m_Project ? m_Project->GetDirectory().parent_path() : std::filesystem::path(),{}, [this](const std::filesystem::path& path) { OpenProject(path); } });
            });
            break;
        }
        case Command::NewScene:
        {
            if (l_HasProject)
            {
                ConfirmDiscard([this] { NewScene(); });
            }
            break;
        }
        case Command::OpenScene:
        {
            if (l_HasProject)
            {
                ConfirmDiscard([this]
                {
                    AskPath({ PathKind::OpenFile, "Open Scene", c_SceneFilters, m_Project->GetAssetsDirectory(),{}, [this](const std::filesystem::path& path)
                    {
                        if (const std::optional<std::string> l_AssetPath = m_Project->ToAssetPath(path))
                        {
                            OpenScene(*l_AssetPath);
                        }
                        else
                        {
                            TR_ERROR("Forge: {} is outside the project's Assets folder", ToUtf8(path));
                        }
                    } });
                });
            }
            break;
        }
        case Command::SaveScene:
        {
            if (l_HasProject)
            {
                SaveScene({});
            }

            break;
        }
        case Command::SaveSceneAs:
        {
            if (l_HasProject)
            {
                SaveSceneAs({});
            }

            break;
        }
        case Command::SetStartScene:
        {
            const Trinity::AssetRecord* l_Record = l_HasProject && !m_ScenePath.empty() ? m_Registry->FindByPath(m_ScenePath) : nullptr;
            if (l_Record != nullptr)
            {
                m_Project->SetStartScene(l_Record->ID);
                SaveProject();
                TR_INFO("Forge: {} ({}) is now the start scene of {}", m_ScenePath, l_Record->ID, m_Project->GetName());
            }

            break;
        }
        case Command::Refresh:
        {
            if (l_HasProject)
            {
                ScanAssets();
            }

            break;
        }
        case Command::CreateModule:
        {
            if (l_HasProject && m_Module.Create(*m_Project))
            {
                SaveProject();
                DebuggerSettings::Write(*m_Project);
            }

            break;
        }
        case Command::BuildModule:
        {
            m_Module.RequestBuild();

            break;
        }
        case Command::CreateAssembly:
        {
            if (l_HasProject && m_Assembly.Create(*m_Project))
            {
                SaveProject();
                DebuggerSettings::Write(*m_Project);
            }

            break;
        }
        case Command::BuildAssembly:
        {
            m_Assembly.RequestBuild();

            break;
        }
        case Command::Exit:
        {
            Trinity::Application::Get().RequestClose();

            break;
        }
    }
}

// A native dialog where the platform has one, which blocks here, and otherwise a text field in a popup
void EditorSession::AskPath(PathRequest request)
{
    if (!Trinity::FileDialog::IsSupported())
    {
        m_PathText = ToUtf8(request.Kind == PathKind::SaveFile ? request.Folder / FromUtf8(request.FileName) : request.Folder);
        m_PathPrompt = std::move(request);
        m_OpenPathPopup = true;

        return;
    }

    std::optional<std::filesystem::path> l_Path;
    switch (request.Kind)
    {
        case PathKind::OpenFile:
        {
            l_Path = Trinity::FileDialog::OpenFile(request.Title, request.Filters, request.Folder);
            break;
        }
        case PathKind::SaveFile:
        {
            l_Path = Trinity::FileDialog::SaveFile(request.Title, request.Filters, request.Folder, request.FileName);
            break;
        }
        case PathKind::Folder:
        {
            l_Path = Trinity::FileDialog::PickFolder(request.Title, request.Folder);
            break;
        }
    }

    if (l_Path)
    {
        request.OnChosen(*l_Path);
    }
}

void EditorSession::ConfirmDiscard(Action action)
{
    if (!IsDirty())
    {
        action();

        return;
    }

    m_AfterDiscard = std::move(action);
    m_OpenUnsavedPopup = true;
}

// Before another project opens: edits held outside the scene are settled first, then the scene's unsaved changes are asked about
void EditorSession::ConfirmCloseProject(Action action)
{
    Action l_Then = [this, l_Action = std::move(action)]() mutable { ConfirmDiscard(std::move(l_Action)); };
    if (!SettleUnappliedEdits(l_Then))
    {
        l_Then();
    }
}

void EditorSession::SetUnappliedEdits(std::function<bool()> pending, std::function<void(std::move_only_function<void()>)> settle)
{
    m_HasUnappliedEdits = std::move(pending);
    m_SettleUnappliedEdits = std::move(settle);
}

// Takes what waits only when there is something to settle, and runs it at the next Update once settled, so a file dialog it opens never blocks with a frame open
bool EditorSession::SettleUnappliedEdits(Action& then)
{
    if (!m_HasUnappliedEdits || !m_SettleUnappliedEdits || !m_HasUnappliedEdits())
    {
        return false;
    }

    m_SettleUnappliedEdits([this, l_Then = std::move(then)]() mutable { m_Pending.push_back(std::move(l_Then)); });

    return true;
}

// A new project opens on its empty start scene, saved and given a UUID at once so the project file never names a missing scene
void EditorSession::CreateProject(const std::filesystem::path& directory)
{
    CloseProject();
    NewScene();

    Trinity::Expected<Trinity::Scope<Trinity::Project>, std::string> l_Project = Trinity::Project::Create(directory);
    if (!l_Project)
    {
        TR_ERROR("Forge: the project could not be created: {}", l_Project.GetError());

        return;
    }

    m_Project = std::move(*l_Project);
    AttachRegistry();
    m_Module.Open(*m_Project);
    m_Assembly.Open(*m_Project);
    DebuggerSettings::Write(*m_Project);
    RememberProject();
    ReadRecentScenes();

    const std::string l_StartScene = std::format("{}/{}", Trinity::Project::c_AssetsMount, Trinity::Project::c_DefaultStartScene);
    if (!WriteScene(l_StartScene))
    {
        return;
    }

    m_ScenePath = l_StartScene;
    RememberScene();
    if (const Trinity::AssetRecord* l_Record = m_Registry->FindByPath(l_StartScene))
    {
        m_Project->SetStartScene(l_Record->ID);
        SaveProject();
        TR_INFO("Forge: created project {} in {}, with an empty start scene at {} ({})", m_Project->GetName(), ToUtf8(m_Project->GetDirectory()), m_ScenePath, l_Record->ID);
    }
}

void EditorSession::OpenProject(const std::filesystem::path& path)
{
    CloseProject();
    NewScene();

    Trinity::Expected<Trinity::Scope<Trinity::Project>, std::string> l_Project = Trinity::Project::Open(path);
    if (!l_Project)
    {
        TR_ERROR("Forge: the project could not be opened: {}", l_Project.GetError());

        return;
    }

    m_Project = std::move(*l_Project);
    AttachRegistry();
    m_Module.Open(*m_Project);
    m_Assembly.Open(*m_Project);
    DebuggerSettings::Write(*m_Project);
    RememberProject();
    ReadRecentScenes();

    // A backup left by a crash is asked about once the start scene is open
    m_Recovery = ReadBackup();
    m_OpenRecoveryPopup = m_Recovery.has_value();

    if (m_Project->ResolveStartScenePath(*m_Registry))
    {
        SaveProject();
        TR_INFO("Forge: {} now names its start scene by UUID", m_Project->GetName());
    }

    const Trinity::UUID l_StartScene = m_Project->GetStartScene();
    const Trinity::AssetRecord* l_Record = l_StartScene ? m_Registry->Find(l_StartScene) : nullptr;
    if (l_Record == nullptr)
    {
        TR_INFO("Forge: {} has {}, so it opens on a new scene", m_Project->GetName(), l_StartScene ? std::format("no asset {} for its start scene", l_StartScene) : std::string("no start scene"));

        return;
    }

    OpenScene(l_Record->Path);
}

// The registry goes before the project unmounts /assets, and the asset manager lets go of it first
void EditorSession::CloseProject()
{
    StopPlay();
    // Closing as it should, so this session's backup goes, while one left by a crash and not yet answered stays to be asked about again
    RemoveBackup();
    m_Recovery.reset();
    m_OpenRecoveryPopup = false;
    m_Module.Close();
    m_Assembly.Close();

    // The texture being encoded and the environment being filtered are finished, since neither can be left halfway, and the rest wait for the project to open again
    m_ImportAgain = false;
    m_TexturesAfterModels = false;
    if (m_ModelImports.IsRunning())
    {
        m_ModelImports.Stop();
        FinishModelImports(m_ModelImports.Wait());
    }

    if (m_Imports.IsRunning())
    {
        m_Imports.Stop();
        FinishImports(m_Imports.Wait());
    }

    if (m_EnvironmentImports.IsRunning())
    {
        m_EnvironmentImports.Stop();
        FinishEnvironmentImports(m_EnvironmentImports.Wait());
    }

    m_Reimporter.Finish();
    m_InspectedAsset = {};
    // Its components name the project's assets
    m_CopiedEntity.reset();
    m_RecentScenes.clear();
    // Read again for the start screen, which shows it once no project is open, with whatever has gone since
    ReadRecentProjects();

    // Everything holding the project's assets lets go of them, so none are left loaded from it when another project's registry is set
    for (auto& [it_ID, it_Listener] : m_CloseListeners)
    {
        it_Listener();
    }

    Trinity::Application::Get().GetRenderer().GetRenderer2D().ReleaseTextures();
    Trinity::Application::Get().GetRenderer().GetRenderer3D().ReleaseAssets();

    if (m_Registry)
    {
        Trinity::AssetManager::SetRegistry(nullptr);
        m_Registry.reset();
    }

    m_Project.reset();
}

void EditorSession::AttachRegistry()
{
    m_Registry = Trinity::CreateScope<Trinity::AssetRegistry>(Trinity::Project::c_AssetsMount);
    m_Registry->SetDefaultSettings(TextureImporter::c_Importer, TextureImporter::GetDefaultSettings());
    m_Registry->SetDefaultSettings(ModelImporter::c_Importer, ModelImporter::GetDefaultSettings());
    Trinity::AssetManager::SetRegistry(m_Registry.get());
    ScanAssets();
}

// Every texture, model and environment the scan finds is cooked into /cache in the background, unless the cache already holds it for the same files and settings
void EditorSession::ScanAssets()
{
    if (!m_Registry)
    {
        return;
    }

    // The open scene is found again by its UUID, so it follows its file when that is moved or renamed
    const Trinity::UUID l_Scene = GetSceneID();
    static_cast<void>(m_Registry->Scan());
    ++m_ScanCount;
    if (const Trinity::AssetRecord* l_Record = l_Scene ? m_Registry->Find(l_Scene) : nullptr)
    {
        m_ScenePath = l_Record->Path;
    }

    UpdateChangedPrefabs();
    StartImports();
}

// A prefab changed outside Forge, as by a text editor or version control, reaches the edited scene's instances as the files are scanned, each made again from it, keeping what it changes. Not a step of the history, as in Unity, and a scene playing keeps the prefab it started with
void EditorSession::UpdateChangedPrefabs()
{
    for (auto& [it_Prefab, it_Text] : m_Prefabs.FindChanged(m_Registry.get()))
    {
        const std::string l_Path = m_Registry->Find(it_Prefab)->Path;
        const std::vector<Trinity::SceneSerializer::PrefabInstanceText> l_Instances = Trinity::SceneSerializer::SavePrefabInstances(m_Scene, it_Prefab);
        if (const Trinity::Expected<void, std::string> l_Replaced = m_Prefabs.Replace(it_Prefab, std::move(it_Text)); !l_Replaced)
        {
            TR_ERROR("Forge: prefab {} changed, and cannot be read now, so its instances stay as they are: {}", l_Path, l_Replaced.GetError());

            continue;
        }

        if (const Trinity::Expected<void, std::string> l_Restored = Trinity::SceneSerializer::RestorePrefabInstances(m_Scene, l_Instances); !l_Restored)
        {
            TR_ERROR("Forge: prefab {} changed, and not every instance could be made again from it: {}", l_Path, l_Restored.GetError());
        }

        Trinity::AssetManager::Reload(it_Prefab);
        TR_INFO("Forge: prefab {} changed outside Forge, so its {} instance(s) in the scene were made again from it", l_Path, l_Instances.size());
    }

    if (m_Selection && !GetScene().FindEntityByUUID(m_Selection))
    {
        m_Selection = {};
    }
}

// Until every import the scans so far asked for is over, with the last report of each kind. For tests, which need the cache filled before they go on, so models are cooked before textures start here
EditorSession::ImportReport EditorSession::WaitForImports()
{
    ImportReport l_Report;
    while (IsImportRunning() || (m_ImportAgain && m_Registry))
    {
        if (m_ModelImports.IsRunning())
        {
            l_Report.Models = m_ModelImports.Wait();
            FinishModelImports(l_Report.Models);
        }

        if (std::exchange(m_TexturesAfterModels, false))
        {
            StartTextureImports();
        }

        if (m_Imports.IsRunning())
        {
            l_Report.Textures = m_Imports.Wait();
            FinishImports(l_Report.Textures);
        }

        if (m_EnvironmentImports.IsRunning())
        {
            l_Report.Environments = m_EnvironmentImports.Wait();
            FinishEnvironmentImports(l_Report.Environments);
        }

        ImportAgainIfAsked();
    }

    return l_Report;
}

// A scan while imports run imports again once they are over, since each batch has the records it started with. Models go first, and textures once the models are planned, so a texture beside a model is encoded the way the model uses it. Environments need nothing else, so they start at once
void EditorSession::StartImports()
{
    if (IsImportRunning())
    {
        m_ImportAgain = true;

        return;
    }

    std::vector<Trinity::AssetRecord> l_Models;
    std::vector<Trinity::AssetRecord> l_Environments;
    for (const Trinity::AssetRecord* it_Record : m_Registry->GetRecords())
    {
        if (it_Record->Importer == ModelImporter::c_Importer)
        {
            l_Models.push_back(*it_Record);
        }
        else if (it_Record->Importer == EnvironmentImporter::c_Importer)
        {
            l_Environments.push_back(*it_Record);
        }
    }

    if (!l_Environments.empty())
    {
        m_EnvironmentImports.Start(std::move(l_Environments));
    }

    if (l_Models.empty())
    {
        StartTextureImports();

        return;
    }

    m_ModelImports.Start(*m_Registry, std::move(l_Models));
    m_TexturesAfterModels = true;
}

// Texture files only, since a model encodes the textures it holds itself. Textures the background reimport is busy with are left to it
void EditorSession::StartTextureImports()
{
    std::vector<Trinity::AssetRecord> l_Records;
    for (const Trinity::AssetRecord* it_Record : m_Registry->GetRecords())
    {
        if (it_Record->Importer == TextureImporter::c_Importer && !it_Record->Parent.IsValid() && !m_Reimporter.IsBusy(it_Record->ID))
        {
            l_Records.push_back(*it_Record);
        }
    }

    if (!l_Records.empty())
    {
        m_Imports.Start(std::move(l_Records));
    }
}

void EditorSession::ImportAgainIfAsked()
{
    if (m_ImportAgain && m_Registry && !IsImportRunning())
    {
        m_ImportAgain = false;
        StartImports();
    }
}

// A folder whose textures are all in the cache stays quiet
void EditorSession::FinishImports(const TextureImportReport& report)
{
    if (report.Encoded != 0 || report.Failed != 0 || report.Stopped != 0)
    {
        TR_INFO("Textures: {} in the project, {} encoded, {} from the cache, {} failed", report.Textures, report.Encoded, report.Cached, report.Failed);
    }

    if (report.Stopped != 0)
    {
        TR_INFO("Textures: the import was stopped with {} texture(s) left, which are imported at the next refresh or when the project opens again", report.Stopped);
    }
}

bool EditorSession::IsImportRunning() const
{
    return m_Imports.IsRunning() || m_ModelImports.IsRunning() || m_EnvironmentImports.IsRunning() || m_TexturesAfterModels;
}

void EditorSession::FinishEnvironmentImports(const EnvironmentImportReport& report)
{
    if (report.Filtered != 0 || report.Failed != 0 || report.Stopped != 0)
    {
        TR_INFO("Environments: {} in the project, {} filtered, {} from the cache, {} failed", report.Environments, report.Filtered, report.Cached, report.Failed);
    }

    if (report.Stopped != 0)
    {
        TR_INFO("Environments: the import was stopped with {} environment(s) left, which are imported at the next refresh or when the project opens again", report.Stopped);
    }
}

void EditorSession::FinishModelImports(const ModelImportReport& report)
{
    ++m_ModelImportCount;
    if (report.Imported != 0 || report.Failed != 0 || report.Stopped != 0)
    {
        TR_INFO("Models: {} in the project, {} imported, {} from the cache, {} failed, with {} embedded texture(s) encoded and {} from the cache", report.Models, report.Imported, report.Cached, report.Failed, report.TexturesEncoded, report.TexturesCached);
    }

    if (report.Stopped != 0)
    {
        TR_INFO("Models: the import was stopped with {} model(s) left, which are imported at the next refresh or when the project opens again", report.Stopped);
    }
}

// Called before the project closes, by anything holding its assets
std::uint64_t EditorSession::AddCloseListener(std::move_only_function<void()> listener)
{
    const std::uint64_t l_ID = m_NextCloseListener++;
    m_CloseListeners.emplace_back(l_ID, std::move(listener));

    return l_ID;
}

void EditorSession::RemoveCloseListener(std::uint64_t id)
{
    std::erase_if(m_CloseListeners, [id](const auto& listener) { return listener.first == id; });
}

void EditorSession::RequestOpenScene(std::string assetPath)
{
    m_Pending.push_back([this, l_Path = std::move(assetPath)]() mutable
    {
        StopPlay();
        ConfirmDiscard([this, l_Path = std::move(l_Path)] { OpenScene(l_Path); });
    });
}

void EditorSession::RequestOpenProject(std::filesystem::path path)
{
    m_Pending.push_back([this, l_Path = std::move(path)]() mutable
    {
        StopPlay();
        ConfirmCloseProject([this, l_Path = std::move(l_Path)] { OpenProject(l_Path); });
    });
}

void EditorSession::RemoveRecentProject(const std::filesystem::path& path)
{
    if (std::erase_if(m_RecentProjects, [&path](const RecentProject& project) { return project.Path == path; }) > 0)
    {
        WriteRecentProjects();
    }
}

void EditorSession::RemoveRecentScene(Trinity::UUID scene)
{
    if (std::erase_if(m_RecentScenes, [scene](const RecentScene& recent) { return recent.ID == scene; }) > 0)
    {
        WriteRecentScenes();
    }
}

void EditorSession::ClearRecentScenes()
{
    m_RecentScenes.clear();
    WriteRecentScenes();
}

std::string EditorSession::FormatAge(std::chrono::system_clock::time_point time)
{
    const std::int64_t l_Seconds = std::max<std::int64_t>(0, std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - time).count());
    const auto a_Count = [](std::int64_t count, std::string_view unit) { return std::format("{} {}{} ago", count, unit, count == 1 ? "" : "s"); };
    if (l_Seconds < 60)
    {
        return "less than a minute ago";
    }

    if (l_Seconds < 3600)
    {
        return a_Count(l_Seconds / 60, "minute");
    }

    return l_Seconds < 86400 ? a_Count(l_Seconds / 3600, "hour") : a_Count(l_Seconds / 86400, "day");
}

// A line a project, newest first: when it was opened, in seconds since 1970, its name and its file, apart by tabs. A line that cannot be read is left out
void EditorSession::ReadRecentProjects()
{
    m_RecentProjects.clear();
    const Trinity::Expected<std::string, Trinity::FileError> l_Text = Trinity::FileSystem::ReadText(c_RecentProjectsPath);
    if (!l_Text)
    {
        return;
    }

    for (const std::string_view it_Line : SplitLines(*l_Text))
    {
        if (m_RecentProjects.size() == c_RecentLimit)
        {
            break;
        }

        const std::size_t l_NameStart = it_Line.find('\t');
        const std::size_t l_PathStart = l_NameStart == std::string_view::npos ? std::string_view::npos : it_Line.find('\t', l_NameStart + 1);
        std::int64_t l_Seconds = 0;
        if (l_PathStart == std::string_view::npos || std::from_chars(it_Line.data(), it_Line.data() + l_NameStart, l_Seconds).ec != std::errc())
        {
            continue;
        }

        std::filesystem::path l_Path = FromUtf8(it_Line.substr(l_PathStart + 1));
        std::error_code l_Error;
        const bool l_Missing = !std::filesystem::is_regular_file(l_Path, l_Error);
        m_RecentProjects.push_back({ .Path = std::move(l_Path), .Name = std::string(it_Line.substr(l_NameStart + 1, l_PathStart - l_NameStart - 1)), .OpenedAt = std::chrono::system_clock::time_point(std::chrono::seconds(l_Seconds)), .Missing = l_Missing });
    }
}

void EditorSession::WriteRecentProjects() const
{
    std::string l_Text;
    for (const RecentProject& it_Project : m_RecentProjects)
    {
        l_Text += std::format("{}\t{}\t{}\n", std::chrono::duration_cast<std::chrono::seconds>(it_Project.OpenedAt.time_since_epoch()).count(), it_Project.Name, ToUtf8(it_Project.Path));
    }

    if (const Trinity::Expected<void, Trinity::FileError> l_Written = Trinity::FileSystem::WriteText(c_RecentProjectsPath, l_Text); !l_Written)
    {
        TR_WARN("Forge: the recent projects could not be saved: {}", Trinity::ToString(l_Written.GetError()));
    }
}

// The open project goes first, once, and the eleventh is let go
void EditorSession::RememberProject()
{
    std::erase_if(m_RecentProjects, [this](const RecentProject& project) { return project.Path == m_Project->GetFilePath(); });
    m_RecentProjects.insert(m_RecentProjects.begin(), { .Path = m_Project->GetFilePath(), .Name = m_Project->GetName(), .OpenedAt = std::chrono::system_clock::now(), .Missing = false });
    if (m_RecentProjects.size() > c_RecentLimit)
    {
        m_RecentProjects.resize(c_RecentLimit);
    }

    WriteRecentProjects();
}

// A line a scene, newest first: its UUID and the path it had, apart by a tab, in the project's Cache, per user as Unity keeps its recent scenes
void EditorSession::ReadRecentScenes()
{
    m_RecentScenes.clear();
    const Trinity::Expected<std::string, Trinity::FileError> l_Text = Trinity::FileSystem::ReadText(GetRecentScenesPath());
    if (!l_Text)
    {
        return;
    }

    for (const std::string_view it_Line : SplitLines(*l_Text))
    {
        const std::size_t l_Tab = it_Line.find('\t');
        const std::optional<Trinity::UUID> l_ID = l_Tab != std::string_view::npos ? Trinity::UUID::Parse(it_Line.substr(0, l_Tab)) : std::nullopt;
        if (l_ID && m_RecentScenes.size() < c_RecentLimit)
        {
            m_RecentScenes.push_back({ .ID = *l_ID, .Path = std::string(it_Line.substr(l_Tab + 1)) });
        }
    }
}

void EditorSession::WriteRecentScenes() const
{
    std::string l_Text;
    for (const RecentScene& it_Scene : m_RecentScenes)
    {
        l_Text += std::format("{}\t{}\n", it_Scene.ID, it_Scene.Path);
    }

    if (const Trinity::Expected<void, Trinity::FileError> l_Written = Trinity::FileSystem::WriteText(GetRecentScenesPath(), l_Text); !l_Written)
    {
        TR_WARN("Forge: the recent scenes could not be saved: {}", Trinity::ToString(l_Written.GetError()));
    }
}

// The open scene goes first, once it has a file, and the eleventh is let go
void EditorSession::RememberScene()
{
    const Trinity::AssetRecord* l_Record = m_Registry && !m_ScenePath.empty() ? m_Registry->FindByPath(m_ScenePath) : nullptr;
    if (l_Record == nullptr)
    {
        return;
    }

    std::erase_if(m_RecentScenes, [l_Record](const RecentScene& recent) { return recent.ID == l_Record->ID; });
    m_RecentScenes.insert(m_RecentScenes.begin(), { .ID = l_Record->ID, .Path = l_Record->Path });
    if (m_RecentScenes.size() > c_RecentLimit)
    {
        m_RecentScenes.resize(c_RecentLimit);
    }

    WriteRecentScenes();
}

void EditorSession::SetPreferences(const EditorPreferences& preferences)
{
    m_Preferences = preferences;
    m_PreferencesChanged = true;
}

void EditorSession::SetPreferenceVariable(std::string_view name, std::string_view value)
{
    if (Trinity::ConsoleVariables::Set(name, value))
    {
        if (const auto it_Held = m_HeldVariables.find(name); it_Held != m_HeldVariables.end())
        {
            m_HeldVariables.erase(it_Held);
        }

        m_PreferencesChanged = true;
    }
}

void EditorSession::SetScriptEditor(ScriptEditor editor)
{
    m_Preferences.Editor = editor;
    m_PreferencesChanged = true;
}

void EditorSession::SetWaitForDebugger(bool wait)
{
    m_Preferences.WaitForDebugger = wait;
    m_PreferencesChanged = true;
}

// A line a setting, name=value, and lines starting with # left for people. One that cannot be read keeps its default, and one Forge does not know is left out. A kept console variable the command line sets keeps the command line's value, and the file's is held to be written back
void EditorSession::ReadPreferences(const Trinity::ApplicationCommandLineArgs& args)
{
    for (const std::string_view it_Assignment : args.GetAllOptions("set"))
    {
        const std::string_view l_Name = it_Assignment.substr(0, it_Assignment.find('='));
        if (std::ranges::find(EditorPreferences::c_Variables, l_Name) != EditorPreferences::c_Variables.end())
        {
            m_HeldVariables.emplace(l_Name, std::string());
        }
    }

    const Trinity::Expected<std::string, Trinity::FileError> l_Text = Trinity::FileSystem::ReadText(EditorPreferences::c_Path);
    m_HasPreferencesFile = l_Text.HasValue();
    if (!l_Text)
    {
        TR_INFO("Forge: no preferences yet, so they are Forge's defaults until one is changed");

        return;
    }

    for (const std::string_view it_Line : SplitLines(*l_Text))
    {
        const std::size_t l_Equals = it_Line.find('=');
        if (it_Line.starts_with('#') || l_Equals == std::string_view::npos)
        {
            continue;
        }

        const std::string_view l_Key = it_Line.substr(0, l_Equals);
        const std::string_view l_Value = it_Line.substr(l_Equals + 1);
        if (std::ranges::find(EditorPreferences::c_Variables, l_Key) != EditorPreferences::c_Variables.end())
        {
            if (const auto it_Held = m_HeldVariables.find(l_Key); it_Held != m_HeldVariables.end())
            {
                it_Held->second = std::string(l_Value);
            }
            else
            {
                static_cast<void>(Trinity::ConsoleVariables::Set(l_Key, l_Value));
            }
        }
        else if (l_Key == "ScriptEditor")
        {
            m_Preferences.Editor = IdeLauncher::FromKey(l_Value).value_or(ScriptEditor::System);
        }
        else if (l_Key == "WaitForDebugger")
        {
            m_Preferences.WaitForDebugger = l_Value == "1";
        }
        else if (l_Key == "PlayModeTint" && ReadNumbers(l_Value, std::span<float>(&m_Preferences.PlayModeTint.x, 3)))
        {
            m_Preferences.PlayModeTint = glm::clamp(m_Preferences.PlayModeTint, 0.0f, 1.0f);
        }
        else if (l_Key == "CameraSpeed" && ReadNumbers(l_Value, std::span<float>(&m_Preferences.CameraSpeed, 1)))
        {
            m_Preferences.CameraSpeed = std::clamp(m_Preferences.CameraSpeed, EditorCamera3D::c_MinimumSpeed, EditorCamera3D::c_MaximumSpeed);
        }
        else if (l_Key == "SnapSteps" && ReadNumbers(l_Value, std::span<float>(&m_Preferences.SnapSteps.x, 3)))
        {
            m_Preferences.SnapSteps = glm::clamp(m_Preferences.SnapSteps, EditorPreferences::c_MinimumSnap, EditorPreferences::c_MaximumSnap);
        }
    }

    TR_INFO("Forge: preferences read from {}", EditorPreferences::c_Path);
}

void EditorSession::WritePreferences()
{
    m_PreferencesChanged = false;
    std::string l_Text = "# Forge's preferences, the same in every project, as Edit > Preferences shows them\n";
    for (const std::string_view it_Name : EditorPreferences::c_Variables)
    {
        if (const auto it_Held = m_HeldVariables.find(it_Name); it_Held != m_HeldVariables.end())
        {
            if (!it_Held->second.empty())
            {
                l_Text += std::format("{}={}\n", it_Name, it_Held->second);
            }
        }
        else if (const Trinity::ConsoleVariableBase* l_Variable = Trinity::ConsoleVariables::Find(it_Name))
        {
            l_Text += std::format("{}={}\n", it_Name, l_Variable->ToString());
        }
    }

    l_Text += std::format("ScriptEditor={}\nWaitForDebugger={}\n", IdeLauncher::ToKey(m_Preferences.Editor), m_Preferences.WaitForDebugger ? 1 : 0);
    l_Text += std::format("PlayModeTint={} {} {}\n", m_Preferences.PlayModeTint.x, m_Preferences.PlayModeTint.y, m_Preferences.PlayModeTint.z);
    l_Text += std::format("CameraSpeed={}\nSnapSteps={} {} {}\n", m_Preferences.CameraSpeed, m_Preferences.SnapSteps.x, m_Preferences.SnapSteps.y, m_Preferences.SnapSteps.z);
    if (const Trinity::Expected<void, Trinity::FileError> l_Written = Trinity::FileSystem::WriteText(EditorPreferences::c_Path, l_Text); !l_Written)
    {
        TR_WARN("Forge: the preferences could not be saved: {}", Trinity::ToString(l_Written.GetError()));

        return;
    }

    m_HasPreferencesFile = true;
}

// A debugger attached already plays at once, and a Play asked for while one is awaited changes nothing. A build running or about to is waited for first, and a Play asked for while waiting stops the wait
void EditorSession::RequestPlay()
{
    m_Pending.push_back([this]
    {
        if (m_Play)
        {
            m_Paused = false;

            return;
        }

        if (m_DebuggerWait)
        {
            return;
        }

        if (m_PlayAfterBuild)
        {
            m_PlayAfterBuild = false;
            TR_INFO("Forge: Play no longer waits for the scripts to build");

            return;
        }

        if (IsBuildingScripts())
        {
            m_PlayAfterBuild = true;
            TR_INFO("Forge: Play waits for the scripts to finish building");

            return;
        }

        BeginPlay();
    });
}

// Once no build is running. Refused while a build has failed, as Unity refuses to play with compile errors, since the scripts would run as they were before the change. Then a debugger first, when Play is to wait for one
void EditorSession::BeginPlay()
{
    if (m_Play || !m_Project)
    {
        return;
    }

    if (const std::string l_Failed = GetFailedBuilds(); !l_Failed.empty())
    {
        TR_ERROR("Forge: {} failed to build, and every build error has to be fixed before the scene can play. The build indicator in the menu bar shows them", l_Failed);
        Notify("All build errors have to be fixed before you can enter Play mode");

        return;
    }

    if (m_Preferences.WaitForDebugger && !Trinity::Process::IsDebuggerAttached() && !Trinity::ManagedScripts::IsDebuggerAttached())
    {
        m_DebuggerWait.emplace();
        m_OpenDebuggerPopup = true;
        TR_INFO("Forge: Play waits for a debugger to attach to Forge, process {}", Trinity::Process::GetCurrentID());

        return;
    }

    StartPlay();
}

std::string EditorSession::GetFailedBuilds() const
{
    if (!m_Project)
    {
        return {};
    }

    const bool l_Module = m_Module.GetStatus().State == BuildState::Failed;
    const bool l_Assembly = m_Assembly.GetStatus().State == BuildState::Failed;
    const std::string l_ModuleName = std::format("the C++ module {}", m_Project->GetModuleName());
    const std::string l_AssemblyName = std::format("the C# project {}", m_Project->GetAssemblyName());
    if (l_Module && l_Assembly)
    {
        return std::format("{} and {}", l_ModuleName, l_AssemblyName);
    }

    return l_Module ? l_ModuleName : l_Assembly ? l_AssemblyName : std::string();
}

// Imports first, which keep assets from showing until they end, then builds
std::vector<EditorSession::Activity> EditorSession::GetActivities() const
{
    std::vector<Activity> l_Activities;
    if (m_Imports.IsRunning())
    {
        const TextureImportBatch::Progress l_Progress = m_Imports.GetProgress();
        const std::optional<double> l_Done = GetImportedTexels(l_Progress);
        const std::optional<float> l_Fraction = l_Done ? std::optional<float>(static_cast<float>(std::clamp(*l_Done / static_cast<double>(l_Progress.TotalTexels), 0.0, 1.0))) : std::nullopt;
        std::string l_Text = !l_Progress.Planned ? std::format("Checking {} texture(s)", l_Progress.Textures) : l_Progress.Encoding != 0 ? std::format("Encoding texture {} of {}", l_Progress.Encoding, l_Progress.ToEncode) : std::string("Finishing the texture import");
        l_Activities.push_back({ std::move(l_Text), l_Fraction });
    }

    if (m_ModelImports.IsRunning())
    {
        l_Activities.push_back({ "Importing models", std::nullopt });
    }

    if (m_EnvironmentImports.IsRunning())
    {
        l_Activities.push_back({ "Filtering environments", std::nullopt });
    }

    if (m_Reimporter.IsRunning())
    {
        l_Activities.push_back({ "Reimporting textures", std::nullopt });
    }

    if (m_Project && m_Module.IsBuilding())
    {
        l_Activities.push_back({ std::format("Building the C++ module {}", m_Project->GetModuleName()), std::nullopt });
    }

    if (m_Project && m_Assembly.IsBuilding())
    {
        l_Activities.push_back({ std::format("Building the C# project {}", m_Project->GetAssemblyName()), std::nullopt });
    }

    return l_Activities;
}

// A build running, or one to start once its sources settle or the build before it ends
bool EditorSession::IsBuildingScripts() const
{
    const auto a_Busy = [](BuildState state) { return state == BuildState::Pending || state == BuildState::Building; };

    return a_Busy(m_Module.GetStatus().State) || a_Busy(m_Assembly.GetStatus().State);
}

void EditorSession::RequestPause()
{
    m_Pending.push_back([this] { m_Paused = m_Play != nullptr && !m_Paused; });
}

// A running scene pauses first, so the step is the only one it takes
void EditorSession::RequestStep()
{
    m_Pending.push_back([this]
    {
        if (m_Play)
        {
            m_Paused = true;
            m_StepRequested = true;
        }
    });
}

void EditorSession::RequestStop()
{
    m_Pending.push_back([this] { StopPlay(); });
}

// After the session's own work, before the transform pass the frame draws with. A paused scene takes only the step asked for. A selection the scene switched to does not have is let go
void EditorSession::UpdatePlay(Trinity::Timestep timestep)
{
    if (!m_Play)
    {
        return;
    }

    if (!m_Paused)
    {
        m_Play->Update(timestep);
    }
    else if (std::exchange(m_StepRequested, false))
    {
        m_Play->Step();
    }

    if (m_Selection && !GetScene().FindEntityByUUID(m_Selection))
    {
        m_Selection = {};
    }
}

// The copy is the edited scene saved to text and loaded into a scene of its own, as a game loads it, so it keeps every UUID and the selection carries over. A scene the serializer cannot read back is not played
void EditorSession::StartPlay()
{
    if (m_Play || !m_Project)
    {
        return;
    }

    if (m_BeforePlay)
    {
        m_BeforePlay();
    }

    const auto l_Start = std::chrono::steady_clock::now();
    Trinity::Scope<Trinity::Scene> l_Copy = Trinity::CreateScope<Trinity::Scene>();
    const std::string l_SceneText = Trinity::SceneSerializer::SaveToText(m_Scene);
    if (const Trinity::Expected<void, std::string> l_Copied = Trinity::SceneSerializer::LoadFromText(*l_Copy, l_SceneText); !l_Copied)
    {
        TR_ERROR("Forge: {} could not be copied to play: {}", GetSceneName(), l_Copied.GetError());

        return;
    }

    const double l_Milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - l_Start).count();
    // As Unity backs up the open scene as Play starts, since a crash while playing is the likeliest way to lose what was not saved. The text is the copy's own, so it costs only the file
    if (IsDirty())
    {
        WriteBackup(l_SceneText);
    }

    m_History.EndMerge();
    m_History.SetLocked(true);
    m_Paused = false;
    m_StepRequested = false;
    const std::size_t l_Entities = l_Copy->GetEntityCount();
    m_Play = Trinity::CreateScope<Trinity::SceneManager>(m_Project->GetPhysicsSettings(), m_Registry.get(), m_Project->GetInputActions());
    m_Play->Run(std::move(l_Copy), GetSceneID());
    TR_INFO("Forge: playing {}, {} entities copied in {:.1f} ms", GetSceneName(), l_Entities, l_Milliseconds);
}

// An IDE sets its breakpoints as it attaches, and one that debugs native and managed code at once attaches each in turn, so with only one seen the scene plays a moment later, and with both at once
void EditorSession::UpdateDebuggerWait()
{
    if (!m_DebuggerWait)
    {
        return;
    }

    const bool l_Native = Trinity::Process::IsDebuggerAttached();
    const bool l_Managed = Trinity::ManagedScripts::IsDebuggerAttached();
    if (!l_Native && !l_Managed)
    {
        m_DebuggerWait->AttachedAt.reset();

        return;
    }

    const std::chrono::steady_clock::time_point l_Now = std::chrono::steady_clock::now();
    if (!m_DebuggerWait->AttachedAt)
    {
        m_DebuggerWait->AttachedAt = l_Now;
    }

    if ((l_Native && l_Managed) || l_Now - *m_DebuggerWait->AttachedAt >= c_DebuggerSettle)
    {
        m_DebuggerWait.reset();
        TR_INFO("Forge: a {} debugger is attached, so the scene plays", l_Native && l_Managed ? "native and a managed" : l_Native ? "native" : "managed");
        StartPlay();
    }
}

// The runtime goes before the scene it runs, and whichever scene plays goes, the copy or one a script switched to. The selection is kept when its entity is in the edited scene
void EditorSession::StopPlay()
{
    m_DebuggerWait.reset();
    m_PlayAfterBuild = false;
    if (!m_Play)
    {
        return;
    }

    m_Play.reset();
    m_Paused = false;
    m_StepRequested = false;
    m_History.SetLocked(false);
    if (!m_Scene.FindEntityByUUID(m_Selection))
    {
        m_Selection = {};
    }

    TR_INFO("Forge: stopped playing {}, which is as it was", GetSceneName());
}

// The scene's text under two lines naming the scene it was opened from and when, which the scene's reader takes for comments
void EditorSession::WriteBackup(std::string_view sceneText)
{
    const std::int64_t l_Seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const std::string l_Text = std::format("# Forge's backup of a scene with unsaved changes, deleted once they are saved or discarded\n{}{}\n{}{}\n{}", c_BackupScenePrefix, m_ScenePath, c_BackupWrittenPrefix, l_Seconds, sceneText);
    if (const Trinity::Expected<void, Trinity::FileError> l_Written = Trinity::FileSystem::WriteText(GetBackupPath(), l_Text); !l_Written)
    {
        TR_WARN("Forge: {} could not be backed up before playing: {}", GetSceneName(), Trinity::ToString(l_Written.GetError()));

        return;
    }

    m_HasBackup = true;
}

// Only the backup this session wrote. One a crash left is removed by Discard alone
void EditorSession::RemoveBackup()
{
    if (std::exchange(m_HasBackup, false) && m_Project)
    {
        static_cast<void>(Trinity::FileSystem::RemoveFile(GetBackupPath()));
    }
}

// Nothing when there is no backup, which is how a project that closed as it should is left
std::optional<EditorSession::Recovery> EditorSession::ReadBackup() const
{
    Trinity::Expected<std::string, Trinity::FileError> l_Text = Trinity::FileSystem::ReadText(GetBackupPath());
    if (!l_Text)
    {
        return std::nullopt;
    }

    Recovery l_Recovery;
    std::string_view l_Rest = *l_Text;
    while (l_Rest.starts_with('#'))
    {
        const std::size_t l_End = l_Rest.find('\n');
        std::string_view l_Line = l_Rest.substr(0, l_End);
        if (l_Line.ends_with('\r'))
        {
            l_Line.remove_suffix(1);
        }

        std::int64_t l_Seconds = 0;
        if (l_Line.starts_with(c_BackupScenePrefix))
        {
            l_Recovery.ScenePath = std::string(l_Line.substr(c_BackupScenePrefix.size()));
        }
        else if (l_Line.starts_with(c_BackupWrittenPrefix) && std::from_chars(l_Line.data() + c_BackupWrittenPrefix.size(), l_Line.data() + l_Line.size(), l_Seconds).ec == std::errc())
        {
            l_Recovery.Written = std::chrono::system_clock::time_point(std::chrono::seconds(l_Seconds));
        }

        if (l_End == std::string_view::npos)
        {
            break;
        }

        l_Rest.remove_prefix(l_End + 1);
    }

    l_Recovery.Text = std::move(*l_Text);

    return l_Recovery;
}

// The backup's scene as the open scene, unsaved, with the path it was opened from while that is still a scene, so Save writes it there. Its file stays until the scene is saved or discarded, so a second crash still leaves it
void EditorSession::RestoreBackup()
{
    std::optional<Recovery> l_Recovery = std::exchange(m_Recovery, std::nullopt);
    if (!l_Recovery || !m_Project)
    {
        return;
    }

    StopPlay();
    PrefabTemplateCache l_Prefabs = std::exchange(m_Prefabs, {});
    if (const Trinity::Expected<void, std::string> l_Loaded = Trinity::SceneSerializer::LoadFromText(m_Scene, l_Recovery->Text); !l_Loaded)
    {
        m_Prefabs = std::move(l_Prefabs);
        TR_ERROR("Forge: the scene backup could not be restored, so it is left in {}: {}", GetBackupPath(), l_Loaded.GetError());

        return;
    }

    const bool l_HasScene = !l_Recovery->ScenePath.empty() && m_Registry && m_Registry->FindByPath(l_Recovery->ScenePath) != nullptr;
    m_ScenePath = l_HasScene ? l_Recovery->ScenePath : std::string();
    m_Selection = {};
    m_History.Clear();
    m_History.MarkUnsaved();
    m_HasBackup = true;
    TR_INFO("Forge: restored {} from its backup, with the changes that were not saved. Saving keeps them", GetSceneName());
    ReportMissingReferences();
}

// Named New Folder, or New Folder 1 and on when that is taken. An empty folder has no assets, so the registry need not scan
std::optional<std::string> EditorSession::CreateFolder(std::string_view parent)
{
    if (!m_Project)
    {
        return std::nullopt;
    }

    const std::string l_Path = GetFreePath(*m_Project, parent, "New Folder", {}, false);
    std::error_code l_Error;
    if (!std::filesystem::create_directory(m_Project->ToNativePath(l_Path), l_Error))
    {
        TR_ERROR("Forge: {} could not be created: {}", l_Path, l_Error ? l_Error.message() : std::string("it already exists"));

        return std::nullopt;
    }

    TR_INFO("Forge: created folder {}", l_Path);

    return l_Path;
}

// Renames, or moves into another folder, or both. A file takes its .meta along, so it keeps its UUID and everything naming it still finds it
bool EditorSession::MoveAsset(std::string_view path, std::string_view folder, std::string_view name)
{
    if (!m_Project || !m_Registry)
    {
        return false;
    }

    const std::string l_Target = std::format("{}/{}", folder, name);
    if (l_Target == path)
    {
        return true;
    }

    if (!IsValidName(name))
    {
        TR_ERROR("Forge: \"{}\" cannot name a file or folder", name);

        return false;
    }

    if (IsSameOrInside(folder, path))
    {
        TR_ERROR("Forge: {} cannot move into itself", path);

        return false;
    }

    const std::filesystem::path l_Source = m_Project->ToNativePath(path);
    const std::filesystem::path l_Destination = m_Project->ToNativePath(l_Target);
    std::error_code l_Error;
    if (std::filesystem::exists(l_Destination, l_Error))
    {
        TR_ERROR("Forge: {} cannot become {}, which already exists", path, l_Target);

        return false;
    }

    std::filesystem::rename(l_Source, l_Destination, l_Error);
    if (l_Error)
    {
        TR_ERROR("Forge: {} could not become {}: {}", path, l_Target, l_Error.message());

        return false;
    }

    const std::string l_Meta = std::string(Trinity::AssetRegistry::c_MetaExtension);
    std::filesystem::path l_SourceMeta = l_Source;
    l_SourceMeta += l_Meta;
    if (!std::filesystem::is_directory(l_Destination) && std::filesystem::exists(l_SourceMeta))
    {
        std::filesystem::path l_DestinationMeta = l_Destination;
        l_DestinationMeta += l_Meta;
        std::filesystem::rename(l_SourceMeta, l_DestinationMeta, l_Error);
        if (l_Error)
        {
            TR_ERROR("Forge: the .meta of {} could not follow it, so it is put back: {}", path, l_Error.message());
            std::filesystem::rename(l_Destination, l_Source, l_Error);

            return false;
        }
    }

    TR_INFO("Forge: {} is now {}", path, l_Target);
    static_cast<void>(ScanAssets());

    return true;
}

// For good, with its .meta and everything cooked from it in Cache, a model's sub-assets included. Assets still in use read as failed, so sprites using a deleted texture draw white. The open scene stays
bool EditorSession::DeleteAsset(std::string_view path)
{
    if (!m_Project || !m_Registry)
    {
        return false;
    }

    if (!m_ScenePath.empty() && IsSameOrInside(m_ScenePath, path))
    {
        TR_ERROR("Forge: {} holds the open scene, so it is not deleted", path);

        return false;
    }

    std::vector<Trinity::UUID> l_Removed;
    for (const Trinity::AssetRecord* it_Record : m_Registry->GetRecords())
    {
        const Trinity::AssetRecord* l_File = it_Record->Parent.IsValid() ? m_Registry->Find(it_Record->Parent) : it_Record;
        if (l_File != nullptr && IsSameOrInside(l_File->Path, path))
        {
            l_Removed.push_back(it_Record->ID);
            RemoveCooked(*it_Record);
        }
    }

    const std::filesystem::path l_Native = m_Project->ToNativePath(path);
    std::filesystem::path l_Meta = l_Native;
    l_Meta += std::string(Trinity::AssetRegistry::c_MetaExtension);
    std::error_code l_Error;
    std::filesystem::remove_all(l_Native, l_Error);
    if (!l_Error)
    {
        std::filesystem::remove(l_Meta, l_Error);
    }

    if (l_Error)
    {
        TR_ERROR("Forge: {} could not be deleted: {}", path, l_Error.message());
    }
    else
    {
        TR_INFO("Forge: deleted {}, which held {} asset(s)", path, l_Removed.size());
    }

    static_cast<void>(ScanAssets());
    for (const Trinity::UUID it_ID : l_Removed)
    {
        Trinity::AssetManager::Reload(it_ID);
        if (it_ID == m_InspectedAsset)
        {
            m_InspectedAsset = {};
        }
    }

    return !l_Error;
}

// A metal-free, half-rough white, which shows light better than glTF's default of metal
std::optional<std::string> EditorSession::CreateMaterial(std::string_view folder)
{
    if (!m_Project || !m_Registry)
    {
        return std::nullopt;
    }

    const std::string l_Path = GetFreePath(*m_Project, folder, "New Material", ".trmat", false);
    Trinity::MaterialData l_Material;
    l_Material.MetallicFactor = 0.0f;
    l_Material.RoughnessFactor = 0.5f;
    if (const Trinity::Expected<void, Trinity::FileError> l_Written = Trinity::FileSystem::WriteText(l_Path, Trinity::WriteMaterialData(l_Material)); !l_Written)
    {
        TR_ERROR("Forge: {} could not be created: {}", l_Path, Trinity::ToString(l_Written.GetError()));

        return std::nullopt;
    }

    TR_INFO("Forge: created material {}", l_Path);
    ScanAssets();

    return l_Path;
}

std::optional<std::string> EditorSession::CreateScript(std::string_view folder, Trinity::ScriptLanguage language, std::string_view className)
{
    if (!m_Project)
    {
        return std::nullopt;
    }

    const bool l_CSharp = language == Trinity::ScriptLanguage::CSharp;
    const std::string_view l_Language = l_CSharp ? "C#" : "C++";
    const std::string_view l_Root = l_CSharp ? c_ScriptsFolder : c_SourceFolder;
    if (!IsSameOrInside(folder, l_Root))
    {
        TR_ERROR("Forge: a {} script goes in {} or a folder in it, so none was made in {}", l_Language, l_Root, folder);

        return std::nullopt;
    }

    if (l_CSharp ? !m_Project->HasAssembly() : !m_Project->HasModule())
    {
        TR_ERROR("Forge: {} has no {} yet, so no {} script was made. Scripts > {} makes one", m_Project->GetName(), l_CSharp ? "C# project" : "C++ module", l_Language, l_CSharp ? "Create C# Project" : "Create C++ Module");

        return std::nullopt;
    }

    if (!IsClassName(className))
    {
        TR_ERROR("Forge: \"{}\" cannot name a class, which takes letters, digits and _ and does not start with a digit", className);

        return std::nullopt;
    }

    const std::string l_Path = std::format("{}/{}{}", folder, className, l_CSharp ? ".cs" : ".cpp");
    if (Trinity::FileSystem::Exists(l_Path))
    {
        TR_ERROR("Forge: {} is already there, so it is kept as it is", l_Path);

        return std::nullopt;
    }

    const std::string& l_Namespace = l_CSharp ? m_Project->GetAssemblyName() : m_Project->GetModuleName();
    if (const Trinity::Expected<void, Trinity::FileError> l_Written = Trinity::FileSystem::WriteText(l_Path, FillScriptTemplate(l_CSharp ? c_CSharpScriptTemplate : c_CppScriptTemplate, l_Namespace, className)); !l_Written)
    {
        TR_ERROR("Forge: {} could not be written: {}", l_Path, Trinity::ToString(l_Written.GetError()));

        return std::nullopt;
    }

    TR_INFO("Forge: created the {} script {}.{} in {}, which builds as its folder settles", l_Language, l_Namespace, className, l_Path);

    return l_Path;
}

std::string EditorSession::GetScriptClassName(Trinity::ScriptLanguage language, std::string_view className) const
{
    if (!m_Project)
    {
        return std::string(className);
    }

    return language == Trinity::ScriptLanguage::CSharp ? std::format("{}.{}", m_Project->GetAssemblyName(), className) : std::format("{}::{}", m_Project->GetModuleName(), className);
}

bool EditorSession::IsClassName(std::string_view name)
{
    const auto a_Letter = [](char character) { return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || character == '_'; };

    return !name.empty() && a_Letter(name.front()) && std::ranges::all_of(name, [&a_Letter](char character) { return a_Letter(character) || (character >= '0' && character <= '9'); });
}

std::optional<std::string> EditorSession::CreateScene(std::string_view folder)
{
    if (!m_Project || !m_Registry)
    {
        return std::nullopt;
    }

    const std::string l_Path = GetFreePath(*m_Project, folder, "New Scene", c_SceneExtension, false);
    Trinity::Scene l_Scene;
    if (const Trinity::Expected<void, std::string> l_Saved = Trinity::SceneSerializer::Save(l_Scene, l_Path); !l_Saved)
    {
        TR_ERROR("Forge: {} could not be created: {}", l_Path, l_Saved.GetError());

        return std::nullopt;
    }

    TR_INFO("Forge: created scene {}", l_Path);
    ScanAssets();

    return l_Path;
}

// Files are copied without their .meta, since the registry would let whichever path sorts first keep the UUID, and "Name 1" sorts before "Name". The first scan gives the copies UUIDs of their own, their originals' settings follow, and only then does the second scan import them, so each cooks once and as its original does
std::optional<std::string> EditorSession::DuplicateAsset(std::string_view path)
{
    if (!m_Project || !m_Registry)
    {
        return std::nullopt;
    }

    const std::filesystem::path l_Source = m_Project->ToNativePath(path);
    std::error_code l_Error;
    const bool l_Folder = std::filesystem::is_directory(l_Source, l_Error);
    const std::string_view l_Name = path.substr(path.find_last_of('/') + 1);
    const std::size_t l_Dot = l_Folder ? std::string_view::npos : l_Name.find_last_of('.');
    const std::string_view l_Stem = l_Dot == std::string_view::npos || l_Dot == 0 ? l_Name : l_Name.substr(0, l_Dot);
    const std::string l_Path = GetFreePath(*m_Project, path.substr(0, path.find_last_of('/')), l_Stem, l_Name.substr(l_Stem.size()), true);
    const std::filesystem::path l_Destination = m_Project->ToNativePath(l_Path);

    if (l_Folder)
    {
        std::filesystem::create_directory(l_Destination, l_Error);
        for (std::filesystem::recursive_directory_iterator it_Entry(l_Source, l_Error), l_End; !l_Error && it_Entry != l_End; it_Entry.increment(l_Error))
        {
            const std::filesystem::path l_Target = l_Destination / it_Entry->path().lexically_relative(l_Source);
            if (it_Entry->is_directory(l_Error))
            {
                std::filesystem::create_directory(l_Target, l_Error);
            }
            else if (!l_Error && it_Entry->path().extension() != std::filesystem::path(Trinity::AssetRegistry::c_MetaExtension))
            {
                std::filesystem::copy_file(it_Entry->path(), l_Target, l_Error);
            }
        }
    }
    else
    {
        std::filesystem::copy_file(l_Source, l_Destination, l_Error);
    }

    if (l_Error)
    {
        TR_ERROR("Forge: {} could not be copied to {}: {}", path, l_Path, l_Error.message());
        std::filesystem::remove_all(l_Destination, l_Error);

        return std::nullopt;
    }

    std::vector<std::pair<std::string, Trinity::AssetSettings>> l_Settings;
    for (const Trinity::AssetRecord* it_Record : m_Registry->GetRecords())
    {
        if (!it_Record->Parent.IsValid() && !it_Record->Settings.empty() && IsSameOrInside(it_Record->Path, path))
        {
            l_Settings.emplace_back(l_Path + it_Record->Path.substr(path.size()), it_Record->Settings);
        }
    }

    static_cast<void>(m_Registry->Scan());
    for (auto& [it_Path, it_RecordSettings] : l_Settings)
    {
        if (const Trinity::AssetRecord* l_Copy = m_Registry->FindByPath(it_Path))
        {
            static_cast<void>(m_Registry->SetSettings(l_Copy->ID, std::move(it_RecordSettings)));
        }
    }

    TR_INFO("Forge: duplicated {} as {}", path, l_Path);
    ScanAssets();

    return l_Path;
}

// As Set as Start Scene does for a scene. Play reads it from its next start
void EditorSession::SetDefaultInputActions(Trinity::UUID asset)
{
    const Trinity::AssetRecord* l_Record = m_Project ? m_Registry->Find(asset) : nullptr;
    if (l_Record == nullptr || l_Record->Importer != Trinity::InputActionsAsset::c_AssetType)
    {
        return;
    }

    m_Project->SetInputActions(asset);
    SaveProject();
    TR_INFO("Forge: {} ({}) is now the default input actions of {}", l_Record->Path, asset, m_Project->GetName());
}

std::optional<std::string> EditorSession::CreateInputActions(std::string_view folder)
{
    if (!m_Project || !m_Registry)
    {
        return std::nullopt;
    }

    const std::string l_Path = GetFreePath(*m_Project, folder, "New Input Actions", Trinity::InputActionsData::c_Extension, false);
    Trinity::InputActionsData l_Data;
    l_Data.Maps.emplace_back().Name = Trinity::InputName(std::string_view("Gameplay"));
    if (const Trinity::Expected<void, Trinity::FileError> l_Written = Trinity::FileSystem::WriteText(l_Path, Trinity::WriteInputActions(l_Data)); !l_Written)
    {
        TR_ERROR("Forge: {} could not be created: {}", l_Path, Trinity::ToString(l_Written.GetError()));

        return std::nullopt;
    }

    TR_INFO("Forge: created input actions {}", l_Path);
    ScanAssets();

    return l_Path;
}

// One asset is kept, the one the panel shows, so moving between two reads each again
const EditorSession::InputActionsFile* EditorSession::ReadInputActions(Trinity::UUID asset)
{
    const Trinity::AssetRecord* l_Record = m_Registry ? m_Registry->Find(asset) : nullptr;
    if (l_Record == nullptr || l_Record->Importer != Trinity::InputActionsAsset::c_AssetType)
    {
        return nullptr;
    }

    InputActionsFile& l_File = m_InputActionsFiles[asset];
    if (l_File.Asset == asset && l_File.ScanCount == m_ScanCount)
    {
        return &l_File;
    }

    l_File = InputActionsFile{ asset, m_ScanCount, std::nullopt,{} };
    const Trinity::Expected<std::string, Trinity::FileError> l_Text = Trinity::FileSystem::ReadText(l_Record->Path);
    if (!l_Text)
    {
        l_File.Error = std::format("{} could not be read: {}", l_Record->Path, Trinity::ToString(l_Text.GetError()));

        return &l_File;
    }

    Trinity::Expected<Trinity::InputActionsData, std::string> l_Data = Trinity::ParseInputActions(*l_Text);
    if (!l_Data)
    {
        l_File.Error = std::format("{} could not be read: {}", l_Record->Path, l_Data.GetError());

        return &l_File;
    }

    l_File.Data = std::move(*l_Data);

    return &l_File;
}

bool EditorSession::SaveInputActions(Trinity::UUID asset, const Trinity::InputActionsData& data)
{
    const Trinity::AssetRecord* l_Record = m_Registry ? m_Registry->Find(asset) : nullptr;
    if (l_Record == nullptr || l_Record->Importer != Trinity::InputActionsAsset::c_AssetType)
    {
        return false;
    }

    const std::string l_Text = Trinity::WriteInputActions(data);
    if (const Trinity::Expected<Trinity::InputActionsData, std::string> l_Again = Trinity::ParseInputActions(l_Text); !l_Again)
    {
        TR_ERROR("Forge: {} was not saved, since it would not read back: {}", l_Record->Path, l_Again.GetError());

        return false;
    }

    if (const Trinity::Expected<void, Trinity::FileError> l_Written = Trinity::FileSystem::WriteText(l_Record->Path, l_Text); !l_Written)
    {
        TR_ERROR("Forge: {} could not be written: {}", l_Record->Path, Trinity::ToString(l_Written.GetError()));

        return false;
    }

    m_InputActionsFiles[asset] = InputActionsFile{ asset, m_ScanCount, data,{} };
    Trinity::AssetManager::Reload(asset);

    return true;
}

bool EditorSession::EditInputActions(Trinity::UUID asset, const Trinity::InputActionsData& before, const Trinity::InputActionsData& after, std::string_view what, std::string field)
{
    const Trinity::AssetRecord* l_Record = m_Registry ? m_Registry->Find(asset) : nullptr;
    if (l_Record == nullptr)
    {
        return false;
    }

    const std::string l_Label = std::format("{} in {}", what, ToUtf8(FromUtf8(l_Record->Path).stem()));

    return m_History.Execute(Trinity::CreateScope<SetInputActionsCommand>([this](Trinity::UUID id, const Trinity::InputActionsData& data) { return SaveInputActions(id, data); }, asset, before, after, std::move(field), l_Label));
}

// Only the default asset's actions are what the fields name, as Play reads that one
bool EditorSession::RenameInputActions(Trinity::UUID asset, const Trinity::InputActionsData& before, const Trinity::InputActionsData& after, std::string_view what)
{
    const Trinity::AssetRecord* l_Record = m_Registry ? m_Registry->Find(asset) : nullptr;
    if (l_Record == nullptr)
    {
        return false;
    }

    std::vector<InputActionFieldRewrite> l_Rewrites = m_Project && m_Project->GetInputActions() == asset ? FindInputActionRewrites(m_Scene, before, after) : std::vector<InputActionFieldRewrite>();
    if (!l_Rewrites.empty() && m_History.IsLocked())
    {
        Notify(std::format("{} would change {} input action field(s) of the scene, which takes no edit while it plays, so nothing was renamed. Stop it first", what, l_Rewrites.size()));

        return false;
    }

    const std::string l_Label = std::format("{} in {}{}", what, ToUtf8(FromUtf8(l_Record->Path).stem()), l_Rewrites.empty() ? std::string() : std::format(", and {} field(s) naming it", l_Rewrites.size()));

    return m_History.Execute(Trinity::CreateScope<SetInputActionsCommand>([this](Trinity::UUID id, const Trinity::InputActionsData& data) { return SaveInputActions(id, data); }, asset, before, after, std::string(), l_Label, std::move(l_Rewrites)));
}

void EditorSession::RequestImportAsset(std::string folder)
{
    m_Pending.push_back([this, l_Folder = std::move(folder)]
    {
        if (m_Project)
        {
            AskPath({ PathKind::OpenFile, "Import New Asset",{},{},{}, [this, l_Folder](const std::filesystem::path& path) { ImportAsset(path, l_Folder); } });
        }
    });
}

// Unity copies too, leaving the file where it was
void EditorSession::ImportAsset(const std::filesystem::path& file, const std::string& folder)
{
    std::error_code l_Error;
    if (!m_Project || !std::filesystem::is_regular_file(file, l_Error))
    {
        TR_ERROR("Forge: {} is not a file, so nothing was imported", ToUtf8(file));

        return;
    }

    if (!std::filesystem::is_directory(m_Project->ToNativePath(folder), l_Error))
    {
        TR_ERROR("Forge: {} is no longer there, so {} was not imported", folder, ToUtf8(file));

        return;
    }

    const std::string l_Path = GetFreePath(*m_Project, folder, ToUtf8(file.stem()), ToUtf8(file.extension()), false);
    std::filesystem::copy_file(file, m_Project->ToNativePath(l_Path), l_Error);
    if (l_Error)
    {
        TR_ERROR("Forge: {} could not be copied to {}: {}", ToUtf8(file), l_Path, l_Error.message());

        return;
    }

    TR_INFO("Forge: imported {} as {}", ToUtf8(file), l_Path);
    ScanAssets();
}

// Refused while imports run, since a batch writes the keys of what it cooks as it ends and would undo the reimport
bool EditorSession::ReimportAssets(std::string_view path)
{
    if (!m_Registry)
    {
        return false;
    }

    if (IsImportingAssets())
    {
        TR_WARN("Forge: {} was not reimported, since assets are importing. Reimport it once they are done", path);

        return false;
    }

    std::size_t l_Files = 0;
    for (const Trinity::AssetRecord* it_Record : m_Registry->GetRecords())
    {
        const Trinity::AssetRecord* l_File = it_Record->Parent.IsValid() ? m_Registry->Find(it_Record->Parent) : it_Record;
        if (l_File != nullptr && IsSameOrInside(l_File->Path, path) && RemoveCacheKey(*it_Record) && !it_Record->Parent.IsValid())
        {
            ++l_Files;
        }
    }

    TR_INFO("Forge: reimporting {} asset(s) in {}", l_Files, path);
    ScanAssets();

    return true;
}

// A project path names one of its mounts, and any other path not rooted is where build output puts it, relative to the project's folder
bool EditorSession::OpenScript(std::string_view path, std::uint32_t line)
{
    if (!m_Project)
    {
        return false;
    }

    const bool l_Mounted = IsSameOrInside(path, Trinity::Project::c_ProjectMount) || IsSameOrInside(path, Trinity::Project::c_AssetsMount) || IsSameOrInside(path, Trinity::Project::c_CacheMount);
    std::filesystem::path l_File = l_Mounted ? m_Project->ToNativePath(path) : FromUtf8(path);
    if (l_File.is_relative())
    {
        l_File = m_Project->GetDirectory() / l_File;
    }

    l_File = l_File.lexically_normal();
    std::error_code l_Error;
    if (!std::filesystem::is_regular_file(l_File, l_Error))
    {
        TR_ERROR("Forge: {} is not there, so it was not opened", ToUtf8(l_File));

        return false;
    }

    const std::filesystem::path l_Solution = m_Project->GetDirectory() / FromUtf8(m_Project->GetAssemblyName() + ".slnx");
    std::filesystem::path l_Workspace = m_Project->HasModule() ? m_Project->GetSourceDirectory() : m_Project->GetDirectory();
    if (l_File.extension() == ".cs")
    {
        l_Workspace = m_Project->HasAssembly() && std::filesystem::is_regular_file(l_Solution, l_Error) ? l_Solution : m_Project->GetScriptsDirectory();
    }

    return IdeLauncher::OpenFile(m_Preferences.Editor, l_File, line, l_Workspace);
}

bool EditorSession::EditScript(Trinity::ScriptLanguage language, std::string_view className)
{
    if (!m_Project)
    {
        return false;
    }

    constexpr std::array<std::string_view, 1> c_CSharpExtensions{ ".cs" };
    constexpr std::array<std::string_view, 3> c_CppExtensions{ ".hpp", ".h", ".cpp" };

    const bool l_CSharp = language == Trinity::ScriptLanguage::CSharp;
    const std::size_t l_Separator = className.find_last_of(".:");
    const std::string_view l_Name = l_Separator == std::string_view::npos ? className : className.substr(l_Separator + 1);
    const std::filesystem::path l_Folder = l_CSharp ? m_Project->GetScriptsDirectory() : m_Project->GetSourceDirectory();
    const std::optional<std::pair<std::filesystem::path, std::uint32_t>> l_Source = FindScriptSource(l_Folder, l_CSharp ? std::span<const std::string_view>(c_CSharpExtensions) : std::span<const std::string_view>(c_CppExtensions), l_Name);
    if (!l_Source)
    {
        TR_ERROR("Forge: no file in {} is named {} or declares it, so {} was not opened", ToUtf8(l_Folder), l_Name, className);

        return false;
    }

    return OpenScript(ToUtf8(l_Source->first), l_Source->second);
}

bool EditorSession::OpenSolution()
{
    if (!m_Project || !m_Project->HasAssembly())
    {
        return false;
    }

    const std::filesystem::path l_Solution = m_Project->GetDirectory() / FromUtf8(m_Project->GetAssemblyName() + ".slnx");
    std::error_code l_Error;
    if (!std::filesystem::is_regular_file(l_Solution, l_Error))
    {
        TR_ERROR("Forge: {} is not there, so the C# scripts were not opened", ToUtf8(l_Solution));

        return false;
    }

    return IdeLauncher::OpenWorkspace(m_Preferences.Editor, l_Solution);
}

bool EditorSession::OpenModuleFolder()
{
    if (!m_Project || !m_Project->HasModule())
    {
        return false;
    }

    return IdeLauncher::OpenWorkspace(m_Preferences.Editor, m_Project->GetSourceDirectory());
}

// Rider and Visual Studio debug with the solution open, and VS Code opens the folder of whichever it is, where Forge wrote its launch.json
bool EditorSession::AttachDebugger()
{
    if (!m_Project)
    {
        return false;
    }

    const std::filesystem::path l_Solution = m_Project->GetDirectory() / FromUtf8(m_Project->GetAssemblyName() + ".slnx");
    std::error_code l_Error;
    std::filesystem::path l_Workspace = m_Project->HasModule() ? m_Project->GetSourceDirectory() : m_Project->GetDirectory();
    if (m_Project->HasAssembly() && std::filesystem::is_regular_file(l_Solution, l_Error))
    {
        l_Workspace = l_Solution;
    }

    return IdeLauncher::AttachDebugger(m_Preferences.Editor, Trinity::Process::GetCurrentID(), l_Workspace);
}

std::optional<std::string> EditorSession::GetMaterialKey(Trinity::UUID material) const
{
    const Trinity::AssetRecord* l_Record = m_Registry ? m_Registry->Find(material) : nullptr;
    const Trinity::AssetRecord* l_Model = l_Record != nullptr && l_Record->Parent.IsValid() ? m_Registry->Find(l_Record->Parent) : nullptr;
    if (l_Model == nullptr)
    {
        return std::nullopt;
    }

    const auto a_SubAsset = std::ranges::find(l_Model->SubAssets, material, &Trinity::SubAsset::ID);

    return a_SubAsset != l_Model->SubAssets.end() ? std::optional(a_SubAsset->Key) : std::nullopt;
}

std::optional<Trinity::MaterialData> EditorSession::ReadMaterial(Trinity::UUID material) const
{
    const Trinity::AssetRecord* l_Record = m_Registry ? m_Registry->Find(material) : nullptr;
    if (l_Record == nullptr || l_Record->Importer != ModelImporter::c_MaterialImporter)
    {
        return std::nullopt;
    }

    const Trinity::Expected<std::string, Trinity::FileError> l_Text = Trinity::FileSystem::ReadText(l_Record->Parent.IsValid() ? Trinity::GetCookedMaterialPath(material) : l_Record->Path);
    const Trinity::Expected<Trinity::MaterialData, std::string> l_Material = l_Text ? Trinity::ParseMaterialData(*l_Text) : Trinity::Expected<Trinity::MaterialData, std::string>(Trinity::Unexpected{ std::string() });

    return l_Material ? std::optional(*l_Material) : std::nullopt;
}

std::optional<Trinity::MaterialData> EditorSession::ReadImportedMaterial(Trinity::UUID material) const
{
    if (!GetMaterialKey(material))
    {
        return std::nullopt;
    }

    const Trinity::Expected<std::string, Trinity::FileError> l_Text = Trinity::FileSystem::ReadText(ModelImporter::GetImportedMaterialPath(material));
    const Trinity::Expected<Trinity::MaterialData, std::string> l_Material = l_Text ? Trinity::ParseMaterialData(*l_Text) : Trinity::Expected<Trinity::MaterialData, std::string>(Trinity::Unexpected{ std::string() });

    return l_Material ? std::optional(*l_Material) : std::nullopt;
}

// The .meta first, so the cooked file never holds what the .meta would not give again
bool EditorSession::WriteMaterial(Trinity::UUID material, const Trinity::MaterialData& data)
{
    const Trinity::AssetRecord* l_Record = m_Registry ? m_Registry->Find(material) : nullptr;
    if (l_Record == nullptr || l_Record->Importer != ModelImporter::c_MaterialImporter)
    {
        return false;
    }

    std::string l_Path = l_Record->Path;
    if (l_Record->Parent.IsValid())
    {
        const std::optional<std::string> l_Key = GetMaterialKey(material);
        const std::optional<Trinity::MaterialData> l_Imported = ReadImportedMaterial(material);
        const Trinity::AssetRecord* l_Model = m_Registry->Find(l_Record->Parent);
        if (!l_Key || !l_Imported || l_Model == nullptr)
        {
            TR_ERROR("Forge: {} has no imported version to compare an edit with, so it can be edited once its model is imported again", l_Record->Path);

            return false;
        }

        if (!m_Registry->SetSettings(l_Model->ID, ModelImporter::MakeMaterialOverrides(*l_Model, *l_Key, *l_Imported, data)))
        {
            TR_ERROR("Forge: the overrides of {} could not be written to {}'s .meta", *l_Key, l_Model->Path);

            return false;
        }

        l_Path = Trinity::GetCookedMaterialPath(material);
    }

    if (const Trinity::Expected<void, Trinity::FileError> l_Written = Trinity::FileSystem::WriteText(l_Path, Trinity::WriteMaterialData(data)); !l_Written)
    {
        TR_ERROR("Forge: {} could not be written: {}", l_Path, Trinity::ToString(l_Written.GetError()));

        return false;
    }

    static_cast<void>(Trinity::Application::Get().GetRenderer().GetMaterialLoader().Apply(material, data));

    return true;
}

bool EditorSession::EditMaterial(Trinity::UUID material, const Trinity::MaterialData& before, const Trinity::MaterialData& after, std::string field)
{
    const Trinity::AssetRecord* l_Record = m_Registry ? m_Registry->Find(material) : nullptr;
    if (l_Record == nullptr)
    {
        return false;
    }

    const std::string l_Name = GetMaterialKey(material).value_or(ToUtf8(FromUtf8(l_Record->Path).stem()));

    return m_History.Execute(Trinity::CreateScope<SetMaterialCommand>([this](Trinity::UUID id, const Trinity::MaterialData& data) { return WriteMaterial(id, data); }, material, before, after, std::move(field), l_Name));
}

// Into the .meta at once, and then encoded in the background. Sprites keep the old texture until the new one has loaded
bool EditorSession::ApplyImportSettings(Trinity::UUID id, Trinity::AssetSettings settings)
{
    if (!m_Registry || !m_Registry->SetSettings(id, std::move(settings)))
    {
        return false;
    }

    const Trinity::AssetRecord* l_Record = m_Registry->Find(id);
    TR_INFO("Forge: reimporting {} with its new settings", l_Record->Path);
    if (l_Record->Importer == ModelImporter::c_Importer)
    {
        // Its cache key holds its settings, so the model imports again while every other model stays cached
        StartImports();

        return true;
    }

    m_Reimporter.Queue(*l_Record);

    return true;
}

// Its hierarchy under one root named after the file, in one command, so a single undo takes it all away. A model still importing, or one whose import failed, has nothing to create yet
bool EditorSession::CreateModel(Trinity::UUID model, Trinity::UUID parent, Trinity::UUID before, glm::vec3 position)
{
    const Trinity::AssetRecord* l_Record = m_Registry ? m_Registry->Find(model) : nullptr;
    if (l_Record == nullptr || l_Record->Importer != ModelImporter::c_Importer)
    {
        TR_WARN("Forge: only a model dropped into the scene creates its hierarchy");

        return false;
    }

    if (!CanTakeChildren(parent))
    {
        return false;
    }

    return m_History.Execute(Trinity::CreateScope<CreateModelCommand>(model, ToUtf8(FromUtf8(l_Record->Path).stem()), parent, FindAddedPlace(m_Scene, before), position));
}

// Written under the local IDs a new instance UUID tells back from the subtree's UUIDs, so linking the entity changes none of them, and what it names outside the subtree stays the instance's own
std::optional<std::string> EditorSession::CreatePrefab(Trinity::UUID entity, std::string_view folder)
{
    const Trinity::Entity l_Entity = m_Scene.FindEntityByUUID(entity);
    if (!m_Project || !m_Registry || IsPlaying() || !l_Entity)
    {
        return std::nullopt;
    }

    if (!IsSameOrInside(folder, Trinity::Project::c_AssetsMount))
    {
        Notify("Prefabs are made in Assets, as every asset of the project is");

        return std::nullopt;
    }

    if (const std::optional<std::string> l_Problem = FindPrefabProblem(l_Entity))
    {
        Notify(*l_Problem);

        return std::nullopt;
    }

    const Trinity::UUID l_Instance = Trinity::UUID::Generate();
    const Trinity::Expected<std::string, std::string> l_Text = Trinity::SceneSerializer::SavePrefabToText(m_Scene, l_Entity, l_Instance);
    if (!l_Text)
    {
        TR_ERROR("Forge: {} could not be made a prefab: {}", GetEntityLabel(l_Entity), l_Text.GetError());

        return std::nullopt;
    }

    const std::string_view l_Name = l_Entity.Has<Trinity::TagComponent>() ? std::string_view(l_Entity.Get<Trinity::TagComponent>().Tag) : std::string_view();
    const std::string l_Path = GetFreePath(*m_Project, folder, GetPrefabFileName(l_Name), Trinity::Prefab::c_Extension, false);
    if (const Trinity::Expected<void, Trinity::FileError> l_Written = Trinity::FileSystem::WriteText(l_Path, *l_Text); !l_Written)
    {
        TR_ERROR("Forge: {} could not be created: {}", l_Path, Trinity::ToString(l_Written.GetError()));

        return std::nullopt;
    }

    TR_INFO("Forge: created prefab {} from {}", l_Path, GetEntityLabel(l_Entity));
    ScanAssets();
    const Trinity::AssetRecord* l_Record = m_Registry->FindByPath(l_Path);
    if (l_Record == nullptr || !m_History.Execute(Trinity::CreateScope<LinkPrefabCommand>(entity, l_Record->ID, l_Instance)))
    {
        TR_ERROR("Forge: {} was written, but {} could not be made its instance", l_Path, GetEntityLabel(l_Entity));
    }

    return l_Path;
}

bool EditorSession::InstantiatePrefab(Trinity::UUID prefab, Trinity::UUID parent, Trinity::UUID before, std::optional<glm::vec3> position)
{
    const Trinity::AssetRecord* l_Record = m_Registry ? m_Registry->Find(prefab) : nullptr;
    if (l_Record == nullptr || l_Record->Importer != Trinity::PrefabAsset::c_AssetType)
    {
        TR_WARN("Forge: only a prefab dropped into the scene makes an instance of one");

        return false;
    }

    if (!CanTakeChildren(parent))
    {
        return false;
    }

    return m_History.Execute(Trinity::CreateScope<InstantiatePrefabCommand>(prefab, ToUtf8(FromUtf8(l_Record->Path).stem()), parent, FindAddedPlace(m_Scene, before), position));
}

bool EditorSession::RevertPrefabOverrides(Trinity::UUID root, std::vector<Trinity::SceneSerializer::PrefabOverride> overrides, std::string_view what, Trinity::UUID subject)
{
    if (overrides.empty())
    {
        return false;
    }

    return m_History.Execute(Trinity::CreateScope<RevertPrefabCommand>(root, std::move(overrides), subject, std::format("Revert {}", what)));
}

bool EditorSession::ApplyPrefabOverrides(Trinity::UUID root, std::vector<Trinity::SceneSerializer::PrefabOverride> overrides, std::string_view what, Trinity::UUID subject)
{
    UpdateChangedPrefabs();
    const Trinity::Entity l_Root = m_Scene.FindEntityByUUID(root);
    if (overrides.empty() || !l_Root || !l_Root.Has<Trinity::PrefabInstanceComponent>())
    {
        return false;
    }

    const Trinity::UUID l_Prefab = l_Root.Get<Trinity::PrefabInstanceComponent>().Prefab;
    const Trinity::AssetRecord* l_Record = m_Registry ? m_Registry->Find(l_Prefab) : nullptr;
    const std::string* l_Text = m_Prefabs.FindText(l_Prefab);
    if (l_Record == nullptr || l_Text == nullptr)
    {
        Notify(std::format("The prefab of {} is no longer in the project, so nothing can be applied to it", GetEntityLabel(l_Root)));

        return false;
    }

    for (const Trinity::SceneSerializer::PrefabOverride& it_Override : overrides)
    {
        const Trinity::Entity l_Added = it_Override.Type == Trinity::SceneSerializer::PrefabOverride::Kind::AddedEntity ? m_Scene.FindEntityByUUID(it_Override.Entity) : Trinity::Entity();
        for (Trinity::Entity it_Entity = l_Added; it_Entity; it_Entity = m_Scene.GetNextInSubtree(it_Entity, l_Added))
        {
            if (it_Entity.Has<Trinity::PrefabInstanceComponent>())
            {
                Notify(std::format("{} {} a prefab instance, and a prefab holding another waits for nested prefabs, so nothing was applied", GetEntityLabel(l_Added), it_Entity == l_Added ? "is" : "holds"));

                return false;
            }
        }
    }

    const std::string l_Label = std::format("Apply {} to prefab {}", what, ToUtf8(FromUtf8(l_Record->Path).stem()));

    return m_History.Execute(Trinity::CreateScope<ApplyPrefabCommand>([this](Trinity::UUID prefab, const std::string& text) { return WritePrefab(prefab, text); }, root, std::move(overrides), *l_Text, subject, l_Label));
}

// The file first, so a prefab that cannot be written keeps its template, and then the template the open scene's instances are made from, from the same text
bool EditorSession::WritePrefab(Trinity::UUID prefab, const std::string& text)
{
    const Trinity::AssetRecord* l_Record = m_Registry ? m_Registry->Find(prefab) : nullptr;
    if (l_Record == nullptr)
    {
        TR_ERROR("Forge: prefab {} is no longer in the project, so it was not written", prefab);

        return false;
    }

    if (const Trinity::Expected<void, Trinity::FileError> l_Written = Trinity::FileSystem::WriteText(l_Record->Path, text); !l_Written)
    {
        TR_ERROR("Forge: {} could not be written: {}", l_Record->Path, Trinity::ToString(l_Written.GetError()));

        return false;
    }

    if (const Trinity::Expected<void, std::string> l_Replaced = m_Prefabs.Replace(prefab, text); !l_Replaced)
    {
        TR_ERROR("Forge: {} was written, but does not read back, so its instances keep the prefab they were made from: {}", l_Record->Path, l_Replaced.GetError());

        return false;
    }

    Trinity::AssetManager::Reload(prefab);

    return true;
}

// What the Hierarchy moves among an instance's entities goes after the prefab's own children there, so it stays where a reload puts it
bool EditorSession::MoveEntity(Trinity::UUID entity, Trinity::UUID parent, Trinity::UUID before)
{
    if (const std::optional<std::string> l_Problem = FindMoveProblem(m_Scene, entity, parent))
    {
        Notify(*l_Problem);

        return false;
    }

    return m_History.Execute(Trinity::CreateScope<MoveEntityCommand>(entity, parent, FindAddedPlace(m_Scene, before), true));
}

bool EditorSession::DuplicateEntity(Trinity::UUID entity)
{
    const Trinity::Entity l_Entity = m_Scene.FindEntityByUUID(entity);
    if (!l_Entity)
    {
        return false;
    }

    if (const std::optional<std::string> l_Problem = FindCopyProblem(l_Entity))
    {
        Notify(*l_Problem);

        return false;
    }

    return m_History.Execute(Trinity::CreateScope<DuplicateEntityCommand>(entity));
}

// A notice says why not
bool EditorSession::CanTakeChildren(Trinity::UUID parent)
{
    const std::optional<std::string> l_Problem = FindChildProblem(m_Scene, parent);
    if (l_Problem)
    {
        Notify(*l_Problem);
    }

    return !l_Problem;
}

bool EditorSession::CreatePrimitive(Trinity::BuiltInMesh mesh, Trinity::UUID parent)
{
    return CanTakeChildren(parent) && m_History.Execute(Trinity::CreateScope<CreatePrimitiveCommand>(mesh, parent, parent ? glm::vec3(0.0f) : m_SpawnPoint));
}

bool EditorSession::AddScript(Trinity::UUID entity, std::string_view path)
{
    if (const Trinity::Entity l_Entity = m_Scene.FindEntityByUUID(entity); l_Entity && IsMissingPrefab(l_Entity))
    {
        Notify(std::format("{} stands in for a prefab instance whose prefab is missing, which keeps the instance as the scene file has it, so it takes no script until the prefab is back", GetEntityLabel(l_Entity)));

        return false;
    }

    const std::filesystem::path l_File = FromUtf8(path);
    const Trinity::ScriptLanguage l_Language = l_File.extension() == ".cs" ? Trinity::ScriptLanguage::CSharp : Trinity::ScriptLanguage::Cpp;
    const std::string l_Name = ToUtf8(l_File.stem());
    const std::vector<const Trinity::ScriptClassDescription*> l_Classes = Trinity::ScriptClassRegistry::GetClasses();
    const auto a_Class = std::ranges::find_if(l_Classes, [l_Language, &l_Name](const Trinity::ScriptClassDescription* description)
    {
        const std::string_view l_ClassName(description->Name);
        const std::size_t l_Separator = l_ClassName.find_last_of(".:");

        return description->Language == l_Language && (l_Separator == std::string_view::npos ? l_ClassName : l_ClassName.substr(l_Separator + 1)) == l_Name;
    });

    if (a_Class == l_Classes.end())
    {
        TR_WARN("Forge: {} cannot be added, since no loaded code has a {} class named {}. A script's class is named after its file, and is there once its build loads it", path, GetScriptLanguageLabel(l_Language), l_Name);

        return false;
    }

    return m_History.Execute(Trinity::CreateScope<AddScriptCommand>(entity, Trinity::ScriptClassRegistry::CreateEntry(**a_Class)));
}

bool EditorSession::CreateEmpty(Trinity::UUID parent)
{
    return CanTakeChildren(parent) && m_History.Execute(Trinity::CreateScope<CreateEntityCommand>("Entity", parent, Trinity::UUID(), parent ? glm::vec3(0.0f) : m_SpawnPoint));
}

bool EditorSession::CreateLight(Trinity::LightType type, Trinity::UUID parent)
{
    return CanTakeChildren(parent) && m_History.Execute(Trinity::CreateScope<CreateLightCommand>(type, parent, parent ? glm::vec3(0.0f) : m_SpawnPoint));
}

bool EditorSession::CreateCamera(Trinity::UUID parent)
{
    return CanTakeChildren(parent) && m_History.Execute(Trinity::CreateScope<CreateCameraCommand>(m_SceneView3D ? Trinity::CameraProjection::Perspective : Trinity::CameraProjection::Orthographic, parent, parent ? glm::vec3(0.0f) : m_SpawnPoint));
}

void EditorSession::CopyEntity(Trinity::UUID entity)
{
    Trinity::Scene& l_Scene = GetScene();
    const Trinity::Entity l_Entity = l_Scene.FindEntityByUUID(entity);
    if (!l_Entity)
    {
        return;
    }

    if (const std::optional<std::string> l_Problem = FindCopyProblem(l_Entity))
    {
        Notify(*l_Problem);

        return;
    }

    const Trinity::Entity l_Parent = l_Entity.GetParent();
    m_CopiedEntity = CopiedEntity{ .Text = Trinity::SceneSerializer::SaveEntityToText(l_Scene, l_Entity), .Original = entity, .Parent = l_Parent ? l_Parent.GetUUID() : Trinity::UUID(), .World = l_Scene.ComputeWorldTransform(l_Entity) };
}

bool EditorSession::PasteEntity()
{
    if (!m_CopiedEntity)
    {
        return false;
    }

    if (const Trinity::Entity l_Original = m_Scene.FindEntityByUUID(m_CopiedEntity->Original))
    {
        const Trinity::Entity l_Parent = l_Original.GetParent();
        const Trinity::Entity l_Next = l_Original.GetNextSibling();

        return PasteEntityAt(l_Parent ? l_Parent.GetUUID() : Trinity::UUID(), l_Next ? l_Next.GetUUID() : Trinity::UUID());
    }

    return PasteEntityAt(m_Scene.FindEntityByUUID(m_CopiedEntity->Parent) ? m_CopiedEntity->Parent : Trinity::UUID(), {});
}

bool EditorSession::PasteEntityAsChild(Trinity::UUID parent)
{
    return m_CopiedEntity && m_Scene.FindEntityByUUID(parent) && PasteEntityAt(parent, {});
}

// Under the parent it was copied from, it keeps its transform, as Duplicate does, and under another it keeps its place in the world, as moving it there in the Hierarchy does. Among an instance's entities it goes after the prefab's own children there. An instance copied while its prefab was there would be pasted as plain entities once the prefab is gone, so it waits for the prefab
bool EditorSession::PasteEntityAt(Trinity::UUID parent, Trinity::UUID before)
{
    if (IsPlaying() || !CanTakeChildren(parent))
    {
        return false;
    }

    const std::vector<Trinity::UUID> l_Prefabs = Trinity::SceneSerializer::GetPrefabReferences(m_CopiedEntity->Text);
    if (std::ranges::any_of(l_Prefabs, [this](Trinity::UUID prefab) { return m_Prefabs.Find(prefab, m_Registry.get()) == nullptr; }))
    {
        Notify("What was copied holds a prefab instance whose prefab is missing or cannot be read, so it is not pasted until the prefab is back");

        return false;
    }

    const std::optional<Trinity::TransformComponent> l_World = parent == m_CopiedEntity->Parent ? std::nullopt : std::optional(m_CopiedEntity->World);

    return m_History.Execute(Trinity::CreateScope<PasteEntityCommand>(m_CopiedEntity->Text, parent, FindAddedPlace(m_Scene, before), l_World));
}

bool EditorSession::EditPhysicsSettings(const Trinity::PhysicsSettings& settings, std::string field)
{
    if (!m_Project)
    {
        return false;
    }

    return m_History.Execute(Trinity::CreateScope<SetPhysicsSettingsCommand>([this](const Trinity::PhysicsSettings& written) { return WritePhysicsSettings(written); }, m_Project->GetPhysicsSettings(), settings, std::move(field)));
}

// A command kept after its project closed has no project to write to
bool EditorSession::WritePhysicsSettings(const Trinity::PhysicsSettings& settings)
{
    if (!m_Project)
    {
        return false;
    }

    m_Project->SetPhysicsSettings(settings);
    SaveProject();

    return true;
}

void EditorSession::SaveProject()
{
    const Trinity::Expected<void, std::string> l_Saved = m_Project->Save();
    if (!l_Saved)
    {
        TR_ERROR("Forge: {}", l_Saved.GetError());
    }
}

void EditorSession::NewScene()
{
    m_Scene.Clear();
    m_Prefabs.Clear();
    m_ScenePath.clear();
    m_Selection = {};
    m_History.Clear();
}

// On an error the scene is left as it was, with the prefabs it was made from. Otherwise each prefab is read again as the new scene asks for it, so one changed or deleted since the last scene opened is as its file is now
void EditorSession::OpenScene(const std::string& assetPath)
{
    PrefabTemplateCache l_Prefabs = std::exchange(m_Prefabs, {});
    const Trinity::Expected<void, std::string> l_Loaded = Trinity::SceneSerializer::Load(m_Scene, assetPath);
    if (!l_Loaded)
    {
        m_Prefabs = std::move(l_Prefabs);
        TR_ERROR("Forge: the scene could not be opened: {}", l_Loaded.GetError());

        return;
    }

    m_ScenePath = assetPath;
    m_Selection = {};
    m_History.Clear();
    RememberScene();
    TR_INFO("Forge: opened scene {} with {} entities", m_ScenePath, m_Scene.GetEntityCount());
    ReportMissingReferences();
}

// One warning listing every slot of the open scene that names an asset the project no longer has, or an entity the scene no longer has, as Properties shows each as Missing. The Console shows its first line, and the rest under the list once it is selected
void EditorSession::ReportMissingReferences()
{
    if (!m_Registry)
    {
        return;
    }

    const std::vector<std::string> l_Missing = FindMissingReferences(m_Scene, *m_Registry);
    if (l_Missing.empty())
    {
        return;
    }

    std::string l_Text = std::format("Forge: {} has {} missing reference{}, to assets deleted or moved outside Forge, or to entities no longer in the scene. Properties shows each as Missing", GetSceneName(), l_Missing.size(), l_Missing.size() == 1 ? "" : "s");
    for (std::size_t it_Index = 0; it_Index < std::min(l_Missing.size(), c_MissingReferenceLimit); ++it_Index)
    {
        l_Text += "\n    ";
        l_Text += l_Missing[it_Index];
    }

    if (l_Missing.size() > c_MissingReferenceLimit)
    {
        l_Text += std::format("\n    and {} more", l_Missing.size() - c_MissingReferenceLimit);
    }

    TR_WARN("{}", l_Text);
}

void EditorSession::SaveScene(Action then)
{
    if (m_ScenePath.empty())
    {
        SaveSceneAs(std::move(then));

        return;
    }

    if (WriteScene(m_ScenePath) && then)
    {
        then();
    }
}

// Inside the project's Assets folder only, since everything in a project is found through /assets
void EditorSession::SaveSceneAs(Action then)
{
    const std::string l_FileName = m_ScenePath.empty() ? std::format("Untitled{}", c_SceneExtension) : ToUtf8(FromUtf8(m_ScenePath).filename());
    AskPath({ PathKind::SaveFile, "Save Scene As", c_SceneFilters, m_Project->GetAssetsDirectory() / "Scenes", l_FileName, [this, l_Then = std::move(then)](const std::filesystem::path& path) mutable
    {
        std::filesystem::path l_Path = path;
        if (l_Path.extension() != c_SceneExtension)
        {
            l_Path += c_SceneExtension;
        }

        const std::optional<std::string> l_AssetPath = m_Project->ToAssetPath(l_Path);
        if (!l_AssetPath)
        {
            TR_ERROR("Forge: scenes are saved inside the project's Assets folder, and {} is not", ToUtf8(l_Path));

            return;
        }

        if (WriteScene(*l_AssetPath))
        {
            m_ScenePath = *l_AssetPath;
            RememberScene();
            if (l_Then)
            {
                l_Then();
            }
        }
    } });
}

bool EditorSession::WriteScene(const std::string& assetPath)
{
    const Trinity::Expected<void, std::string> l_Saved = Trinity::SceneSerializer::Save(m_Scene, assetPath);
    if (!l_Saved)
    {
        TR_ERROR("Forge: the scene could not be saved: {}", l_Saved.GetError());

        return false;
    }

    m_History.MarkSaved();
    TR_INFO("Forge: saved scene {} with {} entities", assetPath, m_Scene.GetEntityCount());

    // A scene saved under a new name gets its .meta and UUID now
    if (m_Registry && m_Registry->FindByPath(assetPath) == nullptr)
    {
        static_cast<void>(m_Registry->Scan());
    }

    return true;
}

// The scene's asset UUID, which an untitled scene has none of
Trinity::UUID EditorSession::GetSceneID() const
{
    const Trinity::AssetRecord* l_Record = m_Registry && !m_ScenePath.empty() ? m_Registry->FindByPath(m_ScenePath) : nullptr;

    return l_Record != nullptr ? l_Record->ID : Trinity::UUID();
}

std::string EditorSession::GetSceneName() const
{
    return m_ScenePath.empty() ? std::string("Untitled") : ToUtf8(FromUtf8(m_ScenePath).stem());
}

// Save, then go on. Don't Save goes on at once. Cancel, Escape or closing the popup stays
void EditorSession::DrawUnsavedPopup()
{
    if (std::exchange(m_OpenUnsavedPopup, false))
    {
        ImGui::OpenPopup(c_UnsavedPopup);
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    bool l_Open = true;
    if (!ImGui::BeginPopupModal(c_UnsavedPopup, &l_Open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        if (!l_Open)
        {
            m_AfterDiscard = {};
        }

        return;
    }

    const std::string l_Message = std::format("{} {} has unsaved changes. Save them first?", Trinity::Icons::c_Warning, GetSceneName());
    ImGui::TextUnformatted(l_Message.c_str());
    ImGui::Spacing();

    if (ImGui::Button("Save"))
    {
        m_Pending.push_back([this, l_Then = std::move(m_AfterDiscard)]() mutable { SaveScene(std::move(l_Then)); });
        ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("Don't Save"))
    {
        m_Pending.push_back(std::move(m_AfterDiscard));
        ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        m_AfterDiscard = {};
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

// Stands in for the native dialog where the platform has none
void EditorSession::DrawPathPopup()
{
    if (std::exchange(m_OpenPathPopup, false))
    {
        ImGui::OpenPopup(c_PathPopup);
    }

    if (!m_PathPrompt)
    {
        return;
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    bool l_Open = true;
    if (!ImGui::BeginPopupModal(c_PathPopup, &l_Open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        if (!l_Open)
        {
            m_PathPrompt.reset();
        }

        return;
    }

    ImGui::TextUnformatted(m_PathPrompt->Title.c_str());
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 40.0f);
    if (ImGui::IsWindowAppearing())
    {
        ImGui::SetKeyboardFocusHere();
    }

    const bool l_Entered = ImGui::InputText("##Path", &m_PathText, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::Spacing();

    if ((ImGui::Button("OK") || l_Entered) && !m_PathText.empty())
    {
        m_Pending.push_back([l_Action = std::move(m_PathPrompt->OnChosen), l_Path = FromUtf8(m_PathText)]() mutable { l_Action(l_Path); });
        m_PathPrompt.reset();
        ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
        m_PathPrompt.reset();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

// Over everything while the batch runs, so nothing edits or opens a texture still being imported. The import itself runs in the background, so Forge keeps drawing. It waits for any other popup to close, since opening it would close that one, and comes back once a popup that took its place has closed. Cancel lets the texture being encoded finish
void EditorSession::DrawImportPopup()
{
    const bool l_Running = m_Imports.IsRunning();
    const TextureImportBatch::Progress l_Progress = m_Imports.GetProgress();
    if (!l_Running || (m_ImportPopupOpen && !ImGui::IsPopupOpen(c_ImportPopup)))
    {
        m_ImportPopupOpen = false;
    }

    if (l_Running && !m_ImportPopupOpen && l_Progress.ElapsedSeconds >= c_ImportPopupDelay && (!l_Progress.Planned || l_Progress.ToEncode != 0) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
    {
        ImGui::OpenPopup(c_ImportPopup);
        m_ImportPopupOpen = true;
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(c_ImportPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        return;
    }

    if (!l_Running)
    {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();

        return;
    }

    std::string l_Status;
    if (!l_Progress.Planned)
    {
        l_Status = std::format("Checking {} texture(s)...", l_Progress.Textures);
    }
    else if (l_Progress.Encoding != 0)
    {
        const std::string_view l_Path = l_Progress.Path;
        l_Status = std::format("Encoding {} of {}: {} ({}x{})", l_Progress.Encoding, l_Progress.ToEncode, l_Path.substr(l_Path.find_last_of('/') + 1), l_Progress.Width, l_Progress.Height);
    }
    else
    {
        l_Status = "Finishing...";
    }

    ImGui::TextUnformatted(l_Status.c_str());

    // Before the encoder has kept a rate, the bar only shows that work goes on
    const std::optional<double> l_Rate = l_Progress.TexelsPerSecond;
    const double l_Total = static_cast<double>(l_Progress.TotalTexels);
    const std::optional<double> l_Done = GetImportedTexels(l_Progress);
    const float l_Fraction = l_Done ? static_cast<float>(std::clamp(*l_Done / l_Total, 0.0, 1.0)) : -static_cast<float>(ImGui::GetTime());
    const std::string l_Percent = l_Done ? std::format("{:.0f}%", l_Fraction * 100.0f) : std::string();
    ImGui::ProgressBar(l_Fraction, ImVec2(ImGui::GetFontSize() * 26.0f, 0.0f), l_Percent.c_str());

    const std::string l_Time = l_Done && l_Rate ? std::format("{} elapsed, about {} left", FormatDuration(l_Progress.ElapsedSeconds), FormatDuration((l_Total - *l_Done) / *l_Rate)) : std::format("{} elapsed, estimating the time left", FormatDuration(l_Progress.ElapsedSeconds));
    ImGui::TextDisabled("%s", l_Time.c_str());
    ImGui::Spacing();

    ImGui::BeginDisabled(l_Progress.Stopping);
    if (ImGui::Button(l_Progress.Stopping ? "Stopping..." : "Cancel") || (!l_Progress.Stopping && ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
    {
        m_Imports.Stop();
    }

    ImGui::EndDisabled();
    if (l_Progress.Stopping)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("The rest wait for the next refresh");
    }

    ImGui::EndPopup();
}

// Over everything while Play waits for a debugger, with what is attached so far. Attach starts the script editor's debugger, Play Now plays without one, and Cancel or Escape plays nothing. It closes by itself once the scene plays
void EditorSession::DrawDebuggerPopup()
{
    if (std::exchange(m_OpenDebuggerPopup, false))
    {
        ImGui::OpenPopup(c_DebuggerPopup);
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(c_DebuggerPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        return;
    }

    if (!m_DebuggerWait)
    {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();

        return;
    }

    const std::string l_ID = std::to_string(Trinity::Process::GetCurrentID());
    const std::string l_Message = std::format("{} The scene plays once a debugger is attached to Forge, process {}", Trinity::Icons::c_Bug, l_ID);
    ImGui::TextUnformatted(l_Message.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy"))
    {
        ImGui::SetClipboardText(l_ID.c_str());
    }

    const auto a_Status = [](std::string_view language, std::string_view status, bool attached)
    {
        const std::string l_Line = std::format("{} {}: {}", attached ? Trinity::Icons::c_CheckCircle : Trinity::Icons::c_Spinner, language, status);
        if (attached)
        {
            ImGui::TextUnformatted(l_Line.c_str());
        }
        else
        {
            ImGui::TextDisabled("%s", l_Line.c_str());
        }
    };

    const bool l_Native = Trinity::Process::IsDebuggerAttached();
    const bool l_Managed = Trinity::ManagedScripts::IsDebuggerAttached();
    ImGui::Spacing();
    a_Status("Native, for C++", l_Native ? "attached" : "not attached", l_Native);
    a_Status("Managed, for C#", AssemblyHost::IsSupported() ? (l_Managed ? "attached" : "not attached") : "C# is off", l_Managed);
    if (m_DebuggerWait->AttachedAt)
    {
        const double l_Left = std::chrono::duration<double>(c_DebuggerSettle - (std::chrono::steady_clock::now() - *m_DebuggerWait->AttachedAt)).count();
        ImGui::TextDisabled("Playing in %.1f s, once the other half of a debugger of both languages has had time to attach", std::max(l_Left, 0.0));
    }

    ImGui::TextDisabled("Visual Studio debugs both languages when it attaches with Native and Managed (.NET Core, .NET 5+) code");
    ImGui::Spacing();

    ImGui::BeginDisabled(m_Preferences.Editor == ScriptEditor::System);
    const std::string l_Attach = std::format("{} Attach {}", Trinity::Icons::c_Bug, IdeLauncher::ToString(m_Preferences.Editor));
    if (ImGui::Button(l_Attach.c_str()))
    {
        static_cast<void>(AttachDebugger());
    }

    ImGui::EndDisabled();
    if (m_Preferences.Editor == ScriptEditor::System)
    {
        ImGui::SetItemTooltip("Pick Visual Studio, Rider or VS Code under Scripts > Script Editor to attach it from here");
    }

    ImGui::SameLine();
    if (ImGui::Button("Play Now"))
    {
        m_DebuggerWait.reset();
        m_Pending.push_back([this] { StartPlay(); });
        ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        m_DebuggerWait.reset();
        TR_INFO("Forge: Play was cancelled while it waited for a debugger");
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

// Forge did not close as it should and left a backup of a scene with unsaved changes. Restore and Discard settle it, and closing the popup leaves the backup to be asked about when the project opens again
void EditorSession::DrawRecoveryPopup()
{
    if (std::exchange(m_OpenRecoveryPopup, false))
    {
        ImGui::OpenPopup(c_RecoveryPopup);
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    bool l_Open = true;
    if (!ImGui::BeginPopupModal(c_RecoveryPopup, &l_Open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        if (!l_Open && m_Recovery)
        {
            m_Recovery.reset();
            TR_INFO("Forge: the scene backup is kept, to be asked about when the project opens again");
        }

        return;
    }

    if (!m_Recovery)
    {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();

        return;
    }

    const std::string l_Scene = m_Recovery->ScenePath.empty() ? std::string("an untitled scene") : m_Recovery->ScenePath;
    const std::string l_When = m_Recovery->Written ? std::format(", made {}", FormatAge(*m_Recovery->Written)) : std::string();
    ImGui::TextUnformatted(std::format("{} Forge did not close as it should last time", Trinity::Icons::c_Warning).c_str());
    ImGui::TextUnformatted(std::format("A backup of {}{} holds changes that were never saved", l_Scene, l_When).c_str());
    ImGui::TextDisabled("Restore opens it with those changes, still to be saved, and Discard deletes the backup");
    ImGui::Spacing();

    if (ImGui::Button("Restore"))
    {
        m_Pending.push_back([this] { RestoreBackup(); });
        ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("Discard"))
    {
        m_Recovery.reset();
        static_cast<void>(Trinity::FileSystem::RemoveFile(GetBackupPath()));
        TR_INFO("Forge: discarded the scene backup");
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}
#pragma once

#include "CommandStack.hpp"
#include "EditorPreferences.hpp"
#include "Importers/EnvironmentImportBatch.hpp"
#include "Importers/EnvironmentImporter.hpp"
#include "Importers/ModelImportBatch.hpp"
#include "Importers/ModelImporter.hpp"
#include "Importers/TextureImportBatch.hpp"
#include "Importers/TextureImporter.hpp"
#include "Importers/TextureReimporter.hpp"
#include "Modules/AssemblyHost.hpp"
#include "Modules/IdeLauncher.hpp"
#include "Modules/ModuleHost.hpp"
#include "PrefabTemplateCache.hpp"

#include "Trinity.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class EditorSession
{
public:
    enum class Command : std::uint8_t
    {
        NewProject,
        OpenProject,
        NewScene,
        OpenScene,
        SaveScene,
        SaveSceneAs,
        SetStartScene,
        Refresh,
        CreateModule,
        BuildModule,
        CreateAssembly,
        BuildAssembly,
        Exit
    };

    // The last report of each kind of import
    struct ImportReport
    {
        TextureImportReport Textures;
        ModelImportReport Models;
        EnvironmentImportReport Environments;
    };

    // A line shown over the editor for a moment, as Unity's notifications are, for something asked for that did not happen
    struct Notice
    {
        std::string Text;
        std::chrono::steady_clock::time_point Shown;
    };

    // Work going on in the background, for the status bar: what it is, and how far along from 0 to 1 when that can be told
    struct Activity
    {
        std::string Text;
        std::optional<float> Progress;
    };

    // What a prefab made from an instance's root is, as Unity asks when one is dragged into the Project window: an original, whose own entities are those the instance's prefab gave it, with the prefabs nested in that one nested in it still, or a variant of the instance's prefab, which keeps what the instance changes of it
    enum class PrefabKind : std::uint8_t
    {
        Original,
        Variant
    };

    // A project opened or created lately: its file, its name then and when, and whether the file was gone as the list was last read, as Forge starts and as a project closes
    struct RecentProject
    {
        std::filesystem::path Path;
        std::string Name;
        std::chrono::system_clock::time_point OpenedAt;
        bool Missing = false;
    };

    // A scene of the project opened or saved lately, by its UUID, so it is followed as its file moves, with the path it had then, for one the project no longer has
    struct RecentScene
    {
        Trinity::UUID ID;
        std::string Path;
    };

    // An input actions asset as its file holds it: what it holds, or why it could not be read, kept until the files are scanned again
    struct InputActionsFile
    {
        Trinity::UUID Asset;
        std::uint64_t ScanCount = 0;
        std::optional<Trinity::InputActionsData> Data;
        std::string Error;
    };

    // The project folders scripts live in, which the Content Browser shows beside Assets
    static constexpr std::string_view c_ScriptsFolder = "/project/Scripts";
    static constexpr std::string_view c_SourceFolder = "/project/Source";

    EditorSession() = default;
    ~EditorSession();

    EditorSession(const EditorSession&) = delete;
    EditorSession& operator=(const EditorSession&) = delete;

    void Start(const Trinity::ApplicationCommandLineArgs& args);
    void CreateProject(const std::filesystem::path& directory);
    void OpenProject(const std::filesystem::path& path);
    void ScanAssets();
    ImportReport WaitForImports();

    void Request(Command command);
    void Update();
    void DrawPopups();
    void UpdateTitle();

    [[nodiscard]] bool RequestClose();

    // Play runs a copy of the edited scene, made through the serializer, in a SceneManager, so its scripts may switch it to other scenes, and Stop throws whichever scene runs away, so the edited scene, its history and whether it is saved are as they were. Pause holds the copy where it is, and Step takes one fixed step and holds it there. The history takes no edit of the scene while playing, though it takes an asset's. Each is asked for mid-frame and done at the next Update, and any command but Refresh and the builds stops playing first, since a build reloads the module while the scene plays on
    void RequestPlay();
    void RequestPause();
    void RequestStep();
    void RequestStop();
    // With the frame's time, after the session's own work
    void UpdatePlay(Trinity::Timestep timestep);
    [[nodiscard]] bool IsPlaying() const { return m_Play != nullptr; }
    [[nodiscard]] bool IsPaused() const { return m_Play != nullptr && m_Paused; }
    // Play waits for a C++ or C# build running or about to, as Unity waits for scripts to compile, then plays once every build is good. Play again stops it waiting
    [[nodiscard]] bool IsWaitingForBuild() const { return m_PlayAfterBuild; }
    // The builds whose last run failed, such as "the C++ module Game", which Play is refused for until they build. Empty when none has
    [[nodiscard]] std::string GetFailedBuilds() const;

    void Notify(std::string text) { m_Notice = Notice{ std::move(text), std::chrono::steady_clock::now() }; }
    [[nodiscard]] const std::optional<Notice>& GetNotice() const { return m_Notice; }
    [[nodiscard]] std::vector<Activity> GetActivities() const;
    [[nodiscard]] Trinity::SceneRuntime* GetRuntime() { return m_Play ? m_Play->GetRuntime() : nullptr; }
    [[nodiscard]] bool IsDirty() const { return !m_History.IsSaved(); }

    // The scene the panels show: the scene playing, the copy or one a script switched to, and otherwise the edited scene
    [[nodiscard]] Trinity::Scene& GetScene() { return m_Play && m_Play->GetScene() ? *m_Play->GetScene() : GetEditedScene(); }
    // The scene itself, as the Game panel shows it: the scene playing, and otherwise the edited scene, in prefab mode too, as Unity's Game view goes on showing the scene
    [[nodiscard]] Trinity::Scene& GetMainScene() { return m_Play && m_Play->GetScene() ? *m_Play->GetScene() : m_Scene; }
    // What the history edits: in prefab mode the open prefab, which its file holds once saved, and otherwise the scene, which the scene file holds, whether or not a copy plays
    [[nodiscard]] Trinity::Scene& GetEditedScene() { return m_Stage ? m_Stage->Scene : m_Scene; }
    // In prefab mode the open prefab's own, so nothing done there reaches the scene's
    [[nodiscard]] CommandStack& GetHistory() { return m_Stage ? m_Stage->History : m_History; }
    [[nodiscard]] const Trinity::Project* GetProject() const { return m_Project.get(); }
    [[nodiscard]] const Trinity::AssetRegistry* GetRegistry() const { return m_Registry.get(); }
    [[nodiscard]] bool HasProject() const { return m_Project != nullptr; }
    [[nodiscard]] const ModuleHost& GetModule() const { return m_Module; }
    [[nodiscard]] const AssemblyHost& GetAssembly() const { return m_Assembly; }
    // Through the history, so it undoes in one step, and saved with the project as it runs and as it is undone. A scene playing keeps the settings it started with, and the next Play takes the new ones
    bool EditPhysicsSettings(const Trinity::PhysicsSettings& settings, std::string field);
    [[nodiscard]] bool HasScenePath() const { return !m_ScenePath.empty(); }
    [[nodiscard]] Trinity::UUID GetSceneID() const;
    // The scene's file name without its extension, or Untitled for a scene never saved
    [[nodiscard]] std::string GetSceneName() const;

    // In prefab mode the open prefab's, and otherwise the scene's, which is kept for when prefab mode closes
    [[nodiscard]] Trinity::UUID GetSelection() const { return m_Stage ? m_Stage->Selection : m_Selection; }
    void SetSelection(Trinity::UUID selection)
    {
        (m_Stage ? m_Stage->Selection : m_Selection) = selection;
        m_InspectedAsset = {};
    }

    [[nodiscard]] Trinity::UUID GetInspectedAsset() const { return m_InspectedAsset; }
    void SetInspectedAsset(Trinity::UUID asset) { m_InspectedAsset = asset; }

    std::uint64_t AddCloseListener(std::move_only_function<void()> listener);
    void RemoveCloseListener(std::uint64_t id);
    // Edits held outside the scene that nothing has saved yet, as Properties holds import settings not applied: whether there are any, and the question that settles them, applying or reverting, then runs what waited for it. Asked before Forge closes or another project opens
    void SetUnappliedEdits(std::function<bool()> pending, std::function<void(std::move_only_function<void()>)> settle);
    // Called just before a scene starts playing, and just before a C++ or C# build starts, before either logs, as the Console's Clear on Play and Clear on Build need
    void SetBeforePlay(std::function<void()> beforePlay) { m_BeforePlay = std::move(beforePlay); }
    void SetBeforeBuild(std::function<void()> beforeBuild) { m_BeforeBuild = std::move(beforeBuild); }

    void RequestOpenScene(std::string assetPath);
    // Newest first, the projects kept per user and the scenes per project, ten of each
    [[nodiscard]] const std::vector<RecentProject>& GetRecentProjects() const { return m_RecentProjects; }
    [[nodiscard]] const std::vector<RecentScene>& GetRecentScenes() const { return m_RecentScenes; }
    // As Open Project does, after asking about unsaved changes
    void RequestOpenProject(std::filesystem::path path);
    void RemoveRecentProject(const std::filesystem::path& path);
    void RemoveRecentScene(Trinity::UUID scene);
    void ClearRecentScenes();
    // As a person says how long ago something was, such as "12 minutes ago"
    [[nodiscard]] static std::string FormatAge(std::chrono::system_clock::time_point time);
    [[nodiscard]] std::optional<std::string> CreateFolder(std::string_view parent);
    bool MoveAsset(std::string_view path, std::string_view folder, std::string_view name);
    bool DeleteAsset(std::string_view path);
    bool ApplyImportSettings(Trinity::UUID id, Trinity::AssetSettings settings);
    [[nodiscard]] std::optional<std::string> CreateMaterial(std::string_view folder);
    // A script of the language from its template, its class and file named as given, in Scripts or a folder in it for C#, and Source or a folder in it for C++, which its build picks up as the folder's watcher sees it. Its project path, or nothing, logged, for a name that cannot name a class, a file there already, a folder outside the language's, or a project without that language's scripts
    [[nodiscard]] std::optional<std::string> CreateScript(std::string_view folder, Trinity::ScriptLanguage language, std::string_view className);
    // The full name of the class CreateScript makes, in the assembly's namespace for C#, as Game.Player, or the module's for C++, as Game::Player
    [[nodiscard]] std::string GetScriptClassName(Trinity::ScriptLanguage language, std::string_view className) const;
    // As Unity adds a script file dropped on its Inspector or Hierarchy: the loaded class of the file's language named after the file, as Player for Player.cs, with its fields at their defaults, in one command. Refused, with why, while no loaded code has that class, as before the script's first build
    bool AddScript(Trinity::UUID entity, std::string_view path);
    // Letters, digits and underscores, not starting with a digit, which both languages take for a class's name
    [[nodiscard]] static bool IsClassName(std::string_view name);
    // Empty, as New Scene makes one, named New Scene, or New Scene 1 and on when that is taken. Its project path, or nothing, logged
    [[nodiscard]] std::optional<std::string> CreateScene(std::string_view folder);
    // A copy beside the file or folder, named as Unity names one: Name 1, or one more than the number its name ends in. Each file copied gets a UUID of its own and keeps its original's import settings. Its project path, or nothing, logged
    [[nodiscard]] std::optional<std::string> DuplicateAsset(std::string_view path);
    // A file from anywhere, chosen in a dialog at the next Update so it never blocks with a frame open, copied into the folder and numbered when its name is taken there
    void RequestImportAsset(std::string folder);
    // Everything cooked from the file, or from every file in the folder, cooked again, since the importers forget what they cooked it from. Assets in use load again as their imports end. Refused, logged, while imports run
    bool ReimportAssets(std::string_view path);
    [[nodiscard]] bool IsImportingAssets() const { return IsImportRunning() || m_ImportAgain; }
    // The project's default input actions, whose first map Play enables, saved with the project at once. Anything but an input actions asset is refused
    void SetDefaultInputActions(Trinity::UUID asset);
    // With one empty map, Gameplay, named New Input Actions, or New Input Actions 1 and on when that is taken. Its project path, or nothing, logged
    [[nodiscard]] std::optional<std::string> CreateInputActions(std::string_view folder);
    // As the Input Actions panel and Properties read it each frame, from its file the first time and again once the files are scanned, as when Forge gets the focus back, so an edit made outside it is seen. Null for an asset that is not one
    [[nodiscard]] const InputActionsFile* ReadInputActions(Trinity::UUID asset);
    // Its file, only once what is written reads back, and the asset loaded again where it is in use, so a scene playing reads it from the next frame
    bool SaveInputActions(Trinity::UUID asset, const Trinity::InputActionsData& data);
    // Through the history, so it undoes in one step, named for what it did, such as "Add action Jump", and the asset. Edits of the same field in a row become one, as a drag's do, and one with no field is always a step of its own. Taken while a scene plays, which reads it at once, and kept once it stops
    bool EditInputActions(Trinity::UUID asset, const Trinity::InputActionsData& before, const Trinity::InputActionsData& after, std::string_view what, std::string field = {});
    // As EditInputActions, for a rename of a map or an action, which in the project's default asset also rewrites the edited scene's input action fields that named it, in the same step. Refused with a notice while a scene plays and a field would change, since the scene takes no edit then
    bool RenameInputActions(Trinity::UUID asset, const Trinity::InputActionsData& before, const Trinity::InputActionsData& after, std::string_view what);

    // Forge's settings for the person using it, saved once nothing is being dragged or typed into
    [[nodiscard]] const EditorPreferences& GetPreferences() const { return m_Preferences; }
    void SetPreferences(const EditorPreferences& preferences);
    // One of the console variables kept with them, which the command line no longer holds once it is set here
    void SetPreferenceVariable(std::string_view name, std::string_view value);
    // Whether Preferences.txt was there as Forge started, or has been written since. Until it is, imgui.ini's settings from before it are read into it
    [[nodiscard]] bool HasPreferencesFile() const { return m_HasPreferencesFile; }

    // What scripts open in, which the preferences keep
    [[nodiscard]] ScriptEditor GetScriptEditor() const { return m_Preferences.Editor; }
    void SetScriptEditor(ScriptEditor editor);
    // A file of the project at its line, counted from 1, or at its top for 0, in the script editor, with the C# solution for a .cs file and the module's folder for any other. The path is a project path, such as /project/Scripts/Player.cs, one relative to the project's folder, as build errors name them, or a native one
    bool OpenScript(std::string_view path, std::uint32_t line = 0);
    // A script class's file at the class, as Unity's Edit Script opens it: the file named after the class in the project's Scripts folder for C#, or its Source folder for C++, or else the first there that declares it
    bool EditScript(Trinity::ScriptLanguage language, std::string_view className);
    // The C# solution beside the project file, or the C++ module's Source folder, in the script editor
    bool OpenSolution();
    bool OpenModuleFolder();
    // The script editor's debugger on this Forge, with the C# solution open, or the module's folder without one
    bool AttachDebugger();
    // Whether Play waits for a debugger first, which the preferences keep. While it waits, a popup shows Forge's process ID and what is attached, and the scene plays once a native or a managed debugger is, or at Play Now
    [[nodiscard]] bool GetWaitForDebugger() const { return m_Preferences.WaitForDebugger; }
    void SetWaitForDebugger(bool wait);
    [[nodiscard]] bool IsWaitingForDebugger() const { return m_DebuggerWait.has_value(); }
    // Where the material lives: its own file, or for a model's material the fields that differ from the import, in the model's .meta, and its cooked file. A loaded material shows it the same frame
    bool WriteMaterial(Trinity::UUID material, const Trinity::MaterialData& data);
    // Through the history, so it undoes in one step
    bool EditMaterial(Trinity::UUID material, const Trinity::MaterialData& before, const Trinity::MaterialData& after, std::string field);
    // As its file holds it, with a model's overrides. Nothing when it cannot be read
    [[nodiscard]] std::optional<Trinity::MaterialData> ReadMaterial(Trinity::UUID material) const;
    // A model's material as it was imported, before its overrides. Nothing for a material of its own
    [[nodiscard]] std::optional<Trinity::MaterialData> ReadImportedMaterial(Trinity::UUID material) const;
    // A model's material's key, or nothing for a material of its own
    [[nodiscard]] std::optional<std::string> GetMaterialKey(Trinity::UUID material) const;
    bool CreateModel(Trinity::UUID model, Trinity::UUID parent, Trinity::UUID before, glm::vec3 position);
    // A prefab of the entity and its subtree, written into the folder as a file named after the entity, numbered when that is taken, and the entity made its first instance in one command, keeping every UUID it has. The instances in the subtree are nested in it, and an instance's root makes the kind asked for: an original, as though the instance were unpacked first, or a variant, named after the entity as Enemy Variant. In prefab mode the open prefab's entity becomes an instance nested in it. Undo makes them as they were and leaves the file, as in Unity. Its project path, or nothing, with a notice for an entity that cannot become one. Not while a copy plays
    [[nodiscard]] std::optional<std::string> CreatePrefab(Trinity::UUID entity, std::string_view folder, PrefabKind kind = PrefabKind::Original);
    // A variant of the prefab beside its file, as Unity's Create > Prefab Variant makes one: its root an instance of the prefab that changes nothing, named after it, as Enemy Variant, numbered when that is taken. Not a step of the history, as no new asset is. Its project path, or nothing, logged
    [[nodiscard]] std::optional<std::string> CreatePrefabVariant(Trinity::UUID prefab);
    // A new instance of the prefab in one command, last under the parent or among the roots, or before the sibling. Its root goes to the position when one is given, as a drop into the Scene panel gives, which is then the instance's own, and otherwise keeps the prefab's transform, as a drop into the Hierarchy does. In prefab mode it is nested in the open prefab, refused with a notice for a prefab holding that one, which would then hold itself
    bool InstantiatePrefab(Trinity::UUID prefab, Trinity::UUID parent, Trinity::UUID before, std::optional<glm::vec3> position);
    // The template the open scene's instances of the prefab are made from, which names the entities an instance removed. Null as the serializer finds none
    [[nodiscard]] Trinity::Scene* FindPrefabTemplate(Trinity::UUID prefab) { return m_Prefabs.Find(prefab, m_Registry.get()); }
    // What an instance changes of its prefab undone, in one command, so it is as its prefab has it. What names the changes, such as Position of Body, and the subject is the entity shown, which stays selected. Not while a copy plays
    bool RevertPrefabOverrides(Trinity::UUID root, std::vector<Trinity::SceneSerializer::PrefabOverride> overrides, std::string_view what, Trinity::UUID subject);
    // What an instance changes of its prefab made the own of the prefab at the level, 0 for the instance's prefab and each level down one nested in it, as SceneSerializer::GetOverrideLevels names them, in one command: the file of that prefab, and of each between it and the instance that changed the same, is written at once, and every instance of them, in the scene and in the open prefab, is made again from them, each keeping what else it changes. Undo writes the files back. A change made to a file outside Forge since the files were scanned is read first, so it is kept. Refused with a notice for a prefab whose file is gone, and for an instance added that the prefab would then hold itself through. Not while a copy plays
    bool ApplyPrefabOverrides(Trinity::UUID root, std::vector<Trinity::SceneSerializer::PrefabOverride> overrides, std::string_view what, Trinity::UUID subject, std::size_t level = 0);
    // As Unity's Unpack: the instance's own entities made plain ones, keeping every UUID and what they hold, so nothing links them to its prefab, with each instance nested in that prefab made an instance of its own, and a variant's root its base's instance, in one command. Completely, as Unpack Completely, the instance and every instance in it are unpacked, so its subtree holds plain entities alone. Refused with a notice for an entity standing in for a missing prefab, which has nothing to unpack until the prefab is back, and for one whose prefab holds an instance whose prefab is missing, which only that prefab's file keeps
    bool UnpackPrefab(Trinity::UUID root, bool completely = false);
    // As Unity's prefab mode: the prefab, as its file holds it, edited in isolation in a scene of its own, with a selection and a history of its own, while the scene keeps its own as they were. Save writes the file and makes every instance of it in the scene again from it, keeping what each changes, a step of neither history. Asked for mid-frame and opened at the next Update, once a copy playing has stopped and the unsaved changes of a prefab open already are settled. Play is refused while it is open
    void RequestOpenPrefab(Trinity::UUID prefab);
    // Back to the scene, after asking what to do with unsaved changes, which Cancel keeps open
    void RequestClosePrefab();
    // The prefab prefab mode has open, or none
    [[nodiscard]] Trinity::UUID GetOpenPrefab() const { return m_Stage ? m_Stage->Prefab : Trinity::UUID(); }
    [[nodiscard]] bool IsPrefabDirty() const { return m_Stage && !m_Stage->History.IsSaved(); }
    // Its file's name without the extension. Empty while none is open
    [[nodiscard]] std::string GetOpenPrefabName() const;
    // As the Hierarchy moves an entity, keeping its place in the world, in one command. One of a prefab's entities in an instance keeps the place its prefab gives it, as in Unity, and nothing goes under an entity standing in for a missing prefab, each refused with a notice
    bool MoveEntity(Trinity::UUID entity, Trinity::UUID parent, Trinity::UUID before);
    // Just after the original, in one command. Refused with a notice for an entity holding an instance whose prefab is missing, which only it can keep
    bool DuplicateEntity(Trinity::UUID entity);
    // In one command, as the Delete key and the Hierarchy's menu ask
    bool DeleteEntity(Trinity::UUID entity);
    // A sprite of the texture at the point, its size in units, as a texture dropped into the Scene panel makes one, in one command
    bool CreateSprite(std::string name, Trinity::UUID texture, glm::vec3 position, glm::vec2 size);
    // A built-in shape with its MeshRenderer and the collider that fits it, in one command. Under a parent it sits at the parent's origin, and otherwise at the spawn point. In prefab mode, as Unity keeps a prefab to one root, whatever is made, pasted or moved with no parent goes last under the prefab's root, keeping its place in the world, and the root is never moved, duplicated or deleted, each refused with a notice. No instance of a prefab holding the open one goes into it, which would then hold itself
    bool CreatePrimitive(Trinity::BuiltInMesh mesh, Trinity::UUID parent);
    // As Unity's GameObject menu makes them, each in one command and placed as a built-in shape is
    bool CreateEmpty(Trinity::UUID parent);
    bool CreateLight(Trinity::LightType type, Trinity::UUID parent);
    // Perspective while the Scene panel shows the scene in 3D, and orthographic in 2D
    bool CreateCamera(Trinity::UUID parent);
    // The entity and its subtree as they are now, to paste into this scene or another until the project closes. While a copy plays, as the playing entity is, to paste once it stops. Refused with a notice, as Duplicate is, for one holding an instance whose prefab is missing
    void CopyEntity(Trinity::UUID entity);
    [[nodiscard]] bool HasCopiedEntity() const { return m_CopiedEntity.has_value(); }
    // As Unity pastes: right after the original while the scene has it, else last under the parent it was copied from, else last among the roots. Not while a copy plays, nor while what was copied holds an instance whose prefab is gone since, or in prefab mode one whose prefab holds the open one
    bool PasteEntity();
    // Last under the parent
    bool PasteEntityAsChild(Trinity::UUID parent);
    // Where the Scene panel's camera looks, which the panel sets each frame with whether it shows the scene in 3D: the 3D camera's focus, or the 2D camera's centre at Z 0
    [[nodiscard]] glm::vec3 GetSpawnPoint() const { return m_SpawnPoint; }
    [[nodiscard]] bool IsSceneView3D() const { return m_SceneView3D; }
    void SetSpawnPoint(glm::vec3 point, bool view3D)
    {
        m_SpawnPoint = point;
        m_SceneView3D = view3D;
    }
    [[nodiscard]] bool IsImporting(Trinity::UUID id) const { return m_Reimporter.IsBusy(id); }
    [[nodiscard]] bool IsImportingModels() const { return m_ModelImports.IsRunning() || m_ImportAgain; }
    [[nodiscard]] std::uint64_t GetScanCount() const { return m_ScanCount; }
    // Model import batches finished, so what reads cooked model files knows to read them again
    [[nodiscard]] std::uint64_t GetModelImportCount() const { return m_ModelImportCount; }

private:
    using Action = std::move_only_function<void()>;
    using PathAction = std::move_only_function<void(const std::filesystem::path&)>;

    // A Play that waits for a debugger, and when one was first seen attached
    struct DebuggerWait
    {
        std::optional<std::chrono::steady_clock::time_point> AttachedAt;
    };

    // A scene backup a crash left, found as the project opened: the scene's text, the asset path it was opened from, empty for an untitled scene, and when it was written
    struct Recovery
    {
        std::string Text;
        std::string ScenePath;
        std::optional<std::chrono::system_clock::time_point> Written;
    };

    enum class PathKind : std::uint8_t
    {
        OpenFile,
        SaveFile,
        Folder
    };

    struct PathRequest
    {
        PathKind Kind = PathKind::OpenFile;
        std::string Title;
        std::span<const Trinity::FileDialogFilter> Filters;
        std::filesystem::path Folder;
        std::string FileName;
        PathAction OnChosen;
    };

    // What Copy took, with where the original was and its place in the world, which a paste away from that parent keeps
    struct CopiedEntity
    {
        std::string Text;
        Trinity::UUID Original;
        Trinity::UUID Parent;
        Trinity::TransformComponent World;
    };

    // A prefab open in prefab mode: its entities under their local IDs in a scene of their own, and the text its file held as it was last read or saved, which a change made outside Forge is found against
    struct PrefabStage
    {
        Trinity::UUID Prefab;
        std::string Text;
        Trinity::Scene Scene;
        Trinity::UUID Selection;
        CommandStack History{ Scene, Selection };
    };

    // Where something made, pasted or moved goes: under the parent, before the sibling
    struct Place
    {
        Trinity::UUID Parent;
        Trinity::UUID Before;
    };

    void Run(Command command);
    void AskPath(PathRequest request);
    void ConfirmDiscard(Action action);
    void ConfirmClosePrefab(Action then);
    void ConfirmCloseProject(Action action);
    [[nodiscard]] bool SettleUnappliedEdits(Action& then);

    void CloseProject();
    void ReadPreferences(const Trinity::ApplicationCommandLineArgs& args);
    void WritePreferences();
    void ReadRecentProjects();
    void WriteRecentProjects() const;
    void RememberProject();
    void ReadRecentScenes();
    void WriteRecentScenes() const;
    void RememberScene();
    bool PasteEntityAt(Trinity::UUID parent, Trinity::UUID before);
    [[nodiscard]] bool CanTakeChildren(Trinity::UUID parent);
    [[nodiscard]] Place FindPlace(Trinity::UUID parent, Trinity::UUID before);
    [[nodiscard]] glm::vec3 ToLocalPoint(Trinity::UUID parent, glm::vec3 point);
    [[nodiscard]] bool IsPrefabRoot(Trinity::UUID entity);
    [[nodiscard]] bool RefuseHoldingPrefab(Trinity::UUID prefab);
    [[nodiscard]] std::optional<std::string> FindUnpackProblem(Trinity::Entity root, bool completely);
    [[nodiscard]] std::string GetPrefabName(Trinity::UUID prefab) const;
    [[nodiscard]] Trinity::Expected<std::size_t, std::string> ReplacePrefabs(std::span<const Trinity::SceneSerializer::PrefabText> texts, const Trinity::Scene* own);
    [[nodiscard]] std::optional<std::size_t> WritePrefabs(std::span<const Trinity::SceneSerializer::PrefabText> texts, const Trinity::Scene* own);
    void UpdateChangedPrefabs();
    void OpenPrefab(Trinity::UUID prefab);
    void ClosePrefab();
    bool SavePrefab();
    void UpdatePrefabStage();
    void AttachRegistry();
    void StartImports();
    void StartTextureImports();
    void ImportAgainIfAsked();
    [[nodiscard]] bool IsImportRunning() const;
    void FinishImports(const TextureImportReport& report);
    void FinishModelImports(const ModelImportReport& report);
    void FinishEnvironmentImports(const EnvironmentImportReport& report);
    void SaveProject();
    void ImportAsset(const std::filesystem::path& file, const std::string& folder);
    bool WritePhysicsSettings(const Trinity::PhysicsSettings& settings);
    void BeginPlay();
    void StartPlay();
    void StopPlay();
    void WriteBackup(std::string_view sceneText);
    void RemoveBackup();
    [[nodiscard]] std::optional<Recovery> ReadBackup() const;
    void RestoreBackup();
    void UpdateDebuggerWait();
    [[nodiscard]] bool IsBuildingScripts() const;
    void NewScene();
    void OpenScene(const std::string& assetPath);
    void ReportMissingReferences();
    void SaveScene(Action then);
    void SaveSceneAs(Action then);
    [[nodiscard]] bool WriteScene(const std::string& assetPath);

    void DrawUnsavedPopup();
    void DrawUnsavedPrefabPopup();
    void DrawPathPopup();
    void DrawImportPopup();
    void DrawDebuggerPopup();
    void DrawRecoveryPopup();

    Trinity::Scope<Trinity::Project> m_Project;
    Trinity::Scope<Trinity::AssetRegistry> m_Registry;
    Trinity::Scene m_Scene;
    Trinity::Scope<Trinity::SceneManager> m_Play;
    bool m_Paused = false;
    bool m_StepRequested = false;
    std::string m_ScenePath;
    Trinity::UUID m_Selection;
    CommandStack m_History{ m_Scene, m_Selection };
    Trinity::UUID m_InspectedAsset;
    std::optional<CopiedEntity> m_CopiedEntity;
    // What the serializer finds prefabs in, since Forge opens a scene at once, before the asset manager could load them
    PrefabTemplateCache m_Prefabs;
    // The prefab prefab mode has open, or null while the scene is edited
    Trinity::Scope<PrefabStage> m_Stage;
    std::vector<RecentProject> m_RecentProjects;
    std::vector<RecentScene> m_RecentScenes;
    std::unordered_map<Trinity::UUID, InputActionsFile> m_InputActionsFiles;
    glm::vec3 m_SpawnPoint{ 0.0f };
    bool m_SceneView3D = false;
    TextureReimporter m_Reimporter;
    TextureImportBatch m_Imports;
    ModelImportBatch m_ModelImports;
    EnvironmentImportBatch m_EnvironmentImports;
    ModuleHost m_Module;
    AssemblyHost m_Assembly;
    EditorPreferences m_Preferences;
    // The kept console variables the command line set, each with the value the file had, written back in its place. Empty for one the file did not have
    std::map<std::string, std::string, std::less<>> m_HeldVariables;
    bool m_HasPreferencesFile = false;
    bool m_PreferencesChanged = false;
    std::optional<DebuggerWait> m_DebuggerWait;
    bool m_OpenDebuggerPopup = false;
    bool m_PlayAfterBuild = false;
    std::optional<Notice> m_Notice;
    // Whether this session wrote the backup in the cache, which it removes once the scene has no unsaved changes or the project closes as it should
    bool m_HasBackup = false;
    std::optional<Recovery> m_Recovery;
    bool m_OpenRecoveryPopup = false;
    bool m_TexturesAfterModels = false;
    bool m_ImportAgain = false;
    bool m_ImportPopupOpen = false;
    std::vector<std::pair<std::uint64_t, std::move_only_function<void()>>> m_CloseListeners;
    std::function<bool()> m_HasUnappliedEdits;
    std::function<void(Action)> m_SettleUnappliedEdits;
    std::function<void()> m_BeforePlay;
    std::function<void()> m_BeforeBuild;
    std::uint64_t m_NextCloseListener = 1;
    std::uint64_t m_ScanCount = 0;
    std::uint64_t m_ModelImportCount = 0;

    std::vector<Action> m_Pending;
    Action m_AfterDiscard;
    bool m_OpenUnsavedPopup = false;
    Action m_AfterPrefab;
    bool m_OpenPrefabPopup = false;

    std::optional<PathRequest> m_PathPrompt;
    std::string m_PathText;
    bool m_OpenPathPopup = false;

    std::string m_Title;
};
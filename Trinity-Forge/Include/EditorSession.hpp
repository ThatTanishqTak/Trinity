#pragma once

#include "CommandStack.hpp"
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

#include "Trinity.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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

    // Play runs a copy of the edited scene, made through the serializer, in a SceneManager, so its scripts may switch it to other scenes, and Stop throws whichever scene runs away, so the edited scene, its history and whether it is saved are as they were. Pause holds the copy where it is, and Step takes one fixed step and holds it there. The history is locked while playing. Each is asked for mid-frame and done at the next Update, and any command but Refresh and the builds stops playing first, since a build reloads the module while the scene plays on
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
    [[nodiscard]] Trinity::Scene& GetScene() { return m_Play && m_Play->GetScene() ? *m_Play->GetScene() : m_Scene; }
    // What the history edits and the scene file holds, whether or not a copy plays
    [[nodiscard]] Trinity::Scene& GetEditedScene() { return m_Scene; }
    [[nodiscard]] CommandStack& GetHistory() { return m_History; }
    [[nodiscard]] const Trinity::Project* GetProject() const { return m_Project.get(); }
    [[nodiscard]] const Trinity::AssetRegistry* GetRegistry() const { return m_Registry.get(); }
    [[nodiscard]] bool HasProject() const { return m_Project != nullptr; }
    [[nodiscard]] const ModuleHost& GetModule() const { return m_Module; }
    [[nodiscard]] const AssemblyHost& GetAssembly() const { return m_Assembly; }
    // Through the history, so it undoes in one step, and saved with the project as it runs and as it is undone. A scene playing keeps the settings it started with, and the next Play takes the new ones
    bool EditPhysicsSettings(const Trinity::PhysicsSettings& settings, std::string field);
    [[nodiscard]] bool HasScenePath() const { return !m_ScenePath.empty(); }
    [[nodiscard]] Trinity::UUID GetSceneID() const;

    [[nodiscard]] Trinity::UUID GetSelection() const { return m_Selection; }
    void SetSelection(Trinity::UUID selection)
    {
        m_Selection = selection;
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
    [[nodiscard]] std::optional<std::string> CreateFolder(std::string_view parent);
    bool MoveAsset(std::string_view path, std::string_view folder, std::string_view name);
    bool DeleteAsset(std::string_view path);
    bool ApplyImportSettings(Trinity::UUID id, Trinity::AssetSettings settings);
    [[nodiscard]] std::optional<std::string> CreateMaterial(std::string_view folder);
    // A script of the language from its template, its class and file named as given, in Scripts or a folder in it for C#, and Source or a folder in it for C++, which its build picks up as the folder's watcher sees it. Its project path, or nothing, logged, for a name that cannot name a class, a file there already, a folder outside the language's, or a project without that language's scripts
    [[nodiscard]] std::optional<std::string> CreateScript(std::string_view folder, Trinity::ScriptLanguage language, std::string_view className);
    // The full name of the class CreateScript makes, in the assembly's namespace for C#, as Game.Player, or the module's for C++, as Game::Player
    [[nodiscard]] std::string GetScriptClassName(Trinity::ScriptLanguage language, std::string_view className) const;
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

    // What scripts open in, which imgui.ini keeps for each person
    [[nodiscard]] ScriptEditor GetScriptEditor() const { return m_ScriptEditor; }
    void SetScriptEditor(ScriptEditor editor) { m_ScriptEditor = editor; }
    // A file of the project at its line, counted from 1, or at its top for 0, in the script editor, with the C# solution for a .cs file and the module's folder for any other. The path is a project path, such as /project/Scripts/Player.cs, one relative to the project's folder, as build errors name them, or a native one
    bool OpenScript(std::string_view path, std::uint32_t line = 0);
    // A script class's file at the class, as Unity's Edit Script opens it: the file named after the class in the project's Scripts folder for C#, or its Source folder for C++, or else the first there that declares it
    bool EditScript(Trinity::ScriptLanguage language, std::string_view className);
    // The C# solution beside the project file, or the C++ module's Source folder, in the script editor
    bool OpenSolution();
    bool OpenModuleFolder();
    // The script editor's debugger on this Forge, with the C# solution open, or the module's folder without one
    bool AttachDebugger();
    // Whether Play waits for a debugger first, which imgui.ini keeps for each person. While it waits, a popup shows Forge's process ID and what is attached, and the scene plays once a native or a managed debugger is, or at Play Now
    [[nodiscard]] bool GetWaitForDebugger() const { return m_WaitForDebugger; }
    void SetWaitForDebugger(bool wait) { m_WaitForDebugger = wait; }
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
    // A built-in shape with its MeshRenderer and the collider that fits it, in one command. Under a parent it sits at the parent's origin, and otherwise at the spawn point
    bool CreatePrimitive(Trinity::BuiltInMesh mesh, Trinity::UUID parent);
    // The entity and its subtree as they are now, to paste into this scene or another until the project closes. While a copy plays, as the playing entity is, to paste once it stops
    void CopyEntity(Trinity::UUID entity);
    [[nodiscard]] bool HasCopiedEntity() const { return m_CopiedEntity.has_value(); }
    // As Unity pastes: right after the original while the scene has it, else last under the parent it was copied from, else last among the roots. Not while a copy plays
    bool PasteEntity();
    // Last under the parent
    bool PasteEntityAsChild(Trinity::UUID parent);
    // Where the Scene panel's camera looks, which the panel sets each frame: the 3D camera's focus, or the 2D camera's centre at Z 0
    [[nodiscard]] glm::vec3 GetSpawnPoint() const { return m_SpawnPoint; }
    void SetSpawnPoint(glm::vec3 point) { m_SpawnPoint = point; }
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

    void Run(Command command);
    void AskPath(PathRequest request);
    void ConfirmDiscard(Action action);
    void ConfirmCloseProject(Action action);
    [[nodiscard]] bool SettleUnappliedEdits(Action& then);

    void CloseProject();
    bool PasteEntityAt(Trinity::UUID parent, Trinity::UUID before);
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
    [[nodiscard]] std::string GetSceneName() const;

    void DrawUnsavedPopup();
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
    glm::vec3 m_SpawnPoint{ 0.0f };
    TextureReimporter m_Reimporter;
    TextureImportBatch m_Imports;
    ModelImportBatch m_ModelImports;
    EnvironmentImportBatch m_EnvironmentImports;
    ModuleHost m_Module;
    AssemblyHost m_Assembly;
    ScriptEditor m_ScriptEditor = ScriptEditor::System;
    bool m_WaitForDebugger = false;
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

    std::optional<PathRequest> m_PathPrompt;
    std::string m_PathText;
    bool m_OpenPathPopup = false;

    std::string m_Title;
};
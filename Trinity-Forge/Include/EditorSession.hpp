#pragma once

#include "AudioClipPreview.hpp"
#include "CommandStack.hpp"
#include "EditorPreferences.hpp"
#include "Importers/AudioClipImportBatch.hpp"
#include "Importers/AudioClipImporter.hpp"
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
#include "SoundEventAudition.hpp"

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
#include <unordered_set>
#include <utility>
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
        ChooseExportFolder,
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
        AudioClipImportReport AudioClips;
    };

    // A line shown over the editor for a moment, for something asked for that did not happen
    struct Notice
    {
        std::string Text;
        std::chrono::steady_clock::time_point Shown;
    };

    // Background work for the status bar: what it is, and how far along from 0 to 1 when known
    struct Activity
    {
        std::string Text;
        std::optional<float> Progress;
    };

    // As Unity asks when an instance's root is dragged to the Project window: an original or a variant
    enum class PrefabKind : std::uint8_t
    {
        Original,
        Variant
    };

    // An empty script, or a character that walks by Move and jumps by Jump, in 3D or 2D
    enum class ScriptTemplate : std::uint8_t
    {
        Script,
        Character3D,
        Character2D
    };

    // Its file, its name then and when, and whether the file was gone as the list was last read
    struct RecentProject
    {
        std::filesystem::path Path;
        std::string Name;
        std::chrono::system_clock::time_point OpenedAt;
        bool Missing = false;
    };

    // By UUID, so it is followed as its file moves, with its path then, for one the project lost
    struct RecentScene
    {
        Trinity::UUID ID;
        std::string Path;
    };

    // What its file holds, or why it could not be read, kept until the files are scanned again
    struct InputActionsFile
    {
        Trinity::UUID Asset;
        std::uint64_t ScanCount = 0;
        std::optional<Trinity::InputActionsData> Data;
        std::string Error;
    };

    // An audio mixer asset as its file holds it, as for input actions
    struct AudioMixerFile
    {
        Trinity::UUID Asset;
        std::uint64_t ScanCount = 0;
        std::optional<Trinity::AudioMixerData> Data;
        std::string Error;
    };

    // A sound event asset as its file holds it, as for input actions
    struct SoundEventFile
    {
        Trinity::UUID Asset;
        std::uint64_t ScanCount = 0;
        std::optional<Trinity::SoundEventData> Data;
        std::string Error;
    };

    // The event, the container chosen in its tree, if any, and the parameter whose curves are edited
    struct SoundEventSelection
    {
        Trinity::UUID Asset;
        Trinity::UUID Container;
        Trinity::UUID Parameter;

        [[nodiscard]] bool operator==(const SoundEventSelection&) const = default;
    };

    // The mixer, the snapshot whose values are edited, and the group or effect chosen, if any
    struct MixerSelection
    {
        Trinity::UUID Asset;
        Trinity::UUID Snapshot;
        Trinity::UUID Item;

        [[nodiscard]] bool operator==(const MixerSelection&) const = default;
    };

    // The project folders scripts live in, which the Content Browser shows beside Assets
    static constexpr std::string_view c_ScriptsFolder = "/project/Scripts";
    static constexpr std::string_view c_SourceFolder = "/project/Source";

    EditorSession() = default;
    ~EditorSession();

    EditorSession(const EditorSession&) = delete;
    EditorSession& operator=(const EditorSession&) = delete;

    void Start(const Trinity::ApplicationCommandLineArgs& args);
    // In the language its scripts keep, from a template or with an empty start scene. False on failure
    bool CreateProject(const std::filesystem::path& directory, Trinity::ScriptLanguage language, const Trinity::ProjectTemplate* projectTemplate = nullptr);
    // An older one is asked about first. language answers for one older than format 10 that needs it
    void OpenProject(const std::filesystem::path& path, std::optional<Trinity::ScriptLanguage> language = std::nullopt);
    // As the question's Back Up and Open, without asking, as --upgrade-project does. False, logged
    bool UpgradeProject(const std::filesystem::path& path, std::optional<Trinity::ScriptLanguage> language);
    void ScanAssets();
    ImportReport WaitForImports();

    void Request(Command command);
    void Update();
    void DrawPopups();
    void UpdateTitle();

    [[nodiscard]] bool RequestClose();

    // Each is asked for mid-frame and done at the next Update. Stop leaves the edited scene as it was
    void RequestPlay();
    void RequestPause();
    void RequestStep();
    void RequestStop();
    // With the frame's time, after the session's own work, the Sound Event panel's audition first
    void UpdatePlay(Trinity::Timestep timestep);
    [[nodiscard]] bool IsPlaying() const { return m_Play != nullptr; }
    [[nodiscard]] bool IsPaused() const { return m_Play != nullptr && m_Paused; }
    // Play waits for a build running or about to, then plays once every build is good
    [[nodiscard]] bool IsWaitingForBuild() const { return m_PlayAfterBuild; }
    // The builds whose last run failed, such as "the C++ module Game". Empty when none has
    [[nodiscard]] std::string GetFailedBuilds() const;

    void Notify(std::string text) { m_Notice = Notice{ std::move(text), std::chrono::steady_clock::now() }; }
    [[nodiscard]] const std::optional<Notice>& GetNotice() const { return m_Notice; }
    [[nodiscard]] std::vector<Activity> GetActivities() const;
    [[nodiscard]] Trinity::SceneRuntime* GetRuntime() { return m_Play ? m_Play->GetRuntime() : nullptr; }
    [[nodiscard]] bool IsDirty() const { return !m_History.IsSaved(); }

    // The scene the panels show: the scene playing, and otherwise the edited scene
    [[nodiscard]] Trinity::Scene& GetScene() { return m_Play && m_Play->GetScene() ? *m_Play->GetScene() : GetEditedScene(); }
    // As the Game panel shows it: the scene playing, else the scene itself, in prefab mode too
    [[nodiscard]] Trinity::Scene& GetMainScene() { return m_Play && m_Play->GetScene() ? *m_Play->GetScene() : m_Scene; }
    // What the history edits: in prefab mode the open prefab, and otherwise the scene
    [[nodiscard]] Trinity::Scene& GetEditedScene() { return m_Stage ? m_Stage->Scene : m_Scene; }
    // In prefab mode the open prefab's own, so nothing done there reaches the scene's
    [[nodiscard]] CommandStack& GetHistory() { return m_Stage ? m_Stage->History : m_History; }
    [[nodiscard]] const Trinity::Project* GetProject() const { return m_Project.get(); }
    [[nodiscard]] const Trinity::AssetRegistry* GetRegistry() const { return m_Registry.get(); }
    [[nodiscard]] bool HasProject() const { return m_Project != nullptr; }
    [[nodiscard]] const ModuleHost& GetModule() const { return m_Module; }
    [[nodiscard]] const AssemblyHost& GetAssembly() const { return m_Assembly; }
    // Through the history, and saved with the project. A scene playing keeps what it started with
    bool EditPhysicsSettings(const Trinity::PhysicsSettings& settings, std::string field);
    // As EditPhysicsSettings, but the engine plays the mixer from the next frame, even while playing
    bool EditAudioSettings(const Trinity::AudioSettings& settings, std::string field);
    // As EditPhysicsSettings, for what the project exports, which nothing playing reads
    bool EditExportSettings(const Trinity::ExportSettings& settings, std::string field);
    [[nodiscard]] bool HasScenePath() const { return !m_ScenePath.empty(); }
    [[nodiscard]] Trinity::UUID GetSceneID() const;
    // The scene's file name without its extension, or Untitled for a scene never saved
    [[nodiscard]] std::string GetSceneName() const;

    // In prefab mode the open prefab's. Otherwise the scene's, which is kept while prefab mode is open
    [[nodiscard]] const EditorSelection& GetSelected() const { return m_Stage ? m_Stage->Selection : m_Selection; }
    // The primary one of the entities selected, which is the only one while one is selected at a time
    [[nodiscard]] Trinity::UUID GetSelection() const { return GetSelected().GetPrimary(); }
    void SetSelection(Trinity::UUID selection)
    {
        (m_Stage ? m_Stage->Selection : m_Selection).Set(selection);
        m_InspectedAsset = {};
        m_InspectingMixer = false;
    }

    // These alone, the last of them the primary, as a Shift-click selects a run of rows
    void SetSelection(std::vector<Trinity::UUID> entities)
    {
        (m_Stage ? m_Stage->Selection : m_Selection).Set(std::move(entities));
        m_InspectedAsset = {};
        m_InspectingMixer = false;
    }

    // A Ctrl-click's: in as the primary if it was out, and out if it was in
    void ToggleSelection(Trinity::UUID entity)
    {
        (m_Stage ? m_Stage->Selection : m_Selection).Toggle(entity);
        m_InspectedAsset = {};
        m_InspectingMixer = false;
    }

    // Entities the Scene panel leaves out, and those a click in it passes over. The user's own, kept in the project's cache and never in the scene's file
    [[nodiscard]] bool IsHidden(Trinity::UUID entity) const { return m_Hidden.contains(entity); }
    [[nodiscard]] bool IsLocked(Trinity::UUID entity) const { return m_Locked.contains(entity); }
    [[nodiscard]] const std::unordered_set<Trinity::UUID>& GetHidden() const { return m_Hidden; }
    [[nodiscard]] const std::unordered_set<Trinity::UUID>& GetLocked() const { return m_Locked; }
    void SetHidden(std::span<const Trinity::UUID> entities, bool hidden);
    void SetLocked(std::span<const Trinity::UUID> entities, bool locked);

    // An asset inspected, or the Audio Mixer panel's group or effect, takes Properties from the other
    [[nodiscard]] Trinity::UUID GetInspectedAsset() const { return m_InspectedAsset; }
    void SetInspectedAsset(Trinity::UUID asset)
    {
        m_InspectedAsset = asset;
        m_InspectingMixer = false;
    }

    [[nodiscard]] bool IsInspectingMixer() const { return m_InspectingMixer; }
    void SetInspectingMixer(bool inspecting)
    {
        m_InspectingMixer = inspecting;
        m_InspectedAsset = inspecting ? Trinity::UUID() : m_InspectedAsset;
    }

    std::uint64_t AddCloseListener(std::move_only_function<void()> listener);
    void RemoveCloseListener(std::uint64_t id);
    // Edits held outside the scene, as unapplied import settings. Settled before Forge closes
    void SetUnappliedEdits(std::function<bool()> pending, std::function<void(std::move_only_function<void()>)> settle);
    // Just before a scene plays or a build starts, before either logs, as the Console's Clear needs
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
    // In Scripts for C# and Source for C++. Its project path, or nothing, logged, if it cannot be made
    [[nodiscard]] std::optional<std::string> CreateScript(std::string_view folder, Trinity::ScriptLanguage language, std::string_view className, ScriptTemplate kind = ScriptTemplate::Script);
    // In the assembly's namespace for C#, as Game.Player, or the module's for C++, as Game::Player
    [[nodiscard]] std::string GetScriptClassName(Trinity::ScriptLanguage language, std::string_view className) const;
    // The loaded class named after the file, as Player for Player.cs. Refused before its first build
    bool AddScript(Trinity::UUID entity, std::string_view path);
    // Letters, digits and underscores, not starting with a digit, as both languages take
    [[nodiscard]] static bool IsClassName(std::string_view name);
    // Named New Scene, or New Scene 1 and on when taken. Its project path, or nothing, logged
    [[nodiscard]] std::optional<std::string> CreateScene(std::string_view folder);
    // Named as Unity names a copy: Name 1. Each file copied gets its own UUID and keeps its settings
    [[nodiscard]] std::optional<std::string> DuplicateAsset(std::string_view path);
    // Chosen in a dialog at the next Update, so it never blocks mid-frame. Numbered if its name is used
    void RequestImportAsset(std::string folder);
    // Cooks everything from the file or folder again. Refused, logged, while imports run
    bool ReimportAssets(std::string_view path);
    [[nodiscard]] bool IsImportingAssets() const { return IsImportRunning() || m_ImportAgain; }
    // The project's default, whose first map Play enables, saved at once. Other asset types are refused
    void SetDefaultInputActions(Trinity::UUID asset);
    // With starter Move, Look and Jump and the UI map. The project's default while it has none
    [[nodiscard]] std::optional<std::string> CreateInputActions(std::string_view folder);
    // From its file the first time and again after each scan, so an outside edit is seen. Null if none
    [[nodiscard]] const InputActionsFile* ReadInputActions(Trinity::UUID asset);
    // Written only once it reads back. The asset reloads, so a scene playing reads it next frame
    bool SaveInputActions(Trinity::UUID asset, const Trinity::InputActionsData& data);
    // One undo step, named by what. Edits of one field in a row merge. Taken while a scene plays
    bool EditInputActions(Trinity::UUID asset, const Trinity::InputActionsData& before, const Trinity::InputActionsData& after, std::string_view what, std::string field = {});
    // A rename also rewrites the scene's fields that named it. Refused while a scene plays, if any do
    bool RenameInputActions(Trinity::UUID asset, const Trinity::InputActionsData& before, const Trinity::InputActionsData& after, std::string_view what);
    // Everything Forge plays goes through it, saved at once. Other asset types are refused
    void SetProjectMixer(Trinity::UUID asset);
    // Master alone at 0 dB, named New Audio Mixer. The project's mixer while it has none
    [[nodiscard]] std::optional<std::string> CreateAudioMixer(std::string_view folder);
    // From its file the first time and again after each scan, so an outside edit is seen. Null if none
    [[nodiscard]] const AudioMixerFile* ReadAudioMixer(Trinity::UUID asset);
    // Written only once it reads back. The asset reloads, so the mix is heard as saved this frame
    bool SaveAudioMixer(Trinity::UUID asset, const Trinity::AudioMixerData& data);
    // As EditInputActions. Refused, logged, for data that breaks a mixer's rules
    bool EditAudioMixer(Trinity::UUID asset, const Trinity::AudioMixerData& before, const Trinity::AudioMixerData& after, std::string_view what, std::string field = {});
    [[nodiscard]] const MixerSelection& GetMixerSelection() const { return m_MixerSelection; }
    void SetMixerSelection(const MixerSelection& selection) { m_MixerSelection = selection; }
    // Unity's Edit in Play Mode: the mixer is edited while playing only with it on. Off at each Play
    [[nodiscard]] bool IsEditingMixerInPlay() const { return m_EditMixerInPlay; }
    void SetEditMixerInPlay(bool edit) { m_EditMixerInPlay = edit; }
    // False only while the mixer is the project's and a scene plays with Edit in Play Mode off
    [[nodiscard]] bool IsMixerEditable(Trinity::UUID asset) const;
    // A Random named Root with nothing in it, named New Sound Event. Its path, or nothing, logged
    [[nodiscard]] std::optional<std::string> CreateSoundEvent(std::string_view folder);
    // A document and a style sheet, named New UI Document and New UI Style Sheet. The document's path
    [[nodiscard]] std::optional<std::string> CreateUIDocument(std::string_view folder);
    [[nodiscard]] std::optional<std::string> CreateUIStyleSheet(std::string_view folder);
    // As ReadAudioMixer, for the Sound Event panel. Null for an asset that is not one
    [[nodiscard]] const SoundEventFile* ReadSoundEvent(Trinity::UUID asset);
    // Written only once it reads back. Instances playing move to it as their containers next trigger
    bool SaveSoundEvent(Trinity::UUID asset, const Trinity::SoundEventData& data);
    // As EditAudioMixer. Refused, logged, for data that breaks an event's rules
    bool EditSoundEvent(Trinity::UUID asset, const Trinity::SoundEventData& before, const Trinity::SoundEventData& after, std::string_view what, std::string field = {});
    [[nodiscard]] const SoundEventSelection& GetSoundEventSelection() const { return m_SoundEventSelection; }
    void SetSoundEventSelection(const SoundEventSelection& selection) { m_SoundEventSelection = selection; }
    // What the Sound Event panel plays events through, which stops as the project closes
    [[nodiscard]] SoundEventAudition& GetAudition() { return m_Audition; }


    // Forge's settings for the person using it, saved once nothing is being dragged or typed into
    [[nodiscard]] const EditorPreferences& GetPreferences() const { return m_Preferences; }
    void SetPreferences(const EditorPreferences& preferences);
    // One of the console variables kept with them, which the command line no longer holds once set
    void SetPreferenceVariable(std::string_view name, std::string_view value);
    // Whether Preferences.txt exists. Until it does, imgui.ini's older settings are read into it
    [[nodiscard]] bool HasPreferencesFile() const { return m_HasPreferencesFile; }

    // What scripts open in, which the preferences keep
    [[nodiscard]] ScriptEditor GetScriptEditor() const { return m_Preferences.Editor; }
    void SetScriptEditor(ScriptEditor editor);
    // At its line, from 1, or its top for 0. A project path, one from the project's folder, or native
    bool OpenScript(std::string_view path, std::uint32_t line = 0);
    // As Unity's Edit Script: the file named after the class, or else the first that declares it
    bool EditScript(Trinity::ScriptLanguage language, std::string_view className);
    // The C# solution beside the project file, or the C++ module's Source folder, in the script editor
    bool OpenSolution();
    bool OpenModuleFolder();
    // The script editor's debugger on this Forge, with the C# solution open, or the module's folder
    bool AttachDebugger();
    // While it waits, a popup shows Forge's process ID. It plays once one attaches, or on Play Now
    [[nodiscard]] bool GetWaitForDebugger() const { return m_Preferences.WaitForDebugger; }
    void SetWaitForDebugger(bool wait);
    // Moved to at once, or the system's default for an empty name. Kept even when it will not open
    void SetAudioDevice(std::string_view name);
    [[nodiscard]] bool IsWaitingForDebugger() const { return m_DebuggerWait.has_value(); }
    // Its own file, or for a model's material the differing fields in the model's .meta, and its cook
    bool WriteMaterial(Trinity::UUID material, const Trinity::MaterialData& data);
    // Through the history, so it undoes in one step
    bool EditMaterial(Trinity::UUID material, const Trinity::MaterialData& before, const Trinity::MaterialData& after, std::string field);
    // As its file holds it, with a model's overrides. Nothing when it cannot be read
    [[nodiscard]] std::optional<Trinity::MaterialData> ReadMaterial(Trinity::UUID material) const;
    // A model's material as it was imported, before its overrides. Nothing for a material of its own
    [[nodiscard]] std::optional<Trinity::MaterialData> ReadImportedMaterial(Trinity::UUID material) const;
    // A model's material's key, or nothing for a material of its own
    [[nodiscard]] std::optional<std::string> GetMaterialKey(Trinity::UUID material) const;
    // The entity becomes its first instance, keeping every UUID. Undo leaves the file, as in Unity
    [[nodiscard]] std::optional<std::string> CreatePrefab(Trinity::UUID entity, std::string_view folder, PrefabKind kind = PrefabKind::Original);
    // As Unity's Create > Prefab Variant: its root an instance that changes nothing. Not undoable
    [[nodiscard]] std::optional<std::string> CreatePrefabVariant(Trinity::UUID prefab);
    // As Unity's Create > Prefab: a root alone with its transform. Not undoable. Its path, or nothing
    [[nodiscard]] std::optional<std::string> CreateEmptyPrefab(std::string_view folder, std::string_view name);
    // In one command. At the position if given, else the prefab's transform. Refused if it nests itself
    bool InstantiatePrefab(Trinity::UUID prefab, Trinity::UUID parent, Trinity::UUID before, std::optional<glm::vec3> position);
    // The template the scene's instances of the prefab are made from. Null as the serializer finds none
    [[nodiscard]] Trinity::Scene* FindPrefabTemplate(Trinity::UUID prefab) { return m_Prefabs.Find(prefab, m_Registry.get()); }
    // Changes each time the prefab's template is made again. 0 until FindPrefabTemplate finds it
    [[nodiscard]] std::uint64_t GetPrefabTemplateVersion(Trinity::UUID prefab) const { return m_Prefabs.GetVersion(prefab); }
    // Whether its root is an instance of another prefab. False for one missing or unreadable
    [[nodiscard]] bool IsPrefabVariant(Trinity::UUID prefab);
    // Whether the asset is a model, which opens in no prefab mode and takes nothing applied to it
    [[nodiscard]] bool IsModel(Trinity::UUID asset) const;
    // Its file's name without the extension, as Enemy, or its UUID for one the project no longer has
    [[nodiscard]] std::string GetPrefabName(Trinity::UUID prefab) const;
    // In one command. what names the changes, and subject is the entity shown. Not while a copy plays
    bool RevertPrefabOverrides(Trinity::UUID root, std::vector<Trinity::SceneSerializer::PrefabOverride> overrides, std::string_view what, Trinity::UUID subject);
    // Level 0 is the instance's prefab. Files are written at once. Refused, noticed, if a file is gone
    bool ApplyPrefabOverrides(Trinity::UUID root, std::vector<Trinity::SceneSerializer::PrefabOverride> overrides, std::string_view what, Trinity::UUID subject, std::size_t level = 0);
    // As Unity's Unpack, keeping every UUID, or Unpack Completely. Refused for a missing prefab
    bool UnpackPrefab(Trinity::UUID root, bool completely = false);
    // As Unity's prefab mode, with its own selection and history. Opened at the next Update
    void RequestOpenPrefab(Trinity::UUID prefab);
    // Back to the scene, after asking what to do with unsaved changes, which Cancel keeps open
    void RequestClosePrefab();
    // The prefab prefab mode has open, or none
    [[nodiscard]] Trinity::UUID GetOpenPrefab() const { return m_Stage ? m_Stage->Prefab : Trinity::UUID(); }
    [[nodiscard]] bool IsPrefabDirty() const { return m_Stage && !m_Stage->History.IsSaved(); }
    // Its file's name without the extension. Empty while none is open
    [[nodiscard]] std::string GetOpenPrefabName() const;
    // Keeps its place in the world, in one command. Refused with a notice for a prefab's own entity
    bool MoveEntity(Trinity::UUID entity, Trinity::UUID parent, Trinity::UUID before);
    // Just after the original, in one command. Refused with a notice if it holds a missing prefab
    bool DuplicateEntity(Trinity::UUID entity);
    // In one command, as the Delete key and the Hierarchy's menu ask
    bool DeleteEntity(Trinity::UUID entity);
    // The same for every selected entity that no other selected one holds, as one command to undo
    bool DuplicateSelection();
    bool DeleteSelection();
    // A sprite of the texture at the point, its size in units, in one command
    bool CreateSprite(std::string name, Trinity::UUID texture, glm::vec3 position, glm::vec2 size);
    // Plays the clip or event as the scene starts. At the point if given, else at the parent's origin
    bool CreateAudioSource(std::string name, Trinity::UUID resource, Trinity::UUID parent, Trinity::UUID before, std::optional<glm::vec3> position, bool spatialize);
    // At the parent's origin, else the spawn point. In prefab mode it goes under the prefab's root
    bool CreatePrimitive(Trinity::BuiltInMesh mesh, Trinity::UUID parent);
    // As Unity's GameObject menu makes them, each in one command and placed as a built-in shape is
    bool CreateEmpty(Trinity::UUID parent);
    bool CreateLight(Trinity::LightType type, Trinity::UUID parent);
    // Perspective while the Scene panel shows the scene in 3D, and orthographic in 2D
    bool CreateCamera(Trinity::UUID parent);
    // As it is now, to paste until the project closes. Refused with a notice, as Duplicate is
    void CopyEntity(Trinity::UUID entity);
    [[nodiscard]] bool HasCopiedEntity() const { return m_CopiedEntity.has_value(); }
    // After the original, else under its old parent, else among the roots. Not while a copy plays
    bool PasteEntity();
    // Last under the parent
    bool PasteEntityAsChild(Trinity::UUID parent);
    // Where the Scene panel's camera looks: the 3D camera's focus, or the 2D camera's centre at Z 0
    [[nodiscard]] glm::vec3 GetSpawnPoint() const { return m_SpawnPoint; }
    [[nodiscard]] bool IsSceneView3D() const { return m_SceneView3D; }
    // Whether the Scene panel starts a scene it has no view of in 3D, as a template asks, once
    [[nodiscard]] std::optional<bool> TakeStartView3D() { return std::exchange(m_StartView3D, std::nullopt); }
    void SetSpawnPoint(glm::vec3 point, bool view3D)
    {
        m_SpawnPoint = point;
        m_SceneView3D = view3D;
    }
    [[nodiscard]] bool IsImporting(Trinity::UUID id) const { return m_Reimporter.IsBusy(id); }
    [[nodiscard]] bool IsImportingModels() const { return m_ModelImports.IsRunning() || m_ImportAgain; }
    [[nodiscard]] bool IsImportingAudioClips() const { return m_AudioClipImports.IsRunning() || m_ImportAgain; }
    // What the Content Browser and Properties play clips and draw waveforms through
    [[nodiscard]] AudioClipPreview& GetAudioPreview() { return m_AudioPreview; }
    // The project's mixer once it has loaded, else Master alone. Null without an audio engine
    [[nodiscard]] const Trinity::AudioMixer* GetMixer() const;
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

    // A scene backup a crash left: its text, the asset path it was opened from, and when it was written
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

    // The folder a new project is made in, and its template, used once its language is chosen
    struct LanguageQuestion
    {
        std::filesystem::path Path;
        // The version's templates, 0 for none and each after it one of them in turn
        std::vector<Trinity::ProjectTemplate> Templates;
        std::size_t Template = 0;
    };

    // Its file, what the file says, the backup it would get, and the language to pick, if it needs one
    struct UpgradeQuestion
    {
        std::filesystem::path File;
        Trinity::ProjectFileInfo Info;
        std::filesystem::path Backup;
        std::optional<Trinity::ScriptLanguage> Language;
    };

    // What Copy took, with where the original was and its place in the world, which a paste keeps
    struct CopiedEntity
    {
        std::string Text;
        Trinity::UUID Original;
        Trinity::UUID Parent;
        Trinity::TransformComponent World;
    };

    // Its entities in a scene of their own, and its file's text, to find outside changes against
    struct PrefabStage
    {
        Trinity::UUID Prefab;
        std::string Text;
        Trinity::Scene Scene;
        EditorSelection Selection;
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
    // Cache/Forge.lock. None when another Forge has it open. Not held, with a warning, on other errors
    [[nodiscard]] std::optional<Trinity::FileLock> LockProject(const std::filesystem::path& path);
    // The project opened once locked. upgrade backs it up first, then moves it to this version
    bool LoadProject(const std::filesystem::path& path, std::optional<Trinity::ScriptLanguage> language, bool upgrade);
    // Why a project of a newer version or format is not opened, logged
    static void ReportNewer(const Trinity::ProjectFileInfo& info);
    // A new project's C# project and solution, or its C++ module, which builds as the project opens
    void CreateScripts();
    // Once its assets are scanned: its start scene, which is opened, its input actions and its mixer
    void ApplyTemplate(const Trinity::ProjectTemplate& projectTemplate);
    void ReadPreferences(const Trinity::ApplicationCommandLineArgs& args);
    void WritePreferences();
    void ReadRecentProjects();
    void TakeOverRecentProjects();
    void RememberProject();
    void ReadRecentScenes();
    void WriteRecentScenes() const;
    void RememberScene();
    void ReadSceneMarks();
    void WriteSceneMarks();
    bool PasteEntityAt(Trinity::UUID parent, Trinity::UUID before);
    // The selected entities none of whose ancestors is selected too, in the order they were picked
    [[nodiscard]] std::vector<Trinity::UUID> GetSelectedRoots();
    [[nodiscard]] bool CanTakeChildren(Trinity::UUID parent);
    [[nodiscard]] Place FindPlace(Trinity::UUID parent, Trinity::UUID before);
    [[nodiscard]] glm::vec3 ToLocalPoint(Trinity::UUID parent, glm::vec3 point);
    [[nodiscard]] bool IsPrefabRoot(Trinity::UUID entity);
    [[nodiscard]] bool RefuseHoldingPrefab(Trinity::UUID prefab);
    [[nodiscard]] std::optional<std::string> FindUnpackProblem(Trinity::Entity root, bool completely);
    [[nodiscard]] Trinity::Expected<std::size_t, std::string> ReplacePrefabs(std::span<const Trinity::SceneSerializer::PrefabText> texts, const Trinity::Scene* own);
    [[nodiscard]] std::optional<std::size_t> WritePrefabs(std::span<const Trinity::SceneSerializer::PrefabText> texts, const Trinity::Scene* own);
    void UpdateChangedPrefabs();
    void RestoreStandIns();
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
    void FinishAudioClipImports(const AudioClipImportReport& report);
    void UpdateMixer();
    // The UI documents and stylesheets saved since the last frame are read again by the game UI
    void ReloadChangedUIFiles();
    [[nodiscard]] std::optional<std::string> CreateTextAsset(std::string_view folder, std::string_view stem, std::string_view extension, std::string_view text, std::string_view kind);
    void ApplyMixer(const Trinity::AudioMixerAsset* asset);
    void ResetMixer();
    void HoldMixer();
    void SaveProject();
    void ImportAsset(const std::filesystem::path& file, const std::string& folder);
    bool WritePhysicsSettings(const Trinity::PhysicsSettings& settings);
    bool WriteAudioSettings(const Trinity::AudioSettings& settings);
    bool WriteExportSettings(const Trinity::ExportSettings& settings);
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
    void DrawLanguagePopup();
    void DrawUpgradePopup();

    Trinity::Scope<Trinity::Project> m_Project;
    // Held while the project is open, so no second Forge opens it beside this one
    Trinity::FileLock m_ProjectLock;
    Trinity::Scope<Trinity::AssetRegistry> m_Registry;
    Trinity::Scene m_Scene;
    Trinity::Scope<Trinity::SceneManager> m_Play;
    bool m_Paused = false;
    bool m_StepRequested = false;
    std::string m_ScenePath;
    EditorSelection m_Selection;
    CommandStack m_History{ m_Scene, m_Selection };
    Trinity::UUID m_InspectedAsset;
    std::optional<CopiedEntity> m_CopiedEntity;
    // The serializer's prefabs, as Forge opens a scene before the asset manager could load them
    PrefabTemplateCache m_Prefabs;
    // The prefab prefab mode has open, or null while the scene is edited
    Trinity::Scope<PrefabStage> m_Stage;
    // Hub's list of projects, which every Forge adds to, and the start screen shows the ten newest of
    std::filesystem::path m_ProjectListFile;
    std::vector<RecentProject> m_RecentProjects;
    std::vector<RecentScene> m_RecentScenes;
    // What the open scene's entities are marked with in the Hierarchy's gutter
    std::unordered_set<Trinity::UUID> m_Hidden;
    std::unordered_set<Trinity::UUID> m_Locked;
    std::unordered_map<Trinity::UUID, InputActionsFile> m_InputActionsFiles;
    std::unordered_map<Trinity::UUID, AudioMixerFile> m_AudioMixerFiles;
    std::unordered_map<Trinity::UUID, SoundEventFile> m_SoundEventFiles;
    SoundEventSelection m_SoundEventSelection;
    glm::vec3 m_SpawnPoint{ 0.0f };
    bool m_SceneView3D = false;
    std::optional<bool> m_StartView3D;
    TextureReimporter m_Reimporter;
    TextureImportBatch m_Imports;
    ModelImportBatch m_ModelImports;
    EnvironmentImportBatch m_EnvironmentImports;
    AudioClipImportBatch m_AudioClipImports;
    AudioClipPreview m_AudioPreview;
    SoundEventAudition m_Audition;
    // The asset the project names, the scan it was last looked for in, and the version applied
    Trinity::UUID m_MixerID;
    Trinity::AssetRef<Trinity::AudioMixerAsset> m_Mixer;
    std::uint64_t m_MixerScan = 0;
    std::uint64_t m_MixerSerial = 0;
    MixerSelection m_MixerSelection;
    bool m_EditMixerInPlay = false;
    bool m_InspectingMixer = false;
    ModuleHost m_Module;
    AssemblyHost m_Assembly;
    // The project's Assets, watched for UI files saved in other programs
    Trinity::DirectoryWatcher m_AssetWatcher;
    EditorPreferences m_Preferences;
    // Console variables the command line set, each with the file's value, which is written back
    std::map<std::string, std::string, std::less<>> m_HeldVariables;
    bool m_HasPreferencesFile = false;
    bool m_PreferencesChanged = false;
    std::optional<DebuggerWait> m_DebuggerWait;
    bool m_OpenDebuggerPopup = false;
    bool m_PlayAfterBuild = false;
    std::optional<Notice> m_Notice;
    // Whether this session wrote the backup in the cache, which it removes once the scene is saved
    bool m_HasBackup = false;
    std::optional<Recovery> m_Recovery;
    bool m_OpenRecoveryPopup = false;
    std::optional<LanguageQuestion> m_LanguageQuestion;
    bool m_OpenLanguagePopup = false;
    std::optional<UpgradeQuestion> m_UpgradeQuestion;
    bool m_OpenUpgradePopup = false;
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
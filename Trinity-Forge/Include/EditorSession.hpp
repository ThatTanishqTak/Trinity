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
#include "Modules/ModuleHost.hpp"

#include "Trinity.hpp"

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

    // Play runs a copy of the edited scene, made through the serializer, and Stop throws the copy away, so the edited scene, its history and whether it is saved are as they were. Pause holds the copy where it is, and Step takes one fixed step and holds it there. The history is locked while playing. Each is asked for mid-frame and done at the next Update, and any command but Refresh and the builds stops playing first, since a build reloads the module while the scene plays on
    void RequestPlay();
    void RequestPause();
    void RequestStep();
    void RequestStop();
    // With the frame's time, after the session's own work
    void UpdatePlay(Trinity::Timestep timestep);
    [[nodiscard]] bool IsPlaying() const { return m_Runtime != nullptr; }
    [[nodiscard]] bool IsPaused() const { return m_Runtime != nullptr && m_Paused; }
    [[nodiscard]] Trinity::SceneRuntime* GetRuntime() { return m_Runtime.get(); }
    [[nodiscard]] bool IsDirty() const { return !m_History.IsSaved(); }

    // The scene the panels show: the running copy while playing, and otherwise the edited scene
    [[nodiscard]] Trinity::Scene& GetScene() { return m_PlayScene ? *m_PlayScene : m_Scene; }
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

    void RequestOpenScene(std::string assetPath);
    [[nodiscard]] std::optional<std::string> CreateFolder(std::string_view parent);
    bool MoveAsset(std::string_view path, std::string_view folder, std::string_view name);
    bool DeleteAsset(std::string_view path);
    bool ApplyImportSettings(Trinity::UUID id, Trinity::AssetSettings settings);
    [[nodiscard]] std::optional<std::string> CreateMaterial(std::string_view folder);
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

    void Run(Command command);
    void AskPath(PathRequest request);
    void ConfirmDiscard(Action action);

    void CloseProject();
    void AttachRegistry();
    void StartImports();
    void StartTextureImports();
    void ImportAgainIfAsked();
    [[nodiscard]] bool IsImportRunning() const;
    void FinishImports(const TextureImportReport& report);
    void FinishModelImports(const ModelImportReport& report);
    void FinishEnvironmentImports(const EnvironmentImportReport& report);
    void SaveProject();
    bool WritePhysicsSettings(const Trinity::PhysicsSettings& settings);
    void StartPlay();
    void StopPlay();
    void NewScene();
    void OpenScene(const std::string& assetPath);
    void SaveScene(Action then);
    void SaveSceneAs(Action then);
    [[nodiscard]] bool WriteScene(const std::string& assetPath);
    [[nodiscard]] std::string GetSceneName() const;

    void DrawUnsavedPopup();
    void DrawPathPopup();
    void DrawImportPopup();

    Trinity::Scope<Trinity::Project> m_Project;
    Trinity::Scope<Trinity::AssetRegistry> m_Registry;
    Trinity::Scene m_Scene;
    Trinity::Scope<Trinity::Scene> m_PlayScene;
    Trinity::Scope<Trinity::SceneRuntime> m_Runtime;
    bool m_Paused = false;
    bool m_StepRequested = false;
    std::string m_ScenePath;
    Trinity::UUID m_Selection;
    CommandStack m_History{ m_Scene, m_Selection };
    Trinity::UUID m_InspectedAsset;
    glm::vec3 m_SpawnPoint{ 0.0f };
    TextureReimporter m_Reimporter;
    TextureImportBatch m_Imports;
    ModelImportBatch m_ModelImports;
    EnvironmentImportBatch m_EnvironmentImports;
    ModuleHost m_Module;
    AssemblyHost m_Assembly;
    bool m_TexturesAfterModels = false;
    bool m_ImportAgain = false;
    bool m_ImportPopupOpen = false;
    std::vector<std::pair<std::uint64_t, std::move_only_function<void()>>> m_CloseListeners;
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
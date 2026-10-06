#pragma once

#include "CommandStack.hpp"
#include "Importers/TextureImportBatch.hpp"
#include "Importers/TextureImporter.hpp"
#include "Importers/TextureReimporter.hpp"

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
        Exit
    };

    EditorSession() = default;
    ~EditorSession();

    EditorSession(const EditorSession&) = delete;
    EditorSession& operator=(const EditorSession&) = delete;

    void Start(const Trinity::ApplicationCommandLineArgs& args);
    void CreateProject(const std::filesystem::path& directory);
    void OpenProject(const std::filesystem::path& path);
    void ScanAssets();
    TextureImportReport WaitForImports();

    void Request(Command command);
    void Update();
    void DrawPopups();
    void UpdateTitle();

    [[nodiscard]] bool RequestClose();
    [[nodiscard]] bool IsDirty() const { return !m_History.IsSaved(); }

    [[nodiscard]] Trinity::Scene& GetScene() { return m_Scene; }
    [[nodiscard]] CommandStack& GetHistory() { return m_History; }
    [[nodiscard]] const Trinity::Project* GetProject() const { return m_Project.get(); }
    [[nodiscard]] const Trinity::AssetRegistry* GetRegistry() const { return m_Registry.get(); }
    [[nodiscard]] bool HasProject() const { return m_Project != nullptr; }
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
    [[nodiscard]] bool IsImporting(Trinity::UUID id) const { return m_Reimporter.IsBusy(id); }
    [[nodiscard]] std::uint64_t GetScanCount() const { return m_ScanCount; }

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
    void FinishImports(const TextureImportReport& report);
    void SaveProject();
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
    std::string m_ScenePath;
    Trinity::UUID m_Selection;
    CommandStack m_History{ m_Scene, m_Selection };
    Trinity::UUID m_InspectedAsset;
    TextureReimporter m_Reimporter;
    TextureImportBatch m_Imports;
    bool m_ImportAgain = false;
    bool m_ImportPopupOpen = false;
    std::vector<std::pair<std::uint64_t, std::move_only_function<void()>>> m_CloseListeners;
    std::uint64_t m_NextCloseListener = 1;
    std::uint64_t m_ScanCount = 0;

    std::vector<Action> m_Pending;
    Action m_AfterDiscard;
    bool m_OpenUnsavedPopup = false;

    std::optional<PathRequest> m_PathPrompt;
    std::string m_PathText;
    bool m_OpenPathPopup = false;

    std::string m_Title;
};
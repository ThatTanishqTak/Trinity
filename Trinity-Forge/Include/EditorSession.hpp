#pragma once

#include <Trinity.hpp>

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
        Exit
    };

    void Start(const Trinity::ApplicationCommandLineArgs& args);

    void Request(Command command);
    void Update();
    void DrawPopups();
    void UpdateTitle();

    [[nodiscard]] bool RequestClose();
    void MarkDirty() { m_Dirty = true; }

    [[nodiscard]] Trinity::Scene& GetScene() { return m_Scene; }
    [[nodiscard]] bool HasProject() const { return m_Project != nullptr; }
    [[nodiscard]] bool HasScenePath() const { return !m_ScenePath.empty(); }

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

    void CreateProject(const std::filesystem::path& directory);
    void OpenProject(const std::filesystem::path& path);
    void NewScene();
    void OpenScene(const std::string& assetPath);
    void SaveScene(Action then);
    void SaveSceneAs(Action then);
    [[nodiscard]] bool WriteScene(const std::string& assetPath);
    [[nodiscard]] std::string GetSceneName() const;

    void DrawUnsavedPopup();
    void DrawPathPopup();

    Trinity::Scope<Trinity::Project> m_Project;
    Trinity::Scene m_Scene;
    std::string m_ScenePath;
    bool m_Dirty = false;

    std::vector<Action> m_Pending;
    Action m_AfterDiscard;
    bool m_OpenUnsavedPopup = false;

    std::optional<PathRequest> m_PathPrompt;
    std::string m_PathText;
    bool m_OpenPathPopup = false;

    std::string m_Title;
};
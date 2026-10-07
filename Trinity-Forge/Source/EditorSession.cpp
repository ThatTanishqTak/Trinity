#include "EditorSession.hpp"

#include "EditorCommands.hpp"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <optional>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
    constexpr std::string_view c_ApplicationTitle = "Trinity Forge";
    constexpr const char* c_UnsavedPopup = "Unsaved Changes###ForgeUnsaved";
    constexpr const char* c_PathPopup = "Choose a Path###ForgePath";
    constexpr const char* c_ImportPopup = "Importing Textures###ForgeImport";
    constexpr std::string_view c_SceneExtension = ".trscene";

    constexpr std::array<Trinity::FileDialogFilter, 1> c_ProjectFilters{ { { "Trinity project", "trproj" } } };
    constexpr std::array<Trinity::FileDialogFilter, 1> c_SceneFilters{ { { "Trinity scene", "trscene" } } };

    // A batch that finishes sooner, or finds every texture in the cache, never shows its popup
    constexpr double c_ImportPopupDelay = 0.3;

    // An estimate for the texture being encoded stops short of it, since the encoder reports nothing until it is done
    constexpr double c_MaximumEstimate = 0.95;

    std::string FormatDuration(double seconds)
    {
        const long long l_Seconds = std::max(0ll, std::llround(seconds));

        return std::format("{}:{:02}", l_Seconds / 60, l_Seconds % 60);
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
        else if (record.Importer == ModelImporter::c_MaterialImporter)
        {
            l_Paths = { Trinity::GetCookedMaterialPath(record.ID) };
        }
        else if (record.Importer == ModelImporter::c_Importer)
        {
            l_Paths = { Trinity::GetCookedModelPath(record.ID), ModelImporter::GetCacheKeyPath(record.ID) };
        }

        for (const std::string& it_Path : l_Paths)
        {
            static_cast<void>(Trinity::FileSystem::RemoveFile(it_Path));
        }
    }
}

// Before the asset manager shuts down, which must not be left holding this project's registry
EditorSession::~EditorSession()
{
    CloseProject();
}

void EditorSession::Start(const Trinity::ApplicationCommandLineArgs& args)
{
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

    ImportAgainIfAsked();

    std::vector<Action> l_Pending = std::exchange(m_Pending, {});
    for (Action& it_Action : l_Pending)
    {
        if (it_Action)
        {
            it_Action();
        }
    }
}

void EditorSession::DrawPopups()
{
    DrawUnsavedPopup();
    DrawPathPopup();
    DrawImportPopup();
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

bool EditorSession::RequestClose()
{
    if (!IsDirty())
    {
        return true;
    }

    ConfirmDiscard([] { Trinity::Application::Get().Close(); });

    return false;
}

void EditorSession::Run(Command command)
{
    const bool l_HasProject = m_Project != nullptr;
    switch (command)
    {
        case Command::NewProject:
        {
            ConfirmDiscard([this]
            {
                AskPath({ PathKind::Folder, "Choose an empty folder for the new project", {}, {}, {}, [this](const std::filesystem::path& path) { CreateProject(path); } });
            });
            break;
        }
        case Command::OpenProject:
        {
            ConfirmDiscard([this]
            {
                AskPath({ PathKind::OpenFile, "Open Project", c_ProjectFilters, m_Project ? m_Project->GetDirectory().parent_path() : std::filesystem::path(), {}, [this](const std::filesystem::path& path) { OpenProject(path); } });
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
                    AskPath({ PathKind::OpenFile, "Open Scene", c_SceneFilters, m_Project->GetAssetsDirectory(), {}, [this](const std::filesystem::path& path)
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

    const std::string l_StartScene = std::format("{}/{}", Trinity::Project::c_AssetsMount, Trinity::Project::c_DefaultStartScene);
    if (!WriteScene(l_StartScene))
    {
        return;
    }

    m_ScenePath = l_StartScene;
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
    // The texture being encoded is finished, since the encoder cannot leave it halfway, and the rest wait for the project to open again
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

    m_Reimporter.Finish();
    m_InspectedAsset = {};

    // Everything holding the project's assets lets go of them, so none are left loaded from it when another project's registry is set
    for (auto& [it_ID, it_Listener] : m_CloseListeners)
    {
        it_Listener();
    }

    Trinity::Application::Get().GetRenderer().GetRenderer2D().ReleaseTextures();

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
    Trinity::AssetManager::SetRegistry(m_Registry.get());
    ScanAssets();
}

// Every texture and model the scan finds is cooked into /cache in the background, unless the cache already holds it for the same files and settings
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

    StartImports();
}

// Until every import the scans so far asked for is over, with the last report of each kind. For tests, which need the cache filled before they go on, so models are cooked before textures start here
EditorSession::ImportReport EditorSession::WaitForImports()
{
    ImportReport l_Report;
    while (m_ModelImports.IsRunning() || m_Imports.IsRunning() || m_TexturesAfterModels || (m_ImportAgain && m_Registry))
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

        ImportAgainIfAsked();
    }

    return l_Report;
}

// A scan while imports run imports again once they are over, since each batch has the records it started with. Models go first, and textures once the models are planned, so a texture beside a model is encoded the way the model uses it
void EditorSession::StartImports()
{
    if (m_Imports.IsRunning() || m_ModelImports.IsRunning() || m_TexturesAfterModels)
    {
        m_ImportAgain = true;

        return;
    }

    std::vector<Trinity::AssetRecord> l_Models;
    for (const Trinity::AssetRecord* it_Record : m_Registry->GetRecords())
    {
        if (it_Record->Importer == ModelImporter::c_Importer)
        {
            l_Models.push_back(*it_Record);
        }
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
    if (m_ImportAgain && m_Registry && !m_Imports.IsRunning() && !m_ModelImports.IsRunning() && !m_TexturesAfterModels)
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

void EditorSession::FinishModelImports(const ModelImportReport& report)
{
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
    m_Pending.push_back([this, l_Path = std::move(assetPath)]() mutable { ConfirmDiscard([this, l_Path = std::move(l_Path)] { OpenScene(l_Path); }); });
}

// Named New Folder, or New Folder 1 and on when that is taken. An empty folder has no assets, so the registry need not scan
std::optional<std::string> EditorSession::CreateFolder(std::string_view parent)
{
    if (!m_Project)
    {
        return std::nullopt;
    }

    std::string l_Path;
    for (std::uint32_t it_Index = 0; l_Path.empty() || std::filesystem::exists(m_Project->ToNativePath(l_Path)); ++it_Index)
    {
        l_Path = it_Index == 0 ? std::format("{}/New Folder", parent) : std::format("{}/New Folder {}", parent, it_Index);
    }

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

// Into the .meta at once, and then encoded in the background. Sprites keep the old texture until the new one has loaded
bool EditorSession::ApplyImportSettings(Trinity::UUID id, Trinity::AssetSettings settings)
{
    if (!m_Registry || !m_Registry->SetSettings(id, std::move(settings)))
    {
        return false;
    }

    const Trinity::AssetRecord* l_Record = m_Registry->Find(id);
    TR_INFO("Forge: reimporting {} with its new settings", l_Record->Path);
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

    return m_History.Execute(Trinity::CreateScope<CreateModelCommand>(model, ToUtf8(FromUtf8(l_Record->Path).stem()), parent, before, position));
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
    m_ScenePath.clear();
    m_Selection = {};
    m_History.Clear();
}

// On an error the scene is left as it was
void EditorSession::OpenScene(const std::string& assetPath)
{
    const Trinity::Expected<void, std::string> l_Loaded = Trinity::SceneSerializer::Load(m_Scene, assetPath);
    if (!l_Loaded)
    {
        TR_ERROR("Forge: the scene could not be opened: {}", l_Loaded.GetError());

        return;
    }

    m_ScenePath = assetPath;
    m_Selection = {};
    m_History.Clear();
    TR_INFO("Forge: opened scene {} with {} entities", m_ScenePath, m_Scene.GetEntityCount());
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

    // The texture being encoded counts by the rate the encoder has kept so far. Before there is one, the bar only shows that work goes on
    const std::optional<double> l_Rate = l_Progress.TexelsPerSecond;
    const double l_Total = static_cast<double>(l_Progress.TotalTexels);
    const double l_Current = l_Rate && l_Progress.Encoding != 0 ? std::min(l_Progress.CurrentSeconds * *l_Rate, static_cast<double>(l_Progress.CurrentTexels) * c_MaximumEstimate) : 0.0;
    const double l_Done = static_cast<double>(l_Progress.DoneTexels) + l_Current;
    const bool l_Known = l_Progress.Planned && l_Total > 0.0 && (l_Rate || l_Progress.Encoding == 0);
    const float l_Fraction = l_Known ? static_cast<float>(std::clamp(l_Done / l_Total, 0.0, 1.0)) : -static_cast<float>(ImGui::GetTime());
    const std::string l_Percent = l_Known ? std::format("{:.0f}%", l_Fraction * 100.0f) : std::string();
    ImGui::ProgressBar(l_Fraction, ImVec2(ImGui::GetFontSize() * 26.0f, 0.0f), l_Known ? l_Percent.c_str() : "");

    const std::string l_Time = l_Known && l_Rate ? std::format("{} elapsed, about {} left", FormatDuration(l_Progress.ElapsedSeconds), FormatDuration((l_Total - l_Done) / *l_Rate)) : std::format("{} elapsed, estimating the time left", FormatDuration(l_Progress.ElapsedSeconds));
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
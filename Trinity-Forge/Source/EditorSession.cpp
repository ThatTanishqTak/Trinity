#include "EditorSession.hpp"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <array>
#include <format>
#include <utility>

namespace
{
    constexpr std::string_view c_ApplicationTitle = "Trinity Forge";
    constexpr const char* c_UnsavedPopup = "Unsaved Changes###ForgeUnsaved";
    constexpr const char* c_PathPopup = "Choose a Path###ForgePath";
    constexpr std::string_view c_SceneExtension = ".trscene";

    constexpr std::array<Trinity::FileDialogFilter, 1> c_ProjectFilters{ { { "Trinity project", "trproj" } } };
    constexpr std::array<Trinity::FileDialogFilter, 1> c_SceneFilters{ { { "Trinity scene", "trscene" } } };

    std::filesystem::path FromUtf8(std::string_view text)
    {
        return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size()));
    }

    std::string ToUtf8(const std::filesystem::path& path)
    {
        const std::u8string l_Text = path.u8string();

        return std::string(reinterpret_cast<const char*>(l_Text.data()), l_Text.size());
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
}

// The project, the scene and an asterisk while unsaved. The renderer adds the backend and frame rate
void EditorSession::UpdateTitle()
{
    const std::string l_Title = m_Project ? std::format("{} - {}{} - {}", m_Project->GetName(), GetSceneName(), m_Dirty ? "*" : "", c_ApplicationTitle) : std::string(c_ApplicationTitle);
    if (l_Title != m_Title)
    {
        m_Title = l_Title;
        Trinity::Application::Get().GetRenderer().SetTitle(m_Title);
    }
}

bool EditorSession::RequestClose()
{
    if (!m_Dirty)
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
                static_cast<void>(m_Registry->Scan());
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
    if (!m_Dirty)
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
    static_cast<void>(m_Registry->Scan());
    Trinity::AssetManager::SetRegistry(m_Registry.get());
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
    m_Dirty = false;
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
    m_Dirty = false;
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

    m_Dirty = false;
    TR_INFO("Forge: saved scene {} with {} entities", assetPath, m_Scene.GetEntityCount());

    // A scene saved under a new name gets its .meta and UUID now
    if (m_Registry && m_Registry->FindByPath(assetPath) == nullptr)
    {
        static_cast<void>(m_Registry->Scan());
    }

    return true;
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
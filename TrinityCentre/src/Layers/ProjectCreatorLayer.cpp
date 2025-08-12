#include "Trinity/trpch.h"
#include "ProjectCreatorLayer.h"

#include <imgui.h>

#include <ImGuiFileDialog.h>
#include <filesystem>
#include <string>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <system_error>
#include <array>

#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif

namespace
{
    void LaunchEditor(const std::filesystem::path& p_ProjectPath)
    {
#ifdef _WIN32
        STARTUPINFOA l_SI{};
        PROCESS_INFORMATION l_PI{};
        l_SI.cb = sizeof(STARTUPINFOA);

        std::string l_Command = "TrinityForge.exe";
        if (CreateProcessA(nullptr, l_Command.data(), nullptr, nullptr, FALSE, 0, nullptr, p_ProjectPath.string().c_str(), &l_SI, &l_PI))
        {
            CloseHandle(l_PI.hProcess);
            CloseHandle(l_PI.hThread);
        }
#else
        pid_t l_PID = fork();
        if (l_PID == 0)
        {
            chdir(p_ProjectPath.string().c_str());
            execl("TrinityForge", "TrinityForge", static_cast<char*>(nullptr));
            std::_Exit(EXIT_FAILURE);
        }
#endif
    }

    std::filesystem::path FindDefaultAssetsRoot()
    {
        auto l_Path = std::filesystem::current_path();
        for (int l_Level = 0; l_Level < 5; ++l_Level)
        {
            auto l_Candidate = l_Path / "TrinityForge" / "Resources" / "DefaultAssets";
            if (std::filesystem::exists(l_Candidate))
            {
                return l_Candidate;
            }
            if (!l_Path.has_parent_path())
            {
                break;
            }
            l_Path = l_Path.parent_path();
        }
        return {};
    }

    std::error_code CreateDefaultAssets(const std::filesystem::path& p_ProjectPath)
    {
        std::error_code l_EC;
        std::filesystem::path l_SourceRoot = FindDefaultAssetsRoot();
        std::filesystem::path l_TargetRoot = p_ProjectPath / "Resources" / "DefaultAssets";

        if (l_SourceRoot.empty())
        {
            return std::make_error_code(std::errc::no_such_file_or_directory);
        }

        std::filesystem::create_directories(l_TargetRoot, l_EC);
        if (l_EC)
        {
            return l_EC;
        }

        const std::array<std::string, 4> l_Folders{ "Icons", "Meshes", "Shaders", "Textures" };
        for (const auto& l_Folder : l_Folders)
        {
            std::filesystem::path l_Source = l_SourceRoot / l_Folder;
            std::filesystem::path l_Target = l_TargetRoot / l_Folder;

            std::filesystem::create_directories(l_Target, l_EC);
            if (l_EC)
            {
                return l_EC;
            }

            if (std::filesystem::exists(l_Source))
            {
                std::filesystem::copy(l_Source, l_Target, std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing, l_EC);
                if (l_EC)
                {
                    return l_EC;
                }
            }
        }

        return l_EC;
    }
}

ProjectCreatorLayer::ProjectCreatorLayer() : m_Templates{ "Empty", "2D", "3D" }, m_SelectedTemplate(0), m_IsOpen(false), m_ShowMessage(false), m_IsError(false)
{
    m_ProjectName[0] = '\0';
    std::string l_CurrentDir = std::filesystem::current_path().string();
    std::snprintf(m_ProjectDirectory, sizeof(m_ProjectDirectory), "%s", l_CurrentDir.c_str());
}

void ProjectCreatorLayer::Open()
{
    m_IsOpen = true;
    m_ShowMessage = false;
    m_IsError = false;

    m_StatusMessage.clear();
    m_ProjectName[0] = '\0';
}

void ProjectCreatorLayer::Close()
{
    m_IsOpen = false;
}

bool ProjectCreatorLayer::IsOpen() const
{
    return m_IsOpen;
}

void ProjectCreatorLayer::OnUIRender()
{
    if (!m_IsOpen)
    {
        return;
    }

    ImGui::Begin("Project Creator", &m_IsOpen);

    ImGui::InputText("Project Name", m_ProjectName, sizeof(m_ProjectName));

    ImGui::InputText("Directory", m_ProjectDirectory, sizeof(m_ProjectDirectory));
    ImGui::SameLine();
    if (ImGui::Button("Browse"))
    {
        ImGuiFileDialog::Instance()->OpenDialog("ChooseDir", "Choose Directory", nullptr);
    }

    if (ImGui::BeginCombo("Template", m_Templates[m_SelectedTemplate].c_str()))
    {
        for (int l_Index = 0; l_Index < static_cast<int>(m_Templates.size()); ++l_Index)
        {
            bool l_IsSelected = (m_SelectedTemplate == l_Index);
            if (ImGui::Selectable(m_Templates[l_Index].c_str(), l_IsSelected))
            {
                m_SelectedTemplate = l_Index;
            }

            if (l_IsSelected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    const char* l_NameInvalid = "\\/:?\"<>|*";
    const char* l_DirInvalid = "\"<>|?*";
    bool l_NameValid = m_ProjectName[0] != '\0' && std::strpbrk(m_ProjectName, l_NameInvalid) == nullptr;
    bool l_DirValid = m_ProjectDirectory[0] != '\0' && std::strpbrk(m_ProjectDirectory, l_DirInvalid) == nullptr;
    bool l_CanCreate = l_NameValid && l_DirValid;

    ImGui::BeginDisabled(!l_CanCreate);
    if (ImGui::Button("Create"))
    {
        std::filesystem::path l_ProjectPath = std::filesystem::path(m_ProjectDirectory) / m_ProjectName;
        if (std::filesystem::exists(l_ProjectPath))
        {
            m_StatusMessage = "Error: Project already exists.";
            m_IsError = true;
        }
        else
        {
            std::error_code l_EC;
            if (std::filesystem::create_directories(l_ProjectPath, l_EC))
            {
                std::error_code l_AssetsEC = CreateDefaultAssets(l_ProjectPath);

                if (l_AssetsEC)
                {
                    m_StatusMessage = std::string("Error: ") + l_AssetsEC.message();
                    m_IsError = true;
                }

                else
                {
                    m_StatusMessage = "Project created successfully.";
                    m_IsError = false;

                    LaunchEditor(l_ProjectPath);
                }
            }

            else
            {
                m_StatusMessage = std::string("Error: ") + l_EC.message();
                m_IsError = true;
            }
        }
        m_ShowMessage = true;
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
        Close();
    }

    if (m_ShowMessage)
    {
        ImVec4 l_Color = m_IsError ? ImVec4(1.0f, 0.0f, 0.0f, 1.0f) : ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
        ImGui::TextColored(l_Color, "%s", m_StatusMessage.c_str());
    }

    ImGui::End();

    if (ImGuiFileDialog::Instance()->Display("ChooseDir"))
    {
        if (ImGuiFileDialog::Instance()->IsOk())
        {
            std::string l_Path = ImGuiFileDialog::Instance()->GetCurrentPath();
            std::snprintf(m_ProjectDirectory, sizeof(m_ProjectDirectory), "%s", l_Path.c_str());
        }
        ImGuiFileDialog::Instance()->Close();
    }
}
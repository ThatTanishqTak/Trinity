#include "Trinity/trpch.h"
#include "ProjectLoaderLayer.h"

#include <imgui.h>
#include <ImGuiFileDialog.h>
#include <filesystem>
#include <string>
#include <cstdio>
#include <cstring>
#include <system_error>
#include <cstdlib>

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
}

ProjectLoaderLayer::ProjectLoaderLayer() : m_IsOpen(false), m_ShowMessage(false), m_IsError(false)
{
    std::string l_CurrentDir = std::filesystem::current_path().string();
    std::snprintf(m_ProjectDirectory, sizeof(m_ProjectDirectory), "%s", l_CurrentDir.c_str());
}

void ProjectLoaderLayer::Open()
{
    m_IsOpen = true;
    m_ShowMessage = false;
    m_IsError = false;
    m_StatusMessage.clear();
}

void ProjectLoaderLayer::Close()
{
    m_IsOpen = false;
}

bool ProjectLoaderLayer::IsOpen() const
{
    return m_IsOpen;
}

void ProjectLoaderLayer::OnUIRender()
{
    if (!m_IsOpen)
    {
        return;
    }

    ImGui::Begin("Load Project", &m_IsOpen);

    ImGui::InputText("Directory", m_ProjectDirectory, sizeof(m_ProjectDirectory));
    ImGui::SameLine();
    if (ImGui::Button("Browse"))
    {
        ImGuiFileDialog::Instance()->OpenDialog("ChooseDir", "Choose Directory", nullptr);
    }

    bool l_DirValid = m_ProjectDirectory[0] != '\0';
    ImGui::BeginDisabled(!l_DirValid);
    if (ImGui::Button("Load"))
    {
        std::filesystem::path l_Path = m_ProjectDirectory;
        if (std::filesystem::exists(l_Path))
        {
            LaunchEditor(l_Path);
            m_StatusMessage = "Project loaded successfully.";
            m_IsError = false;
        }
        else
        {
            m_StatusMessage = "Error: Directory does not exist.";
            m_IsError = true;
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
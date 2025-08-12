#include "Trinity/trpch.h"
#include "ProjectLoaderLayer.h"

#include <imgui.h>
#include <filesystem>
#include <string>
#include <cstdio>
#include <cstring>
#include <system_error>
#include <cstdlib>

#ifdef _WIN32
#include <Windows.h>
#include <ShlObj.h>
#else
#include <unistd.h>
#endif

namespace
{
    std::filesystem::path OpenDirectoryDialog()
    {
#ifdef _WIN32
        BROWSEINFOA l_BI{};
        l_BI.lpszTitle = "Select Directory";
        LPITEMIDLIST l_PIDL = SHBrowseForFolderA(&l_BI);
        if (l_PIDL)
        {
            char l_Path[MAX_PATH];
            if (SHGetPathFromIDListA(l_PIDL, l_Path))
            {
                IMalloc* l_Malloc = nullptr;
                if (SUCCEEDED(SHGetMalloc(&l_Malloc)))
                {
                    l_Malloc->Free(l_PIDL);
                    l_Malloc->Release();
                }
                return std::filesystem::path(l_Path);
            }
        }
        return {};
#else
        FILE* l_Pipe = popen("zenity --file-selection --directory 2>/dev/null", "r");
        if (!l_Pipe)
        {
            return {};
        }
        char l_Buffer[1024];
        std::string l_Result;
        while (fgets(l_Buffer, sizeof(l_Buffer), l_Pipe))
        {
            l_Result += l_Buffer;
        }
        pclose(l_Pipe);
        if (!l_Result.empty() && l_Result.back() == '\n')
        {
            l_Result.pop_back();
        }
        return l_Result.empty() ? std::filesystem::path{} : std::filesystem::path(l_Result);
#endif
    }

    void LaunchEditor(const std::filesystem::path& p_ProjectPath)
    {
        std::filesystem::path l_Editor = std::filesystem::current_path() / "TrinityForge";
#ifdef _WIN32
        l_Editor.replace_extension(".exe");

        STARTUPINFOA l_SI{};
        PROCESS_INFORMATION l_PI{};
        l_SI.cb = sizeof(STARTUPINFOA);

        std::string l_Command = l_Editor.string();
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
            execl(l_Editor.c_str(), l_Editor.c_str(), static_cast<char*>(nullptr));
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
        std::filesystem::path l_Path = OpenDirectoryDialog();
        if (!l_Path.empty())
        {
            std::snprintf(m_ProjectDirectory, sizeof(m_ProjectDirectory), "%s", l_Path.string().c_str());
        }
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
}
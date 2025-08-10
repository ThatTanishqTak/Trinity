#include "Trinity/trpch.h"
#include "ProjectWizardLayer.h"

#include <imgui.h>

#include <ImGuiFileDialog.h>
#include <filesystem>
#include <string>
#include <cstdio>
#include <cstring>
#include <system_error>

ProjectWizardLayer::ProjectWizardLayer() : m_Templates{ "Empty", "2D", "3D" }, m_SelectedTemplate(0), m_IsOpen(false), m_ShowMessage(false), m_IsError(false)
{
    m_ProjectName[0] = '\0';
    std::string l_CurrentDir = std::filesystem::current_path().string();
    std::snprintf(m_ProjectDirectory, sizeof(m_ProjectDirectory), "%s", l_CurrentDir.c_str());
}

void ProjectWizardLayer::Open()
{
    m_IsOpen = true;
    m_ShowMessage = false;
    m_IsError = false;

    m_StatusMessage.clear();
    m_ProjectName[0] = '\0';
}

void ProjectWizardLayer::Close()
{
    m_IsOpen = false;
}

bool ProjectWizardLayer::IsOpen() const
{
    return m_IsOpen;
}

void ProjectWizardLayer::OnUIRender()
{
    if (!m_IsOpen)
    {
        return;
    }

    ImGui::Begin("Project Wizard", &m_IsOpen);

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
                m_StatusMessage = "Project created successfully.";
                m_IsError = false;
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
#include "Trinity/trpch.h"
#include "ProjectWizardLayer.h"

#include <imgui.h>
#include <filesystem>
#include <string>
#include <cstdio>

ProjectWizardLayer::ProjectWizardLayer() : m_Templates{ "Empty", "2D", "3D" }, m_SelectedTemplate(0)
{
    m_ProjectName[0] = '\0';
    std::string l_CurrentDir = std::filesystem::current_path().string();
    std::snprintf(m_ProjectDirectory, sizeof(m_ProjectDirectory), "%s", l_CurrentDir.c_str());
}

void ProjectWizardLayer::OnUIRender()
{
    ImGui::Begin("Project Wizard");

    ImGui::InputText("Project Name", m_ProjectName, sizeof(m_ProjectName));
    ImGui::InputText("Directory", m_ProjectDirectory, sizeof(m_ProjectDirectory));

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

    if (ImGui::Button("Create"))
    {
        std::filesystem::path l_ProjectPath = std::filesystem::path(m_ProjectDirectory) / m_ProjectName;
        if (!std::filesystem::exists(l_ProjectPath))
        {
            std::filesystem::create_directories(l_ProjectPath);
        }
    }

    ImGui::End();
}
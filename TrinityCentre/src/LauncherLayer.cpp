#include "Trinity/trpch.h"
#include "LauncherLayer.h"

#include <imgui.h>

void LauncherLayer::OnUIRender()
{
    if (m_ProjectWizard.IsOpen())
    {
        m_ProjectWizard.OnUIRender();
    }

    else
    {
        ImGui::Begin("Launcher");
        if (ImGui::Button("New Project"))
        {
            m_ProjectWizard.Open();
        }

        else if (ImGui::Button("Load Project"))
        {
            m_ProjectWizard.Open();
        }

        ImGui::End();
    }
}
#include "Trinity/trpch.h"
#include "LauncherLayer.h"

#include <imgui.h>

void LauncherLayer::OnUIRender()
{
    if (m_ProjectCreator.IsOpen())
    {
        m_ProjectCreator.OnUIRender();
    }
    else if (m_ProjectLoader.IsOpen())
    {
        m_ProjectLoader.OnUIRender();
    }
    else
    {
        ImGui::Begin("Launcher");
        if (ImGui::Button("New Project"))
        {
            m_ProjectCreator.Open();
        }

        else if (ImGui::Button("Load Project"))
        {
            m_ProjectLoader.Open();
        }

        ImGui::End();
    }
}
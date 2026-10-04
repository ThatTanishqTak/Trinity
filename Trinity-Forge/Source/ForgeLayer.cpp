#include "ForgeLayer.hpp"

#include <imgui.h>

ForgeLayer::ForgeLayer() : Layer("Forge")
{

}

void ForgeLayer::OnAttach()
{
    const Trinity::ApplicationCommandLineArgs& l_Args = Trinity::Application::Get().GetSpecification().CommandLineArgs;

    if (const auto it_Project = l_Args.GetOption("project"))
    {
        TR_INFO("Project: {}", *it_Project);
    }
    else
    {
        TR_INFO("No project given. Start Forge from Trinity-Hub or pass --project=<path>.");
    }
}

void ForgeLayer::OnEvent(Trinity::Event& event)
{
    Trinity::EventDispatcher l_Dispatcher(event);
    l_Dispatcher.Dispatch<Trinity::KeyPressedEvent>(TR_BIND_EVENT_FN(OnKeyPressed));
}

// F1 shows and hides the demo window, and never arrives here while an ImGui text field has the keyboard
bool ForgeLayer::OnKeyPressed(Trinity::KeyPressedEvent& event)
{
    if (event.GetKeyCode() != Trinity::KeyCode::TR_F1 || event.IsRepeat())
    {
        return false;
    }

    m_ShowDemoWindow = !m_ShowDemoWindow;
    TR_INFO("Forge: demo window {}", m_ShowDemoWindow ? "shown" : "hidden");

    return true;
}

void ForgeLayer::OnImGuiRender()
{
    if (m_ShowDemoWindow)
    {
        ImGui::ShowDemoWindow(&m_ShowDemoWindow);
    }
}
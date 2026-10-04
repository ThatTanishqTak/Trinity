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

    DrawFontWindow();
}

// Both weights, icons from the Font Awesome and Material Design sets, and the scale the UI is drawn at
void ForgeLayer::DrawFontWindow()
{
    if (!ImGui::Begin("Fonts", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::End();

        return;
    }

    ImGui::PushFont(Trinity::ImGuiLayer::GetFont(Trinity::UIFont::Bold), 0.0f);
    ImGui::TextUnformatted("JetBrains Mono Bold");
    ImGui::PopFont();
    ImGui::TextUnformatted("JetBrains Mono Regular");

    ImGui::Text("%s %s %s %s %s %s", Trinity::Icons::c_File, Trinity::Icons::c_FolderOpen, Trinity::Icons::c_Save, Trinity::Icons::c_Gear, Trinity::Icons::c_Terminal, Trinity::Icons::c_Warning);
    ImGui::Text("%s %s Material Design, above U+FFFF", Trinity::Icons::c_Monitor, Trinity::Icons::c_CubeOutline);

    ImGui::Separator();
    ImGui::Text("DPI scale %.2f, ui.scale %.2f, text %.0f px", ImGui::GetStyle().FontScaleDpi, ImGui::GetStyle().FontScaleMain, ImGui::GetFontSize());

    ImGui::End();
}
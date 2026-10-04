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

void ForgeLayer::OnImGuiRender()
{
    ImGui::ShowDemoWindow();
}
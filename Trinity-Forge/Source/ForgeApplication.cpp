#include "ForgeLayer.hpp"

#include <Trinity/Core/EntryPoint.hpp>

class ForgeApplication final : public Trinity::Application
{
public:
    explicit ForgeApplication(Trinity::ApplicationSpecification specification) : Application(std::move(specification))
    {
        // ImGui comes first, so Forge can add its panels to imgui.ini as it attaches. Overlays still update after layers, and the scene is the bottom layer, so events reach it last
        Trinity::ImGuiLayer& l_ImGui = PushOverlay<Trinity::ImGuiLayer>();
        const SceneLayer& l_Scene = PushLayer<SceneLayer>();
        PushLayer<ForgeLayer>(l_ImGui, l_Scene);
    }
};

Trinity::Application* Trinity::CreateApplication(ApplicationCommandLineArgs args)
{
    ApplicationSpecification l_Specification;
    l_Specification.Name = "Trinity Forge";
    l_Specification.CommandLineArgs = args;

    return new ForgeApplication(std::move(l_Specification));
}
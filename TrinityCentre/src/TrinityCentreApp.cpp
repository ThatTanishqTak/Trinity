#include "Trinity/Core/EntryPoint.h"

#include "LauncherLayer.h"

namespace Trinity
{
    class TrinityCentre : public Application
    {
    public:
        TrinityCentre(const ApplicationSpecification& specifications) : Application(specifications)
        {
            auto a_LauncherLayer = std::make_unique<LauncherLayer>();
            if (m_ImGuiLayer)
            {
                m_ImGuiLayer->RegisterPanel(std::move(a_LauncherLayer));
            }
        }
    };

    Application* CreateApplication(ApplicationCommandLineArgs args)
    {
        TR_INFO("Creating Application Specifications");

        ApplicationSpecification l_Specifications;
        l_Specifications.Title = "TrinityCenter";
        l_Specifications.Width = 1280;
        l_Specifications.Height = 720;
        l_Specifications.CommandLineArgs = args;
        l_Specifications.InitializeRenderer = false;

        std::filesystem::path l_ProjectAssets = std::filesystem::current_path() / "TrinityCentre" / "Resources" / "Layout";
        l_Specifications.ImGuiLayoutPath = (l_ProjectAssets / "imgui.ini").string();

        TR_INFO("Application Specifications Created");

        return new TrinityCentre(l_Specifications);
    }
}
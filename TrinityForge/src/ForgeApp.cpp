#include "Trinity/Core/EntryPoint.h"

#include "EditorLayer.h"
#include "Panels/StatsPanel.h"

namespace Trinity
{
    class ForgeApp : public Application
    {
    public:
        ForgeApp(const ApplicationSpecification& l_Specifications) : Application(l_Specifications)
        {
            auto l_EditorLayer = std::make_unique<EditorLayer>();
            l_EditorLayer->RegisterPanel(std::make_unique<StatsPanel>());
            m_ImGuiLayer->RegisterPanel(std::move(l_EditorLayer));
        }
    };

    Application* CreateApplication(ApplicationCommandLineArgs args)
    {
        TR_INFO("Creating Application Specifications");

        ApplicationSpecification l_Specifications;
        l_Specifications.Title = "Forge";
        l_Specifications.Width = 1920;
        l_Specifications.Height = 1080;
        l_Specifications.CommandLineArgs = args;

        TR_INFO("Application Specifications Created");

        return new ForgeApp(l_Specifications);
    }
}
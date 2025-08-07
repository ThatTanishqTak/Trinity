#include "Trinity/Core/EntryPoint.h"

#include "EditorLayer.h"
#include "Panels/StatsPanel.h"
#include "Panels/ToolbarPanel.h"
#include "Panels/PropertiesPanel.h"
#include "Panels/SceneHierarchyPanel.h"
#include "Panels/ContentBrowserPanel.h"
#include "Panels/SceneViewportPanel.h"

namespace Trinity
{
    class ForgeApp : public Application
    {
    public:
        ForgeApp(const ApplicationSpecification& l_Specifications) : Application(l_Specifications)
        {
            auto a_EditorLayer = std::make_unique<EditorLayer>();
            a_EditorLayer->RegisterPanel(std::make_unique<ToolbarPanel>());
            a_EditorLayer->RegisterPanel(std::make_unique<SceneViewportPanel>(m_Renderer.get()));
            a_EditorLayer->RegisterPanel(std::make_unique<SceneHierarchyPanel>());
            a_EditorLayer->RegisterPanel(std::make_unique<ContentBrowserPanel>());
            a_EditorLayer->RegisterPanel(std::make_unique<PropertiesPanel>());
            a_EditorLayer->RegisterPanel(std::make_unique<StatsPanel>());

            m_ImGuiLayer->RegisterPanel(std::move(a_EditorLayer));
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
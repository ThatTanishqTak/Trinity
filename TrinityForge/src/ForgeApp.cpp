#include "Trinity/Core/EntryPoint.h"

#include "EditorLayer.h"
#include "Panels/StatsPanel.h"
#include "Panels/ToolbarPanel.h"
#include "Panels/SceneHierarchyPanel.h"
#include "Panels/ContentBrowserPanel.h"
#include "Panels/InspectorPanel.h"
#include "Panels/SceneViewportPanel.h"

#include <entt/entt.hpp>
#include <filesystem>

namespace Trinity
{
    class ForgeApp : public Application
    {
    public:
        ForgeApp(const ApplicationSpecification& specifications) : Application(specifications)
        {
            auto a_EditorLayer = std::make_unique<EditorLayer>(m_Scene.get(), m_AssetManager.get());
            entt::entity* l_SelectionContext = a_EditorLayer->GetSelectionContext();
            a_EditorLayer->RegisterPanel(std::make_unique<ToolbarPanel>(m_Scene.get(), m_AssetManager.get()));
            a_EditorLayer->RegisterPanel(std::make_unique<SceneHierarchyPanel>(m_Scene.get(), l_SelectionContext));

            std::filesystem::path l_ProjectAssets = std::filesystem::current_path() / "Resources";

            a_EditorLayer->RegisterPanel(std::make_unique<ContentBrowserPanel>(l_ProjectAssets));
            a_EditorLayer->RegisterPanel(std::make_unique<InspectorPanel>(m_Scene.get(), l_SelectionContext, m_AssetManager.get()));
            a_EditorLayer->RegisterPanel(std::make_unique<StatsPanel>());
            a_EditorLayer->RegisterPanel(std::make_unique<SceneViewportPanel>(m_Renderer.get()));

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

        std::filesystem::path l_ImGuiLayout = std::filesystem::current_path() / "TrinityForge" / "Resources" / "Layout";
        l_Specifications.ImGuiLayoutPath = (l_ImGuiLayout / "imgui.ini").string();

        TR_INFO("Application Specifications Created");

        return new ForgeApp(l_Specifications);
    }
}
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
            m_ImGuiLayer->RegisterPanel(std::move(a_LauncherLayer));
        }
    };

    Application* CreateApplication(ApplicationCommandLineArgs args)
    {
        TR_INFO("Creating Application Specifications");

        ApplicationSpecification l_Specification;
        l_Specification.Title = "TrinityCenter";
        l_Specification.Width = 1280;
        l_Specification.Height = 720;
        l_Specification.CommandLineArgs = args;

        TR_INFO("Application Specifications Created");

        return new TrinityCentre(l_Specification);
    }
}
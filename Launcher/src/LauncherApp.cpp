#include "Trinity/Core/EntryPoint.h"

#include "LauncherLayer.h"

namespace Trinity
{
    class LauncherApp : public Application
    {
    public:
        LauncherApp(const ApplicationSpecification& l_Specifications) : Application(l_Specifications)
        {

        }
    };

    Application* CreateApplication(ApplicationCommandLineArgs args)
    {
        TR_INFO("Creating Application Specifications");

        ApplicationSpecification l_Specification;
        l_Specification.Title = "ForgeCenter";
        l_Specification.Width = 1280;
        l_Specification.Height = 720;
        l_Specification.CommandLineArgs = args;

        TR_INFO("Application Specifications Created");

        return new LauncherApp(l_Specification);
    }
}
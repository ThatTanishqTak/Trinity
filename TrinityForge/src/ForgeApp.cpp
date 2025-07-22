#include "Trinity/Core/EntryPoint.h"

#include "EditorLayer.h"

namespace Trinity
{
	class ForgeApp : public Application
	{
	public:
		ForgeApp(const ApplicationSpecification& specs) : Application(specs)
		{

		}
	};

	Application* CreateApplication(ApplicationCommandLineArgs args)
	{
		TR_INFO("Creating Application Specifications");

		ApplicationSpecification specs;
		specs.Title = "Forge";
		specs.Width = 1920;
		specs.Height = 1080;
		specs.CommandLineArgs = args;

		TR_INFO("Application Specifications Created");

		return new ForgeApp(specs);
	}
}
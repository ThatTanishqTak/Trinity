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
		TR_INFO("Creating Application");

		ApplicationSpecification specs;
		specs.Name = "Forge";
		specs.Width = 1920;
		specs.Height = 1080;
		specs.CommandLineArgs = args;

		TR_INFO("Application Created");

		return new ForgeApp(specs);
	}
}
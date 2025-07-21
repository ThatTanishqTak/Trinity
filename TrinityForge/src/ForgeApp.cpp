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
		ApplicationSpecification specs;
		specs.Name = "TrinityForge";
		specs.Width = 1920;
		specs.Height = 1080;
		specs.CommandLineArgs = args;

		return new ForgeApp(specs);
	}
}
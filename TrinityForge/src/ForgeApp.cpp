#include "Trinity/Core/EntryPoint.h"

#include "EditorLayer.h"

namespace Trinity
{
	class ForgeApp : public Application
	{
	public:
		ForgeApp(const ApplicationSpecification& l_Specifications) : Application(l_Specifications)
		{

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
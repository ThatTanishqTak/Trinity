#include <Trinity.h>
#include <Trinity/Core/EntryPoint.h>

#include "EditorLayer.h"

namespace Trinity
{
	class Forge : public Application
	{
	public:
		Forge(const ApplicationSpecification& spec) : Application(spec)
		{
			PushLayer(new EditorLayer());
		}

		~Forge()
		{

		}
	};

	Application* CreateApplication(ApplicationCommandLineArgs args)
	{
		ApplicationSpecification spec;
		spec.Name = "Forge";
		spec.CommandLineArgs = args;

		return new Forge(spec);
	}
}
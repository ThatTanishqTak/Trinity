#include <Trinity.h>
#include <Trinity/Core/EntryPoint.h>

#include "EditorLayer.h"

namespace Trinity
{
	class Forge : public Application
	{
	public:
		Forge(ApplicationCommandLineArgs args) : Application("Trinity-Forge", args)
		{
			PushLayer(new EditorLayer());
		}

		~Forge()
		{

		}
	};


	Application* CreateApplication(ApplicationCommandLineArgs args)
	{
		return new Forge(args);
	}
}
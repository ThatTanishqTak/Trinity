#include <Trinity.h>
#include <Trinity/Core/EntryPoint.h>

#include "EditorLayer.h"

namespace Trinity
{
	class Forge : public Application
	{
	public:
		Forge() : Application("Forge")
		{
			PushLayer(new EditorLayer());
		}

		~Forge()
		{

		}
	};


	Application* CreateApplication()
	{
		return new Forge();
	}
}
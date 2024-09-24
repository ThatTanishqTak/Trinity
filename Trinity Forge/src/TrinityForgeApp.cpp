#include <Trinity.h>
#include <Trinity/Core/EntryPoint.h>

#include "EditorLayer.h"

namespace Trinity
{
	class TrinityForge : public Application
	{
	public:
		TrinityForge() : Application("Trinity Forge")
		{
			PushLayer(new EditorLayer());
		}

		~TrinityForge()
		{

		}
	};

	Application* CreateApplication()
	{
		return new TrinityForge();
	}
}
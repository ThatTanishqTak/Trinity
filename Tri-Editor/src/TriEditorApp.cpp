#include <Trinity.h>
#include <Trinity/Core/EntryPoint.h>

#include "EditorLayer.h"

namespace Trinity
{
	class TriEditor : public Application
	{
	public:
		TriEditor() : Application("Trinity Editor")
		{
			PushLayer(new EditorLayer());
		}

		~TriEditor()
		{

		}
	};

	Application* CreateApplication()
	{
		return new TriEditor();
	}
}
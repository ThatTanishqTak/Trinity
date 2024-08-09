#pragma once

#include "Core.h"

namespace Trinity
{
	class TRINITY_API Application
	{
	public:
		Application();
		virtual ~Application();

		void Run();
	};

	Application* CreateApplication();
}
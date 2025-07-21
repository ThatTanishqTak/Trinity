#pragma once

#include <string>

#include "Trinity/Utilities/Utilities.h"

namespace Trinity
{
	struct ApplicationCommandLineArgs
	{
		int Count = 0;
		char** Args = nullptr;
	};

	struct ApplicationSpecification
	{
		std::string Name = "Trinity-Application";
		unsigned int Width = 1920;
		unsigned int Height = 1080;
		ApplicationCommandLineArgs CommandLineArgs;
	};

	class Application
	{
	public:
		Application() = default;
		Application(const ApplicationSpecification& specification);
		virtual ~Application();

		virtual void Run()
		{
			TR_CORE_INFO("Entering main loop");

			while (true)
			{

				// Poll events
				
				// Rendering
			}

			TR_CORE_INFO("Exiting main loop");
		}

	protected:
		ApplicationSpecification m_Specification;
	};
}
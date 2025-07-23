#pragma once

#include <string>

#include "Trinity/Utilities/Utilities.h"
#include "Trinity/Window/Window.h"
#include "Trinity/Vulkan/VulkanContext.h"

namespace Trinity
{
	struct ApplicationCommandLineArgs
	{
		int Count = 0;
		char** Args = nullptr;
	};

	struct ApplicationSpecification
	{
		std::string Title = "Trinity-Application";
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
			TR_CORE_TRACE("Entering main loop");

			while (!m_Window->ShouldWindowClose())
			{
				// Poll events
				m_Window->PollEvents();
				
				// Rendering
			}

			TR_CORE_TRACE("Exiting main loop");
		}

	protected:
		ApplicationSpecification m_Specification;

		std::unique_ptr<Window> m_Window;
		std::unique_ptr<VulkanContext> m_VulkanContext;
	};
}
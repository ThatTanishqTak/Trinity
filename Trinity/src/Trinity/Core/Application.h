#pragma once

#include <string>

#include "Trinity/Utilities/Utilities.h"
#include "Trinity/Window/Window.h"
#include "Trinity/Vulkan/VulkanContext.h"
#include "Trinity/Renderer/Renderer.h"
#include "Trinity/Core/ResourceManager.h"
#include "Trinity/ECS/Scene.h"
#include "Trinity/ECS/Entity.h"
#include "Trinity/ECS/Components.h"
#include "Trinity/Camera/CameraController.h"

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
				Utilities::Time::Update();

				// Poll events
				m_Window->PollEvents();
				if (m_CameraController)
				{
					m_CameraController->Update();
				}

				// Rendering
				m_Renderer->DrawFrame();
			}

			TR_CORE_TRACE("Exiting main loop");
		}

	protected:
		ApplicationSpecification m_Specification;

		std::unique_ptr<Window> m_Window;
		std::unique_ptr<VulkanContext> m_VulkanContext;
		std::unique_ptr<Renderer> m_Renderer;
		std::unique_ptr<ResourceManager> m_ResourceManager;
		std::unique_ptr<Scene> m_Scene;
		std::unique_ptr<CameraController> m_CameraController;
	};
}
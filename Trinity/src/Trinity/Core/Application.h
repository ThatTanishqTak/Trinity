#pragma once

#include <string>
#include <future>
#include <chrono>

#include "Trinity/Camera/CameraController.h"
#include "Trinity/Core/ResourceManager.h"
#include "Trinity/ECS/Components.h"
#include "Trinity/ECS/Entity.h"
#include "Trinity/ECS/Scene.h"
#include "Trinity/Events/Event.h"
#include "Trinity/Events/ApplicationEvent.h"
#include "Trinity/Renderer/Renderer.h"
#include "Trinity/UI/ImGuiLayer.h"
#include "Trinity/Utilities/Utilities.h"
#include "Trinity/Vulkan/VulkanContext.h"
#include "Trinity/Window/Window.h"

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

                m_ResourceManager->ProcessJobs();
                if (m_MeshComponent)
                {
                    if (m_MeshFuture.valid() && m_MeshFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
                    {
                        m_MeshComponent->MeshHandle = m_MeshFuture.get();
                    }

                    if (m_TextureFuture.valid() && m_TextureFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
                    {
                        m_MeshComponent->MeshTexture = m_TextureFuture.get();
                    }
                }

                // Poll events
                m_Window->PollEvents();
                if (m_CameraController)
                {
                    m_CameraController->Update();
                }

                // Rendering
                m_ImGuiLayer->BeginFrame();
                m_Renderer->DrawFrame([this](VkCommandBuffer l_CommandBuffer)
                    {
                        m_ImGuiLayer->EndFrame(l_CommandBuffer);
                    });
            }

            TR_CORE_TRACE("Exiting main loop");
        }

        virtual void OnEvent(Event& e);

    protected:
        bool OnWindowResize(WindowResizeEvent& e);

    protected:
        ApplicationSpecification m_Specification;

        std::unique_ptr<Window> m_Window;
        std::unique_ptr<VulkanContext> m_VulkanContext;
        std::unique_ptr<Renderer> m_Renderer;
        std::unique_ptr<ResourceManager> m_ResourceManager;
        std::unique_ptr<Scene> m_Scene;
        std::unique_ptr<CameraController> m_CameraController;
        std::unique_ptr<ImGuiLayer> m_ImGuiLayer;
        std::future<std::shared_ptr<Mesh>> m_MeshFuture;
        std::future<std::shared_ptr<Texture>> m_TextureFuture;
        MeshComponent* m_MeshComponent = nullptr;
    };
}
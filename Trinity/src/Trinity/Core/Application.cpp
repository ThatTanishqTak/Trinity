#include "Application.h"

#include "Trinity/trpch.h"

#include "Trinity/Core/Input.h"
#include "Trinity/Resources/ShaderWatcher.h"

namespace Trinity
{
    Application::Application(const ApplicationSpecification& specification) : m_Specification(specification)
    {
        TR_CORE_INFO("-------INITIALIZING APPLICATION-------");

        m_Window = std::make_unique<Window>(WindowSpecification{ m_Specification.Title, m_Specification.Width, m_Specification.Height });
        if (!m_Window->Initialize())
        {
            TR_CORE_ERROR("Failed to initialize window");
        }

        m_Window->SetEventCallback(TR_BIND_EVENT_FN(Application::OnEvent));

        Input::Initialize(m_Window->GetNativeWindow());

        m_VulkanContext = std::make_unique<VulkanContext>(m_Window->GetNativeWindow());
        if (!m_VulkanContext->Initialize())
        {
            TR_CORE_ERROR("Failed to initialize vulkan");
        }

        m_Renderer = std::make_unique<Renderer>(m_VulkanContext.get());
        if (!m_Renderer->Initialize())
        {
            TR_CORE_ERROR("Failed to initialize renderer");
        }

        m_ImGuiLayer = std::make_unique<ImGuiLayer>
            (
                m_Window->GetNativeWindow(), m_VulkanContext->GetInstance(), 
                m_VulkanContext->GetPhysicalDevice(), m_VulkanContext->GetDevice(),
                m_VulkanContext->FindQueueFamilies(m_VulkanContext->GetPhysicalDevice()).GraphicsFamily.value(),
                m_VulkanContext->GetGraphicsQueue(), m_Renderer->GetRenderPass(),static_cast<uint32_t>(m_VulkanContext->GetSwapChainImages().size())
            );
        
        if (!m_ImGuiLayer->Initialize())
        {
            TR_CORE_ERROR("Failed to initialize ImGui layer");
        }

        m_ResourceManager = std::make_unique<ResourceManager>(m_VulkanContext.get());

        m_Scene = std::make_unique<Scene>();
        m_Renderer->SetScene(m_Scene.get());

        m_CameraController = std::make_unique<CameraController>(&m_Renderer->GetCamera());

        Entity l_Entity = m_Scene->CreateEntity();
        auto& a_Mesh = l_Entity.AddComponent<MeshRenderer>();
        a_Mesh.MeshHandle = m_ResourceManager->Load<Mesh>("Assets/Textures/Checkers.png");
        a_Mesh.MeshTexture = m_ResourceManager->Load<Texture>("Assets/Textures/Checkers.png");

        Resources::ShaderWatcher::Start();

        TR_CORE_INFO("-------APPLICATION INITIALIZED-------");
    }

    Application::~Application()
    {
        TR_CORE_INFO("-------SHUTTING DOWN APPLICATION-------");

        if (m_Renderer)
        {
            m_Renderer->Shutdown();
            m_Renderer.reset();
        }

        if (m_ImGuiLayer)
        {
            m_ImGuiLayer->Shutdown();
            m_ImGuiLayer.reset();
        }

        m_Scene.reset();
        m_ResourceManager.reset();

        if (m_VulkanContext)
        {
            m_VulkanContext->Shutdown();
            m_VulkanContext.reset();
        }

        if (m_Window)
        {
            m_Window->Shutdown();
            m_Window.reset();
        }

        TR_CORE_INFO("-------APPLICATION SHUTDOWN COMPLETE-------");
    }

    void Application::OnEvent(Event& e)
    {
#if _DEBUG
        TR_CORE_TRACE("{}", e.ToString());
#endif
        EventDispatcher l_Dispatcher(e);
        l_Dispatcher.Dispatch<WindowResizeEvent>(TR_BIND_EVENT_FN(Application::OnWindowResize));

        if (m_ImGuiLayer)
        {
            m_ImGuiLayer->OnEvent(e);
        }
    }

    bool Application::OnWindowResize(WindowResizeEvent& e)
    {
        if (e.GetWidth() == 0 || e.GetHeight() == 0)
        {
            return false;
        }

        if (m_Renderer)
        {
            m_Renderer->OnWindowResize();
        }

        return false;
    }
}
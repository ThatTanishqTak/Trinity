#include "EditorLayer.h"

#include "imgui/imgui.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <iostream>

namespace Trinity
{
    EditorLayer::EditorLayer() : Layer("TrinityForge Layer"), m_CameraController(1280.0f / 720.0f)
    {

    }

    void EditorLayer::OnAttach()
    {
        FramebufferSpecifications specs;
        specs.Width = 1280;
        specs.Height = 720;

        m_Framebuffer = Framebuffer::Create(specs);
        m_ActiveScene = CreateRef<Scene>();

        m_Square = m_ActiveScene->CreateEntity();
        m_ActiveScene->Reg().emplace<TransformComponent>(m_Square);
        m_ActiveScene->Reg().emplace<SpriteRendererComponent>(m_Square, glm::vec4{ 0.0f, 1.0f, 0.0f, 1.0f });
    }

    void EditorLayer::OnDetach()
    {

    }

    void EditorLayer::OnImGuiRender()
    {
        static bool dockspaceOpen = true;

        static bool opt_fullscreen = true;
        static bool opt_padding = false;
        static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

        ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
        if (opt_fullscreen)
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
            window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        }
        else
        {
            dockspace_flags &= ~ImGuiDockNodeFlags_PassthruCentralNode;
        }

        if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
            window_flags |= ImGuiWindowFlags_NoBackground;

        if (!opt_padding)
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("DockSpace Demo", &dockspaceOpen, window_flags);
        if (!opt_padding)
            ImGui::PopStyleVar();

        if (opt_fullscreen)
            ImGui::PopStyleVar(2);

        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
        {
            ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
            ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
        }

        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Exit")) { Application::Get().Close(); }
                ImGui::EndMenu();
            }

            ImGui::EndMenuBar();
        }

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0.0f, 0.0f });

        ImGui::Begin("Viewport");
        {
            m_ViewportFocused = ImGui::IsWindowFocused();
            m_ViewportHovered = ImGui::IsWindowHovered();

            Application::Get().GetImGuiLayer()->BlockEvents(!m_ViewportFocused || !m_ViewportHovered);

            ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
            if (m_ViewportSize != *((glm::vec2*)&viewportPanelSize) && viewportPanelSize.x > 0.0f && viewportPanelSize.y > 0.0f)
            {
                m_Framebuffer->Resize((uint32_t)viewportPanelSize.x, (uint32_t)viewportPanelSize.y);
                m_ViewportSize = { viewportPanelSize.x, viewportPanelSize.y };

                m_CameraController.OnResize(viewportPanelSize.x, viewportPanelSize.y);
            }

            uint32_t textureID = m_Framebuffer->GetColorAttachmentRendererID();
            ImGui::Image((void*)textureID, ImVec2{ m_ViewportSize.x, m_ViewportSize.y }, ImVec2{ 0.0f, 1.0f }, ImVec2{ 1.0f, 0.0f });

            ImGui::End();
            ImGui::PopStyleVar();
        }

        ImGui::Begin("Render Stats");
        {
            auto stats = Renderer2D::GetStats();

            ImGui::Text("DrawCalls: %d", stats.DrawCalls);
            ImGui::Text("Index Count: %d", stats.GetTotalIndexCount());
            ImGui::Text("Vertex Count: %d", stats.GetTotalVertexCount());
            ImGui::Text("Quad Count: %d", stats.QuadCount);

            ImGui::End();
        }

        ImGui::Begin("Color");
        {
            auto& color = m_ActiveScene->Reg().get<SpriteRendererComponent>(m_Square).Color;
            ImGui::ColorEdit4("Square Color", glm::value_ptr(color));

            ImGui::End();
        }

        ImGui::End();
    }

    void EditorLayer::OnUpdate(Timestep deltaTime)
    {
        // Update
        if (m_ViewportFocused)
        {
            m_CameraController.OnUpdate(deltaTime);
        }
     
        // Render
        Renderer2D::ResetStats();
        m_Framebuffer->Bind();
        RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });

        Renderer2D::BeginScene(m_CameraController.GetCamera());
        
        // Update Scene
        m_ActiveScene->OnUpdate(deltaTime);

        Renderer2D::EndScene();
        
        m_Framebuffer->Unbind();
    }

    void EditorLayer::OnEvent(Event& e)
    {
        m_CameraController.OnEvent(e);
    }
}
#include "Sandbox2D.h"

#include "imgui/imgui.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

Sandbox2D::Sandbox2D() : Layer("Sandbox2D Layer"), m_CameraController(1280.0f / 720.0f)
{

}

void Sandbox2D::OnAttach()
{
	m_Texture = Trinity::Texture2D::Create("assets/textures/texture.png");
	m_Blend = Trinity::Texture2D::Create("assets/textures/blend.png");

	m_SubTexture = Trinity::SubTexture2D::CreateFromCoords(m_Texture, { 1.0f, 1.0f }, { 0.01f, 0.01f });

    Trinity::FramebufferSpecifications specs;
    specs.Width = 1280;
    specs.Height = 720;

    m_Framebuffer = Trinity::Framebuffer::Create(specs);
}

void Sandbox2D::OnDetach()
{

}

void Sandbox2D::OnImGuiRender()
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
            if (ImGui::MenuItem("Exit")) { Trinity::Application::Get().Close(); }
            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }

    ImGui::Begin("Settings");
    {
        auto stats = Trinity::Renderer2D::GetStats();

        ImGui::Text("DrawCalls: %d", stats.DrawCalls);
        ImGui::Text("Index Count: %d", stats.GetTotalIndexCount());
        ImGui::Text("Vertex Count: %d", stats.GetTotalVertexCount());
        ImGui::Text("Quad Count: %d", stats.QuadCount);

        ImGui::ColorEdit4("Square Color", glm::value_ptr(m_SquareColor));
        ImGui::InputFloat("Rotation Speed", &m_Speed);

        uint32_t textureID = m_Framebuffer->GetColorAttachmentRendererID();
        ImGui::Image((void*)textureID, ImVec2{ 1280, 720.0f }, ImVec2{ 0.0f, 1.0f }, ImVec2{ 1.0f, 0.0f });

        ImGui::End();
    }

    ImGui::End();
}

void Sandbox2D::OnUpdate(Trinity::Timestep timestep)
{
	m_CameraController.OnUpdate(timestep);

	Trinity::Renderer2D::ResetStats();

    m_Framebuffer->Bind();
	Trinity::RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
	Trinity::RenderCommand::Clear();

	Trinity::Renderer2D::BeginScene(m_CameraController.GetCamera());
	{
		Trinity::Renderer2D::DrawQuad({ 0.0f, 0.0f, -0.1f }, { 16.0f, 9.0f }, glm::radians(0.0f), m_Blend);
		Trinity::Renderer2D::DrawQuad({ 1.0f, 1.0f }, { 1.0f, 1.0f }, glm::radians(0.0f), m_SquareColor);
		Trinity::Renderer2D::DrawQuad({ 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f }, glm::radians(0.0f), m_Texture);

		Trinity::Renderer2D::DrawTilledQuad({ 2.0f, 2.0f }, { 1.0f, 1.0f }, glm::radians(45.0f), m_Texture, 10.0f);

		m_Rotation += m_Speed * timestep;

		Trinity::Renderer2D::DrawTilledQuad({ -2.0f, -2.0f }, { 1.0f, 1.0f }, glm::radians(m_Rotation), m_Blend, 20.0f);

		Trinity::Renderer2D::DrawQuad({ 0.0f, 0.0f }, { 1.0f, 1.0f }, 0.0f, m_SubTexture);
	}

	Trinity::Renderer2D::EndScene();
    m_Framebuffer->Unbind();
}

void Sandbox2D::OnEvent(Trinity::Event& e)
{
	m_CameraController.OnEvent(e);
}
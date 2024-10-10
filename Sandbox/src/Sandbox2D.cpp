#include "Sandbox2D.h"

#include "imgui/imgui.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

Sandbox2D::Sandbox2D() : Layer("Sandbox2D Layer"), m_CameraController(1280.0f / 720.0f)
{

}

void Sandbox2D::OnAttach()
{
    m_Tex = Trinity::Texture2D::Create("assets/textures/texture.png");
    m_Sub = Trinity::SubTexture2D::CreateFromCoords(m_Tex, { 1, 1 }, { 1, 1 });
}

void Sandbox2D::OnDetach()
{

}

void Sandbox2D::OnImGuiRender()
{
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

        ImGui::End();
    }
}

void Sandbox2D::OnUpdate(Trinity::Timestep timestep)
{
	m_CameraController.OnUpdate(timestep);
	m_Rotation += m_Speed * timestep;

	Trinity::Renderer2D::ResetStats();
	Trinity::RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });

	Trinity::Renderer2D::BeginScene(m_CameraController.GetCamera());
    {
        /*Trinity::Renderer2D::DrawQuad(glm::vec2{ 1.0f }, glm::vec2{ 1.0f }, glm::radians(0.0f), m_Sub);*/

        Trinity::Renderer2D::EndScene();
	}
}

void Sandbox2D::OnEvent(Trinity::Event& e)
{
	m_CameraController.OnEvent(e);
}
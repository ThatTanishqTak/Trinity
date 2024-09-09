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
}

void Sandbox2D::OnDetach()
{

}

void Sandbox2D::OnImGuiRender()
{
	ImGui::Begin("Settings");
	{
		ImGui::ColorEdit4("Square Color", glm::value_ptr(m_SquareColor));
		ImGui::InputFloat("Rotation Speed", &m_Speed);

		ImGui::End();
	}

	ImGui::Begin("Renderer2D Stats:");
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

	Trinity::Renderer2D::ResetStats();

	Trinity::RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
	Trinity::RenderCommand::Clear();

	Trinity::Renderer2D::BeginScene(m_CameraController.GetCamera());

	Trinity::Renderer2D::DrawQuad({ 0.0f, 0.0f, -0.1f }, { 16.0f, 9.0f }, 0.0f, m_Blend);
	Trinity::Renderer2D::DrawQuad({ 1.0f, 1.0f }, { 1.0f, 1.0f }, 0.0f, m_SquareColor);
	Trinity::Renderer2D::DrawQuad({ 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f }, 0.0f, m_Texture);

	Trinity::Renderer2D::DrawTilledQuad({ 2.0f, 2.0f }, { 1.0f, 1.0f }, glm::radians(45.0f), m_Texture, 10.0f);

	m_Rotation += m_Speed * timestep;

	Trinity::Renderer2D::DrawTilledQuad({ -2.0f, -2.0f }, { 1.0f, 1.0f }, glm::radians(m_Rotation), m_Blend, 20.0f);

	Trinity::Renderer2D::EndScene();
}

void Sandbox2D::OnEvent(Trinity::Event& e)
{
	m_CameraController.OnEvent(e);
}
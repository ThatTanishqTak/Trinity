#include "Sandbox2D.h"
#include <imgui/imgui.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

Sandbox2D::Sandbox2D()
	: Layer("Sandbox2D"), m_CameraController(1280.0f / 720.0f), m_SquareColor({ 0.2f, 0.3f, 0.8f, 1.0f })
{
}

void Sandbox2D::OnAttach()
{
	TR_PROFILE_FUNCTION();

	m_CheckerboardTexture = Trinity::Texture2D::Create("assets/textures/Checkerboard.png");
}

void Sandbox2D::OnDetach()
{
	TR_PROFILE_FUNCTION();
}

void Sandbox2D::OnUpdate(Trinity::Timestep timestep)
{
	TR_PROFILE_FUNCTION();

	// Update
	m_CameraController.OnUpdate(timestep);

	// Render
	Trinity::Renderer2D::ResetStats();
	{
		TR_PROFILE_SCOPE("Renderer Prep");
		Trinity::RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1 });
		Trinity::RenderCommand::Clear();
	}

	{
		static float rotation = 0.0f;
		rotation += timestep * 50.0f;

		TR_PROFILE_SCOPE("Renderer Draw");
		Trinity::Renderer2D::BeginScene(m_CameraController.GetCamera());
		{
			Trinity::Renderer2D::DrawQuad({ 0.0f, 0.0f, -0.1f }, { 20.0f, 20.0f }, m_CheckerboardTexture, 10.0f);
			Trinity::Renderer2D::DrawRotatedQuad({ -2.0f, 0.0f, 0.0f }, { 1.0f, 1.0f }, rotation, m_CheckerboardTexture, 20.0f);
		}
		Trinity::Renderer2D::EndScene();

		Trinity::Renderer2D::BeginScene(m_CameraController.GetCamera());
		{
			for (float y = -5.0f; y < 5.0f; y += 0.5f)
			{
				for (float x = -5.0f; x < 5.0f; x += 0.5f)
				{
					glm::vec4 color = { (x + 5.0f) / 10.0f, 0.4f, (y + 5.0f) / 10.0f, 0.7f };
					Trinity::Renderer2D::DrawQuad({ x, y }, { 0.45f, 0.45f }, color);
				}
			}
		}
		Trinity::Renderer2D::EndScene();
	}
}

void Sandbox2D::OnImGuiRender()
{
	TR_PROFILE_FUNCTION();

	ImGui::Begin("Settings");

	auto stats = Trinity::Renderer2D::GetStats();
	ImGui::Text("Renderer2D Stats:");
	ImGui::Text("Draw Calls: %d", stats.DrawCalls);
	ImGui::Text("Quads: %d", stats.QuadCount);
	ImGui::Text("Vertices: %d", stats.GetTotalVertexCount());
	ImGui::Text("Indices: %d", stats.GetTotalIndexCount());
	ImGui::Text("Camera: %d, %d, %d", m_CameraController.GetCamera().GetPosition().x, m_CameraController.GetCamera().GetPosition().y, m_CameraController.GetCamera().GetPosition().z);

	ImGui::ColorEdit4("Square Color", glm::value_ptr(m_SquareColor));
	ImGui::End();
}

void Sandbox2D::OnEvent(Trinity::Event& e)
{
	m_CameraController.OnEvent(e);
}
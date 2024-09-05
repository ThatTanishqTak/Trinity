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
	ImGui::Begin("Tint");
	
	ImGui::ColorEdit4("Square Color", glm::value_ptr(m_SquareColor));
	
	ImGui::End();
}

void Sandbox2D::OnUpdate(Trinity::Timestep timestep)
{
	m_CameraController.OnUpdate(timestep);

	Trinity::RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
	Trinity::RenderCommand::Clear();

	Trinity::Renderer2D::BeginScene(m_CameraController.GetCamera());

	Trinity::Renderer2D::DrawQuad({ 0.0f, 0.0f }, { 1.0f, 1.0f }, m_SquareColor);
	//Trinity::Renderer2D::DrawQuad({ 0.0f, 0.0f }, { 16.0f, 9.0f }, m_Blend);

	Trinity::Renderer2D::EndScene();
}

void Sandbox2D::OnEvent(Trinity::Event& e)
{
	m_CameraController.OnEvent(e);
}
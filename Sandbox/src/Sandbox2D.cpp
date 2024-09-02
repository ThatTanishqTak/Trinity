#include "Sandbox2D.h"

#include "imgui/imgui.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <chrono>

template<typename Fn>

class Timer
{
public:
	Timer(const char* name, Fn&& func) : m_Name(name), m_Func(func), m_Stopped(false)
	{
		m_StartTimepoint = std::chrono::high_resolution_clock::now();
	}

	~Timer()
	{
		if (!m_Stopped)
		{
			Stop();
		}
	}

	void Stop()
	{
		auto endTimepoint = std::chrono::high_resolution_clock::now();

		long long start = std::chrono::time_point_cast<std::chrono::microseconds>(m_StartTimepoint).time_since_epoch().count();
		long long end = std::chrono::time_point_cast<std::chrono::microseconds>(endTimepoint).time_since_epoch().count();
	
		m_Stopped = true;

		float duration = (end - start) * 0.001f;
		m_Func({ m_Name, duration });
	}

private:
	const char* m_Name;
	std::chrono::time_point<std::chrono::steady_clock> m_StartTimepoint;
	Fn m_Func;
	bool m_Stopped;
};

#define PROFILE_SCOPE(name) Timer timer##__LINE__(name, [&](ProfileResult profileResult) { m_ProfileResults.push_back(profileResult); })

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
	
	ImGui::ColorEdit4("Text Tint", glm::value_ptr(m_TextTint));
	ImGui::ColorEdit4("Background Tint", glm::value_ptr(m_BackgroundTint));
	
	ImGui::End();

	ImGui::Begin("Profile Result");

	for (auto& result : m_ProfileResults)
	{
		char label[50];
		strcpy(label, "  %.3fms ");
		strcat(label, result.Name);
		ImGui::Text(label, result.Time);
	}

	m_ProfileResults.clear();

	ImGui::End();
}

void Sandbox2D::OnUpdate(Trinity::Timestep timestep)
{
	PROFILE_SCOPE("Sandbox2D::OnUpdate");

	{
		PROFILE_SCOPE("Sandbox2D::CameraController");
		m_CameraController.OnUpdate(timestep);
	}

	{
		PROFILE_SCOPE("Sandbox2D::Render Start");
		Trinity::RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
		Trinity::RenderCommand::Clear();
	}

	Trinity::Renderer2D::BeginScene(m_CameraController.GetCamera());

	{
		PROFILE_SCOPE("Sandbox2D::DrawCalls");
		Trinity::Renderer2D::DrawQuad({ 0.0f, 0.0f }, { 16.0f, 9.0f }, m_Blend, m_BackgroundTint);
		Trinity::Renderer2D::DrawQuad({ 0.0f, 0.0f }, { 1.0f, 1.0f }, m_Texture, m_TextTint);
	}

	Trinity::Renderer2D::EndScene();
}

void Sandbox2D::OnEvent(Trinity::Event& e)
{
	m_CameraController.OnEvent(e);
}
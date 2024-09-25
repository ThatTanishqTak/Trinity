#include "trpch.h"
#include "OrthographicCameraController.h"

#include "Trinity/Core/Input.h"
#include "Trinity/Core/KeyCodes.h"

namespace Trinity
{
	OrthographicCameraController::OrthographicCameraController(float aspectRatio, bool rotation) 
		: m_AspectRatio(aspectRatio), m_Camera(-m_AspectRatio * m_ZoomLevel, m_AspectRatio* m_ZoomLevel, -m_ZoomLevel, m_ZoomLevel), m_Rotation(rotation)
	{

	}

	void OrthographicCameraController::OnUpdate(Timestep deltaTime)
	{
		if (Input::IsKeyPressed(Key::W))   { m_CameraPosition.y += m_CameraTranslationSpeed * deltaTime; }
		if (Input::IsKeyPressed(Key::S))   { m_CameraPosition.y -= m_CameraTranslationSpeed * deltaTime; }
		if (Input::IsKeyPressed(Key::D))   { m_CameraPosition.x += m_CameraTranslationSpeed * deltaTime; }
		if (Input::IsKeyPressed(Key::A))   { m_CameraPosition.x -= m_CameraTranslationSpeed * deltaTime; }

		if (m_Rotation)
		{
			if (Input::IsKeyPressed(Key::E))   { m_CameraRotation -= m_CameraRotationSpeed * deltaTime; }
			if (Input::IsKeyPressed(Key::Q))   { m_CameraRotation += m_CameraRotationSpeed * deltaTime; }

			m_Camera.SetRotation(m_CameraRotation);
		}

		m_Camera.SetPosition(m_CameraPosition);

		m_CameraTranslationSpeed = m_ZoomLevel;
	}

	void OrthographicCameraController::OnEvent(Event& e)
	{
		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<MouseScrolledEvent>(TR_BIND_EVENT_FN(OrthographicCameraController::OnMouseScrolled));
		dispatcher.Dispatch<WindowResizeEvent>(TR_BIND_EVENT_FN(OrthographicCameraController::OnWindowResized));
	}

	void OrthographicCameraController::OnResize(float width, float height)
	{
		m_AspectRatio = width / height;
		m_Camera.SetProjection(-m_AspectRatio * m_ZoomLevel, m_AspectRatio * m_ZoomLevel, -m_ZoomLevel, m_ZoomLevel);
	}

	bool OrthographicCameraController::OnMouseScrolled(MouseScrolledEvent& e)
	{
		m_ZoomLevel -= e.GetYOffset() * 0.25f;
		m_ZoomLevel = std::max(m_ZoomLevel, 0.25f);

		m_Camera.SetProjection(-m_AspectRatio * m_ZoomLevel, m_AspectRatio * m_ZoomLevel, -m_ZoomLevel, m_ZoomLevel);

		return false;
	}

	bool OrthographicCameraController::OnWindowResized(WindowResizeEvent& e)
	{
		OnResize((float)e.GetWidth(), (float)e.GetHeight());
		m_Camera.SetProjection(-m_AspectRatio * m_ZoomLevel, m_AspectRatio * m_ZoomLevel, -m_ZoomLevel, m_ZoomLevel);

		return false;
	}
}
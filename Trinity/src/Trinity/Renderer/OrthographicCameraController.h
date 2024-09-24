#pragma once

#include "Trinity/Renderer/OrthographicCamera.h"
#include "Trinity/Core/Timestep.h"

#include "Trinity/Events/ApplicationEvent.h"
#include "Trinity/Events/MouseEvent.h"

namespace Trinity
{
	class OrthographicCameraController
	{
	public:
		OrthographicCameraController(float aspectRatio, bool rotation = false);

		void OnUpdate(Timestep deltaTime);
		void OnEvent(Event& e);
		void OnResize(float width, float height);

		OrthographicCamera& GetCamera() { return m_Camera; }
		const OrthographicCamera& GetCamera() const { return m_Camera; }

		float GetZoomLevel() const { return m_ZoomLevel; }
		void SetZoomLevel(float level) { m_ZoomLevel = level; }

	private:
		bool OnMouseScrolled(MouseScrolledEvent& e);
		bool OnWindowResized(WindowResizeEvent& e);

	private:
		float m_AspectRatio;
		float m_ZoomLevel = 1.0f;

		glm::vec3 m_CameraPosition = { 0.0, 0.0f, 0.0f };
		float m_CameraTranslationSpeed = 5.0f;

		float m_CameraRotation = 0.0f;
		float m_CameraRotationSpeed = 180.0f;

		bool m_Rotation = false;

		OrthographicCamera m_Camera;
	};
}
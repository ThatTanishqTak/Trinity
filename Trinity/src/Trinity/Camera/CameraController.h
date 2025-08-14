#pragma once

#include <glm/glm.hpp>

#include "Trinity/Camera/Camera.h"

namespace Trinity
{
	class CameraController
	{
	public:
		CameraController(Camera* camera);

		void Update();

	private:
		Camera* m_Camera = nullptr;
		glm::vec3 m_Position{ 0.0f, 0.0f, 3.0f };
		glm::vec3 m_Rotation{ 0.0f };
		float m_MoveSpeed = 5.0f;
		float m_RotationSpeed = 90.0f;
		float m_MouseSensitivity = 0.1f;
		glm::vec2 m_LastMousePosition{ 0.0f };
		bool m_ResetMouse = true;
	};
}
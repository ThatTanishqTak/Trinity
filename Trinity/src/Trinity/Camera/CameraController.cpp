#include "Trinity/trpch.h"

#include "Trinity/Camera/CameraController.h"
#include "Trinity/Core/Input.h"
#include "Trinity/Utilities/Utilities.h"

#include <GLFW/glfw3.h>

namespace Trinity
{
	CameraController::CameraController(Camera* camera) : m_Camera(camera)
	{

	}

	void CameraController::Update()
	{
		float l_DeltaTime = Utilities::Time::GetDeltaTime();
		float l_MoveStep = m_MoveSpeed * l_DeltaTime;
		float l_RotationStep = m_RotationSpeed * l_DeltaTime;

		if (Input::IsKeyPressed(GLFW_KEY_W))
		{ 
			m_Position.z -= l_MoveStep;
		}
		
		if (Input::IsKeyPressed(GLFW_KEY_S))
		{ 
			m_Position.z += l_MoveStep;
		}
		
		if (Input::IsKeyPressed(GLFW_KEY_A))
		{ 
			m_Position.x -= l_MoveStep;
		}
		
		if (Input::IsKeyPressed(GLFW_KEY_D))
		{ 
			m_Position.x += l_MoveStep;
		}
		
		if (Input::IsKeyPressed(GLFW_KEY_Q))
		{ 
			m_Position.y -= l_MoveStep;
		}
		
		if (Input::IsKeyPressed(GLFW_KEY_E))
		{ 
			m_Position.y += l_MoveStep;
		}

		if (Input::IsKeyPressed(GLFW_KEY_UP))
		{
			m_Rotation.x += l_RotationStep;
		}
		
		if (Input::IsKeyPressed(GLFW_KEY_DOWN))
		{
			m_Rotation.x -= l_RotationStep;
		}
		
		if (Input::IsKeyPressed(GLFW_KEY_LEFT))
		{
			m_Rotation.y += l_RotationStep;
		}
		
		if (Input::IsKeyPressed(GLFW_KEY_RIGHT))
		{
			m_Rotation.y -= l_RotationStep;
		}

		if (m_Camera)
		{
			m_Camera->SetPosition(m_Position);
			m_Camera->SetRotation(m_Rotation);
		}
	}
}
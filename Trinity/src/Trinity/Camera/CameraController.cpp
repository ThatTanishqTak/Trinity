#include "Trinity/trpch.h"

#include "Trinity/Camera/CameraController.h"
#include "Trinity/Core/Input.h"
#include "Trinity/Utilities/Utilities.h"

#include <GLFW/glfw3.h>
#include <glm/gtx/euler_angles.hpp>

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

        glm::mat4 l_RotationMatrix = glm::yawPitchRoll(glm::radians(m_Rotation.y), glm::radians(m_Rotation.x), glm::radians(m_Rotation.z));
        glm::vec3 l_Forward = glm::vec3(l_RotationMatrix * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));
        glm::vec3 l_Right = glm::vec3(l_RotationMatrix * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
        glm::vec3 l_Up = glm::vec3(l_RotationMatrix * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));

        if (Input::IsKeyPressed(GLFW_KEY_W))
        {
            m_Position += l_Forward * l_MoveStep;
        }

        if (Input::IsKeyPressed(GLFW_KEY_S))
        {
            m_Position -= l_Forward * l_MoveStep;
        }

        if (Input::IsKeyPressed(GLFW_KEY_A))
        {
            m_Position -= l_Right * l_MoveStep;
        }

        if (Input::IsKeyPressed(GLFW_KEY_D))
        {
            m_Position += l_Right * l_MoveStep;
        }

        if (Input::IsKeyPressed(GLFW_KEY_Q))
        {
            m_Position -= l_Up * l_MoveStep;
        }

        if (Input::IsKeyPressed(GLFW_KEY_E))
        {
            m_Position += l_Up * l_MoveStep;
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

        if (Input::IsKeyPressed(GLFW_KEY_Z))
        {
            m_Rotation.z += l_RotationStep;
        }

        if (Input::IsKeyPressed(GLFW_KEY_X))
        {
            m_Rotation.z -= l_RotationStep;
        }

        if (Input::IsKeyPressed(GLFW_KEY_P))
        {
            m_Camera->SetPerspective(m_Camera->GetFov(), m_Camera->GetAspectRatio(), m_Camera->GetNear(), m_Camera->GetFar());
        }

        if (Input::IsKeyPressed(GLFW_KEY_O))
        {
            m_Camera->SetOrthographic(m_Camera->GetOrthoSize(), m_Camera->GetAspectRatio(), m_Camera->GetNear(), m_Camera->GetFar());
        }

        if (Input::IsKeyPressed(GLFW_KEY_KP_ADD))
        {
            if (m_Camera->GetProjectionType() == Camera::ProjectionType::Perspective)
            {
                float l_Fov = m_Camera->GetFov() - 30.0f * l_DeltaTime;
                m_Camera->SetPerspective(l_Fov, m_Camera->GetAspectRatio(), m_Camera->GetNear(), m_Camera->GetFar());
            }
            else
            {
                float l_Size = m_Camera->GetOrthoSize() - 5.0f * l_DeltaTime;
                m_Camera->SetOrthographic(l_Size, m_Camera->GetAspectRatio(), m_Camera->GetNear(), m_Camera->GetFar());
            }
        }

        if (Input::IsKeyPressed(GLFW_KEY_KP_SUBTRACT))
        {
            if (m_Camera->GetProjectionType() == Camera::ProjectionType::Perspective)
            {
                float l_Fov = m_Camera->GetFov() + 30.0f * l_DeltaTime;
                m_Camera->SetPerspective(l_Fov, m_Camera->GetAspectRatio(), m_Camera->GetNear(), m_Camera->GetFar());
            }
            else
            {
                float l_Size = m_Camera->GetOrthoSize() + 5.0f * l_DeltaTime;
                m_Camera->SetOrthographic(l_Size, m_Camera->GetAspectRatio(), m_Camera->GetNear(), m_Camera->GetFar());
            }
        }

        if (m_Camera)
        {
            m_Camera->SetPosition(m_Position);
            m_Camera->SetRotation(m_Rotation);
        }
    }
}
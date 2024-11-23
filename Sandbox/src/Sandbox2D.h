#pragma once

#include "Trinity.h"

class Sandbox2D : public Trinity::Layer
{
public:
	Sandbox2D();
	virtual ~Sandbox2D() = default;

	virtual void OnAttach() override;
	virtual void OnDetach() override;
	virtual void OnImGuiRender() override;

	void OnUpdate(Trinity::Timestep timestep) override;
	void OnEvent(Trinity::Event& e) override;

private:
	Trinity::OrthographicCameraController m_CameraController;

	Trinity::Ref<Trinity::VertexArray> m_SquareVA;
	
	Trinity::Ref<Trinity::Shader> m_FlatColorShader;

	Trinity::Ref<Trinity::Texture2D> m_Tex;
	Trinity::Ref<Trinity::SubTexture2D> m_Sub;

	glm::vec4 m_SquareColor = { 1.0f, 1.0f, 1.0f, 1.0f };

	float m_Rotation = 0;
	float m_Speed = 50;
};
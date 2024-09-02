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
	
	Trinity::Ref<Trinity::Texture2D> m_Texture;
	Trinity::Ref<Trinity::Texture2D> m_Blend;

	glm::vec4 m_BackgroundTint = { 1.0f, 1.0f, 1.0f, 1.0f };
	glm::vec4 m_TextTint = { 1.0f, 1.0f, 1.0f, 1.0f };
};
#pragma once

#include "Trinity.h"

class Sandbox2D : public Trinity::Layer
{
public:
	Sandbox2D();
	virtual ~Sandbox2D() = default;

	virtual void OnAttach() override;
	virtual void OnDetach() override;

	void OnUpdate(Trinity::Timestep timestep) override;
	virtual void OnImGuiRender() override;
	void OnEvent(Trinity::Event& e) override;
private:
	Trinity::OrthographicCameraController m_CameraController;

	// Temp
	Trinity::Ref<Trinity::VertexArray> m_SquareVA;
	Trinity::Ref<Trinity::Shader> m_FlatColorShader;

	Trinity::Ref<Trinity::Texture2D> m_CheckerboardTexture;

	glm::vec4 m_SquareColor = { 0.2f, 0.3f, 0.8f, 1.0f };
};
#pragma once

#include "Trinity.h"

class ExampleLayer : public Trinity::Layer
{
public:
	ExampleLayer();
	virtual ~ExampleLayer() = default;

	virtual void OnAttach() override;
	virtual void OnDetach() override;

	void OnUpdate(Trinity::Timestep ts) override;
	virtual void OnImGuiRender() override;
	void OnEvent(Trinity::Event& e) override;
private:
	Trinity::ShaderLibrary m_ShaderLibrary;
	Trinity::Ref<Trinity::Shader> m_Shader;
	Trinity::Ref<Trinity::VertexArray> m_VertexArray;

	Trinity::Ref<Trinity::Shader> m_FlatColorShader;
	Trinity::Ref<Trinity::VertexArray> m_SquareVA;

	Trinity::Ref<Trinity::Texture2D> m_Texture, m_ChernoLogoTexture;

	Trinity::OrthographicCameraController m_CameraController;
	glm::vec3 m_SquareColor = { 0.2f, 0.3f, 0.8f };
};
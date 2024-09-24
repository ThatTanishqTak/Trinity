#pragma once

#include "Trinity.h"

namespace Trinity
{
	class EditorLayer : public Layer
	{
	public:
		EditorLayer();
		virtual ~EditorLayer() = default;

		virtual void OnAttach() override;
		virtual void OnDetach() override;
		virtual void OnImGuiRender() override;

		void OnUpdate(Timestep timestep) override;
		void OnEvent(Event& e) override;

	private:
		OrthographicCameraController m_CameraController;

		Ref<VertexArray> m_SquareVA;

		Ref<Shader> m_FlatColorShader;

		Ref<Texture2D> m_Texture;
		Ref<Texture2D> m_Blend;

		Ref<SubTexture2D> m_SubTexture;

		Ref<Framebuffer> m_Framebuffer;

		glm::vec4 m_SquareColor = { 1.0f, 1.0f, 1.0f, 1.0f };

		glm::vec2 m_ViewportSize = { 0.0f, 0.0f };

		float m_Rotation = 0;
		float m_Speed = 50;

		bool m_ViewportFocused = false;
		bool m_ViewportHovered = false;
	};
}
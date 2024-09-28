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
		Ref<Framebuffer> m_Framebuffer;
		Ref<Scene> m_ActiveScene;

		Entity m_Square;
		Entity m_CameraEntity;
		Entity m_SecondCamera;

		glm::vec2 m_ViewportSize = { 0.0f, 0.0f };

		bool m_PrimaryCamera = true;

		bool m_ViewportFocused = false;
		bool m_ViewportHovered = false;
	};
}
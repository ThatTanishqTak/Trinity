#pragma once

#include "Trinity.h"
#include "Panels/SceneHierarchyPanel.h"
#include "Panels/ContentBrowserPanel.h"
#include "Trinity/Renderer/EditorCamera.h"

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
		bool OnKeyPressed(KeyPressedEvent& e);
		bool OnMouseButtonPressed(MouseButtonPressedEvent& e);
		void OnOverlayRenderer();

		void NewScene();
		void SaveScene();
		void SaveSceneAs();
		void OpenScene();
		void OpenScene(const std::filesystem::path& path);

		void SerializeScene(Ref<Scene> scene, const std::filesystem::path& path);

		void OnScenePlay();
		void OnSceneStop();

		void OnDuplicateEntity();
		void OnDeleteEntity();

		bool CanSelectEntity() const;

		void UI_ToolBar();

	private:
		OrthographicCameraController m_CameraController;

		Ref<VertexArray> m_SquareVA;
		Ref<Shader> m_FlatColorShader;
		Ref<Framebuffer> m_Framebuffer;

		Ref<Scene> m_ActiveScene;
		Ref<Scene> m_EditorScene;
		Ref<Scene> m_RuntimeScene;

		std::filesystem::path m_EditorScenePath;

		Entity m_Square;
		Entity m_CameraEntity;
		Entity m_SecondCamera;

		Entity m_HoveredEntity;
		Entity m_SelectedEntity;

		EditorCamera m_EditorCamera;

		glm::vec2 m_ViewportSize = { 0.0f, 0.0f };
		glm::vec2 m_ViewportBounds[2];

		enum class SceneState
		{
			Edit = 0, Play = 1
		};

		SceneState m_SceneState = SceneState::Edit;

		Ref<Texture2D> m_IconPlay;
		Ref<Texture2D> m_IconStop;

		bool m_PrimaryCamera = true;
		bool m_ViewportFocused = false;
		bool m_ViewportHovered = false;

		bool m_ShowColliders = false;

		int m_GizmoType = -1;

		//Panels
		SceneHierarchyPanel m_SceneHierarchyPanel;
		ContentBrowserPanel m_ContentBrowserPanel;
	};
}
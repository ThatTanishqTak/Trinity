#pragma once

#include "Trinity/Core/Core.h"
#include "Trinity/Core/Log.h"
#include "Trinity/Scene/Scene.h"
#include "Trinity/Scene/Entity.h"

#include "Trinity/Renderer/Texture.h"

namespace Trinity
{
	class SceneHierarchyPanel
	{
	public:
		SceneHierarchyPanel() = default;
		SceneHierarchyPanel(const Ref<Scene>& context);
		
		void SetContext(const Ref<Scene>& context);
		void OnImGuiRender();

		Entity GetSelectedEntity() const { return m_SelectionContext; }
		void SetSelectedEntity(Entity entity);

	private:
		template<typename T>
		void DisplayAddComponentEntry(const std::string& entryName);

		void DrawEntityNode(Entity entity);
		void DrawComponents(Entity entity);

	private:
		Ref<Scene> m_Context;
		Entity m_SelectionContext;

		bool m_PrimaryCamera = true;
	};
}
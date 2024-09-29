#pragma once

#include "Trinity/Core/Core.h"
#include "Trinity/Core/Log.h"
#include "Trinity/Scene/Scene.h"
#include "Trinity/Scene/Entity.h"

namespace Trinity
{
	class SceneHierarchyPanel
	{
	public:
		SceneHierarchyPanel() = default;
		SceneHierarchyPanel(const Ref<Scene>& context);
		
		void SetContext(const Ref<Scene>& context);
		void OnImGuiRender();

	private:
		void DrawEntityNode(Entity entity);

	private:
		Ref<Scene> m_Context;
		Entity m_SelectionContext;
	};
}
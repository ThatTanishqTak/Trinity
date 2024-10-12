#pragma once

#include "Trinity/Core/Timestep.h"
#include "Trinity/Renderer/EditorCamera.h"

#include "entt.hpp"

class b2World;

namespace Trinity
{
	class Entity;

	class Scene
	{
	public:
		Scene();
		~Scene();

		Entity CreateEntity(const std::string& name = std::string());
		void DestroyEntity(Entity entity);

		void OnRuntimeStart();
		void OnRuntimeStop();

		void OnUpdateRuntime(Timestep timestep);
		void OnUpdateEditor(Timestep timestep, EditorCamera& editorCamera);
		void OnViewportResize(uint32_t width, uint32_t height);

		Entity GetPrimaryCameraEntity();

	private:
		template<typename T>
		void OnComponentAdded(Entity entity, T& component);

	private:
		entt::registry m_Registry;
	
		b2World* m_PhysicsWorld = nullptr;
	
		friend class Entity;
		friend class SceneSerializer;
		friend class SceneHierarchyPanel;

		uint32_t count = 0;
		uint32_t m_ViewportWidth = 0;
		uint32_t m_ViewportHeight = 0;
	};
}
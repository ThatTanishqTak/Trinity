#pragma once

#include "Trinity/Core/Timestep.h"
#include "Trinity/Core/UUID.h"
#include "Trinity/Renderer/EditorCamera.h"

#include "entt.hpp"

#include <list>

class b2World;

namespace Trinity
{
	class Entity;

	class Scene
	{
	public:
		Scene();
		~Scene();

		static Ref<Scene> Copy(Ref<Scene> sourceScene);

		Entity CreateEntity(const std::string& name = std::string());
		Entity CreateEntityWithUUID(UUID uuid, const std::string& name = std::string());
		void DestroyEntity(Entity entity);

		void OnRuntimeStart();
		void OnSimulationStart();
		
		void OnRuntimeStop();
		void OnSimulationStop();

		//TODO: (Add OnScenePause())

		void OnUpdateRuntime(Timestep timestep);
		void OnUpdateSimulation(Timestep timestep, EditorCamera& editorCamera);
		void OnUpdateEditor(Timestep timestep, EditorCamera& editorCamera);

		void OnViewportResize(uint32_t width, uint32_t height);

		void DuplicateEntity(Entity entity);

		Entity GetPrimaryCameraEntity();

		template<typename... T>
		auto GetAllEntitiesWith()
		{
			return m_Registry.view<T...>();
		}

		void OnPhysics2DStart();
		void OnPhysics2DStop();

		void RenderScene(EditorCamera& camera);

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
		
		std::list<uint32_t> entityList;
	};
}
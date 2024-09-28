#pragma once

#include "entt.hpp"

#include "Trinity/Core/Timestep.h"

namespace Trinity
{
	class Entity;

	class Scene
	{
	public:
		Scene();
		~Scene();

		Entity CreateEntity(const std::string& name = std::string());

		void OnUpdate(Timestep deltaTime);
		void OnViewportResize(uint32_t width, uint32_t height);

	private:
		entt::registry m_Registry;
	
		friend class Entity;

		uint32_t count = 0;
		uint32_t m_ViewportWidth = 0;
		uint32_t m_ViewportHeight = 0;
	};
}
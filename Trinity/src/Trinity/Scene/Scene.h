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

	private:
		entt::registry m_Registry;
	
		friend class Entity;
	};
}
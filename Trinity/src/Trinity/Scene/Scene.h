#pragma once

#include "entt.hpp"

#include "Trinity/Core/Timestep.h"

namespace Trinity
{
	class Scene
	{
	public:
		Scene();
		~Scene();

		entt::entity CreateEntity();

		entt::registry& Reg() { return m_Registry; }

		void OnUpdate(Timestep deltaTime);

	private:
		entt::registry m_Registry;
	};
}
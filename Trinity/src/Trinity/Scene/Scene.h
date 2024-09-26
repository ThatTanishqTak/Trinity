#pragma once

#include "entt.hpp"

namespace Trinity
{
	class Scene
	{
	public:
		Scene();
		~Scene();

	private:
		entt::registry m_Registry;
	};
}
#pragma once

#include "Trinity/Layer/Layer.hpp"

namespace Forge
{
	class ForgeLayer : public Trinity::Layer
	{
	public:
		ForgeLayer();

		void OnAttach() override;
		void OnDetach() override;
		void OnUpdate(Trinity::Timestep deltaTime) override;
		void OnFixedUpdate(Trinity::Timestep fixedDeltaTime) override;
		void OnEvent(Trinity::Event& event) override;
	};
}
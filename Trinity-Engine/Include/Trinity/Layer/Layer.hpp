#pragma once

#include "Trinity/Time/Timestep.hpp"

#include <string>
#include <string_view>

namespace Trinity
{
	class Event;

	class Layer
	{
	public:
		Layer(std::string_view name = "Layer");
		virtual ~Layer() = default;

		Layer(const Layer&) = delete;
		Layer& operator=(const Layer&) = delete;
		Layer(Layer&&) = delete;
		Layer& operator=(Layer&&) = delete;

		virtual void OnAttach() {}
		virtual void OnDetach() {}
		virtual void OnUpdate(Timestep /*deltaTime*/) {}
		virtual void OnFixedUpdate(Timestep /*fixedDeltaTime*/) {}
		virtual void OnEvent(Event& /*event*/) {}

		const std::string& GetName() const { return m_DebugName; }

	protected:
		std::string m_DebugName;
	};
}
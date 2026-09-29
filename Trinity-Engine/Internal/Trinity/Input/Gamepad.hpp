#pragma once

#include "Trinity/Events/Event.hpp"

#include <memory>

namespace Trinity
{
	class Gamepad : public EventSource
	{
	public:
		Gamepad() = default;
		virtual ~Gamepad() = default;

		Gamepad(const Gamepad&) = delete;
		Gamepad& operator=(const Gamepad&) = delete;
		Gamepad(Gamepad&&) = delete;
		Gamepad& operator=(Gamepad&&) = delete;

		virtual bool Initialize() = 0;
		virtual void Shutdown() = 0;
		virtual void Poll() = 0;

		static std::unique_ptr<Gamepad> Create();
	};
}
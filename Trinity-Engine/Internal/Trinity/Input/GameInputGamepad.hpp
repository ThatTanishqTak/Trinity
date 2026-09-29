#pragma once

#include "Trinity/Input/Gamepad.hpp"

#include <memory>

namespace Trinity
{
	class GameInputGamepad final : public Gamepad
	{
	public:
		GameInputGamepad();
		~GameInputGamepad() override;

		bool Initialize() override;
		void Shutdown() override;
		void Poll() override;

	private:
		struct State;

		std::unique_ptr<State> m_State;
	};
}
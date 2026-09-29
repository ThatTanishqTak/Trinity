#pragma once

#include "Trinity/Input/GamepadCodes.hpp"
#include "Trinity/Input/KeyCodes.hpp"
#include "Trinity/Input/MouseCodes.hpp"

#include <cstdint>
#include <utility>
#include <vector>

namespace Trinity
{
	class Application;
	class Event;

	class Input
	{
	public:
		Input() = delete;

		static bool IsKeyDown(KeyCode key);
		static bool IsKeyPressed(KeyCode key);
		static bool IsKeyReleased(KeyCode key);

		static bool IsMouseButtonDown(MouseCode button);
		static bool IsMouseButtonPressed(MouseCode button);
		static bool IsMouseButtonReleased(MouseCode button);

		static std::pair<float, float> GetMousePosition();
		static std::pair<float, float> GetScrollDelta();

		static bool IsGamepadConnected(uint32_t gamepadId);
		static bool IsGamepadButtonDown(uint32_t gamepadId, GamepadButton button);
		static bool IsGamepadButtonPressed(uint32_t gamepadId, GamepadButton button);
		static bool IsGamepadButtonReleased(uint32_t gamepadId, GamepadButton button);
		static float GetGamepadAxis(uint32_t gamepadId, GamepadAxis axis);

	private:
		friend class Application;

		static void OnEvent(const Event& event);
		static void EndFrame();

		static std::vector<KeyCode> GetHeldKeys();
		static KeyCode GetHeldKeyLabel(KeyCode key);
		static std::vector<MouseCode> GetHeldMouseButtons();
	};
}
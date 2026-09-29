#include "Trinity/Input/Gamepad.hpp"

#include "Trinity/Core/Log.hpp"

#if defined(_WIN32)
#include "Trinity/Input/GameInputGamepad.hpp"
#endif

namespace Trinity
{
	std::unique_ptr<Gamepad> Gamepad::Create()
	{
#if defined(_WIN32)
		return std::make_unique<GameInputGamepad>();
#else
		TR_CORE_WARN("No gamepad backend for this platform yet");

		return nullptr;
#endif
	}
}
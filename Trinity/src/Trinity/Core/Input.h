#pragma once

#include "Trinity/Core/Core.h"
#include "Trinity/Core/KeyCodes.h"
#include "Trinity/Core/MouseCodes.h"


namespace Trinity
{
	class Input
	{
	public:
		static bool IsKeyPressed(int keyCode);
		static bool IsMouseButtonPressed(int button);
		
		static std::pair<float, float> GetMousePosition();
		
		static float GetMouseX();
		static float GetMouseY();
	};
}
#include "Trinity/Core/Platform.hpp"

namespace Trinity
{
    namespace Platform
    {
        // No gamepads until the Linux platform layer, in M13, and the macOS one, in M14, read them
        void InitializeGamepads()
        {

        }

        void ShutdownGamepads()
        {

        }

        void PollGamepads(GamepadListener&, bool)
        {

        }

        void SetGamepadRumble(std::uint32_t, float, float)
        {

        }
    }
}
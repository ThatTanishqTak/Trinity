#include "Trinity/Core/Platform.hpp"

namespace Trinity
{
    namespace Platform
    {
        // No gamepads until the Linux and macOS platform layers read them
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

        // Nothing is installed for them here
        bool HasGamepadRuntime()
        {
            return true;
        }
    }
}
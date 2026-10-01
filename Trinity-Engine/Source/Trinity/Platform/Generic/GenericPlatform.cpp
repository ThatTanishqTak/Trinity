#include "Trinity/Core/Platform.hpp"

namespace Trinity
{
    namespace Platform
    {
        void Initialize() {}
        void Shutdown() {}

        const char* GetName()
        {
#if defined(TR_PLATFORM_LINUX)
            return "Linux";
#elif defined(TR_PLATFORM_MACOS)
            return "macOS";
#else
            return "Unknown";
#endif
        }
    }
}
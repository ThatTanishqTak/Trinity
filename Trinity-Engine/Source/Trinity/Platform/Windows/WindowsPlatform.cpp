#include "Trinity/Core/Platform.hpp"

#include "Trinity/Platform/Windows/WindowsHeaders.hpp"

namespace Trinity
{
    namespace Platform
    {

        void Initialize()
        {
            ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            ::SetConsoleOutputCP(CP_UTF8);
        }

        void Shutdown()
        {

        }

        const char* GetName()
        {
            return "Windows";
        }
    }
}
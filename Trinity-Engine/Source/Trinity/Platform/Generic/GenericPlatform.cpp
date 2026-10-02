#include "Trinity/Core/Platform.hpp"

#include <cstdlib>

namespace Trinity
{
    namespace Platform
    {
        void Initialize()
        {

        }

        void Shutdown()
        {

        }

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

        void* Allocate(std::size_t size, std::size_t alignment)
        {
            // aligned_alloc requires the size to be a multiple of the alignment.
            const std::size_t l_Size = (size + alignment - 1) & ~(alignment - 1);

            return std::aligned_alloc(alignment, l_Size);
        }

        void Free(void* memory)
        {
            std::free(memory);
        }

        std::filesystem::path GetUserDataDirectory()
        {
            const char* l_Home = std::getenv("HOME");
            const bool l_HasHome = l_Home != nullptr && *l_Home != '\0';
#if defined(TR_PLATFORM_MACOS)
            if (l_HasHome)
            {
                return std::filesystem::path(l_Home) / "Library" / "Application Support";
            }
#else
            if (const char* l_DataHome = std::getenv("XDG_DATA_HOME"); l_DataHome != nullptr && std::filesystem::path(l_DataHome).is_absolute())
            {
                return l_DataHome;
            }

            if (l_HasHome)
            {
                return std::filesystem::path(l_Home) / ".local" / "share";
            }
#endif

            return {};
        }
    }
}
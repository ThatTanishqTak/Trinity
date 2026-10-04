#include "Trinity/Core/Platform.hpp"

#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <dlfcn.h>

#if defined(TR_PLATFORM_MACOS)
#include <mach-o/dyld.h>

#include <cstdint>
#include <cstring>
#endif

namespace Trinity
{
    namespace Platform
    {
        namespace
        {
            std::string s_Clipboard;
        }

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

        std::filesystem::path GetExecutableDirectory()
        {
            std::error_code l_Error;
#if defined(TR_PLATFORM_MACOS)
            std::uint32_t l_Size = 0;
            ::_NSGetExecutablePath(nullptr, &l_Size);

            std::string l_Path(l_Size, '\0');
            if (::_NSGetExecutablePath(l_Path.data(), &l_Size) != 0)
            {
                return {};
            }
            l_Path.resize(std::strlen(l_Path.c_str()));

            return std::filesystem::canonical(l_Path, l_Error).parent_path();
#else
            return std::filesystem::read_symlink("/proc/self/exe", l_Error).parent_path();
#endif
        }

        void* LoadSharedLibrary(const std::filesystem::path& path, std::string& error)
        {
            void* l_Library = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
            if (l_Library == nullptr)
            {
                const char* l_Error = ::dlerror();
                error = l_Error != nullptr ? l_Error : "unknown error";
            }

            return l_Library;
        }

        void* GetSharedLibrarySymbol(void* library, const char* name)
        {
            return ::dlsym(library, name);
        }

        void UnloadSharedLibrary(void* library)
        {
            ::dlclose(library);
        }

        // No system clipboard until the Linux platform layer exists, so copy and paste work within the process only
        std::string GetClipboardText()
        {
            return s_Clipboard;
        }

        void SetClipboardText(std::string_view text)
        {
            s_Clipboard = text;
        }

        std::vector<MonitorInfo> GetMonitors()
        {
            return {};
        }

        std::optional<ScreenPoint> GetCursorPosition()
        {
            return std::nullopt;
        }

        void* GetWindowAt(ScreenPoint)
        {
            return nullptr;
        }

        void* GetFocusedWindow()
        {
            return nullptr;
        }
    }
}
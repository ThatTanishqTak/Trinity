#include "Trinity/Core/Platform.hpp"

#include "Trinity/Platform/Windows/WindowsHeaders.hpp"

#include <shlobj.h>

#include <malloc.h>

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

        void* Allocate(std::size_t size, std::size_t alignment)
        {
            return ::_aligned_malloc(size, alignment);
        }

        void Free(void* memory)
        {
            ::_aligned_free(memory);
        }

        std::filesystem::path GetUserDataDirectory()
        {
            PWSTR l_Folder = nullptr;
            std::filesystem::path l_Result;
            if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &l_Folder)))
            {
                l_Result = l_Folder;
            }

            ::CoTaskMemFree(l_Folder);

            return l_Result;
        }
    }
}
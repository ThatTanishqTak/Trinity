#include "Trinity/Core/Platform.hpp"

#include "Trinity/Platform/Windows/WindowsHeaders.hpp"

#include <shlobj.h>

#include <malloc.h>

#include <format>
#include <string>

namespace Trinity
{
    namespace Platform
    {
        namespace
        {
            std::string GetErrorMessage(DWORD error)
            {
                LPWSTR l_Buffer = nullptr;
                const DWORD l_Length = ::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0, reinterpret_cast<LPWSTR>(&l_Buffer), 0, nullptr);

                std::string l_Message;
                if (l_Length != 0)
                {
                    const int l_Size = ::WideCharToMultiByte(CP_UTF8, 0, l_Buffer, static_cast<int>(l_Length), nullptr, 0, nullptr, nullptr);
                    l_Message.resize(static_cast<std::size_t>(l_Size));
                    ::WideCharToMultiByte(CP_UTF8, 0, l_Buffer, static_cast<int>(l_Length), l_Message.data(), l_Size, nullptr, nullptr);
                }
                ::LocalFree(l_Buffer);

                while (!l_Message.empty() && (l_Message.back() == '\n' || l_Message.back() == '\r' || l_Message.back() == ' '))
                {
                    l_Message.pop_back();
                }

                return l_Message.empty() ? std::format("error {}", error) : l_Message;
            }
        }

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

        std::filesystem::path GetExecutableDirectory()
        {
            std::wstring l_Path(MAX_PATH, L'\0');
            while (true)
            {
                const DWORD l_Length = ::GetModuleFileNameW(nullptr, l_Path.data(), static_cast<DWORD>(l_Path.size()));
                if (l_Length == 0)
                {
                    return {};
                }

                if (l_Length < l_Path.size())
                {
                    l_Path.resize(l_Length);

                    return std::filesystem::path(l_Path).parent_path();
                }

                l_Path.resize(l_Path.size() * 2);
            }
        }

        void* LoadSharedLibrary(const std::filesystem::path& path, std::string& error)
        {
            // Also searches the library's own folder, so a module's dependencies can sit beside it
            const HMODULE l_Library = ::LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if (l_Library == nullptr)
            {
                error = GetErrorMessage(::GetLastError());
            }

            return l_Library;
        }

        void* GetSharedLibrarySymbol(void* library, const char* name)
        {
            return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(library), name));
        }

        void UnloadSharedLibrary(void* library)
        {
            ::FreeLibrary(static_cast<HMODULE>(library));
        }
    }
}
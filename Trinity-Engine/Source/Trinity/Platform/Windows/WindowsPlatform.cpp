#include "Trinity/Core/Platform.hpp"

#include "Trinity/Platform/Windows/WindowsHeaders.hpp"

#include <shellscalingapi.h>
#include <shlobj.h>

#include <malloc.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

            std::string ToUtf8(std::wstring_view text)
            {
                if (text.empty())
                {
                    return {};
                }

                const int l_Size = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
                std::string l_Result(static_cast<std::size_t>(l_Size), '\0');
                ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), l_Result.data(), l_Size, nullptr, nullptr);

                return l_Result;
            }

            std::wstring ToWide(std::string_view text)
            {
                if (text.empty())
                {
                    return {};
                }

                const int l_Size = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
                std::wstring l_Result(static_cast<std::size_t>(l_Size), L'\0');
                ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), l_Result.data(), l_Size);

                return l_Result;
            }

            MonitorArea ToMonitorArea(const RECT& rectangle)
            {
                return { rectangle.left, rectangle.top, static_cast<std::uint32_t>(rectangle.right - rectangle.left), static_cast<std::uint32_t>(rectangle.bottom - rectangle.top) };
            }

            BOOL CALLBACK AddMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM data)
            {
                MONITORINFOEXW l_Info{};
                l_Info.cbSize = sizeof(l_Info);
                if (!::GetMonitorInfoW(monitor, &l_Info))
                {
                    return TRUE;
                }

                UINT l_DpiX = USER_DEFAULT_SCREEN_DPI;
                UINT l_DpiY = USER_DEFAULT_SCREEN_DPI;
                if (FAILED(::GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &l_DpiX, &l_DpiY)))
                {
                    l_DpiX = USER_DEFAULT_SCREEN_DPI;
                }

                MonitorInfo l_Monitor;
                l_Monitor.Name = ToUtf8(l_Info.szDevice);
                l_Monitor.Area = ToMonitorArea(l_Info.rcMonitor);
                l_Monitor.WorkArea = ToMonitorArea(l_Info.rcWork);
                l_Monitor.DpiScale = static_cast<float>(l_DpiX) / static_cast<float>(USER_DEFAULT_SCREEN_DPI);
                l_Monitor.Primary = (l_Info.dwFlags & MONITORINFOF_PRIMARY) != 0;

                reinterpret_cast<std::vector<MonitorInfo>*>(data)->push_back(std::move(l_Monitor));

                return TRUE;
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

        std::string GetClipboardText()
        {
            std::string l_Text;
            if (!::OpenClipboard(::GetActiveWindow()))
            {
                return l_Text;
            }

            if (const HANDLE l_Data = ::GetClipboardData(CF_UNICODETEXT))
            {
                if (const wchar_t* l_Wide = static_cast<const wchar_t*>(::GlobalLock(l_Data)))
                {
                    l_Text = ToUtf8(l_Wide);
                    ::GlobalUnlock(l_Data);
                }
            }

            ::CloseClipboard();

            return l_Text;
        }

        // The active window owns the clipboard, since a clipboard opened without an owner can refuse SetClipboardData
        void SetClipboardText(std::string_view text)
        {
            const std::wstring l_Wide = ToWide(text);
            if (!::OpenClipboard(::GetActiveWindow()))
            {
                return;
            }

            ::EmptyClipboard();

            const SIZE_T l_Bytes = (l_Wide.size() + 1) * sizeof(wchar_t);
            if (const HGLOBAL l_Memory = ::GlobalAlloc(GMEM_MOVEABLE, l_Bytes))
            {
                if (void* l_Destination = ::GlobalLock(l_Memory))
                {
                    std::memcpy(l_Destination, l_Wide.c_str(), l_Bytes);
                    ::GlobalUnlock(l_Memory);
                }

                if (::SetClipboardData(CF_UNICODETEXT, l_Memory) == nullptr)
                {
                    ::GlobalFree(l_Memory);
                }
            }

            ::CloseClipboard();
        }

        std::vector<MonitorInfo> GetMonitors()
        {
            std::vector<MonitorInfo> l_Monitors;
            ::EnumDisplayMonitors(nullptr, nullptr, &AddMonitor, reinterpret_cast<LPARAM>(&l_Monitors));
            std::ranges::stable_partition(l_Monitors, &MonitorInfo::Primary);

            return l_Monitors;
        }
    }
}
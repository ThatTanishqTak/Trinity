#include "Trinity/Core/Platform.hpp"

#include "Trinity/Core/Memory.hpp"
#include "Trinity/Platform/Windows/WindowsHeaders.hpp"

#include <shellapi.h>
#include <shellscalingapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <malloc.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <cwchar>
#include <format>
#include <optional>
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

            template<typename T>
            void Release(T*& object)
            {
                if (object != nullptr)
                {
                    object->Release();
                    object = nullptr;
                }
            }

            void CloseValidHandle(HANDLE& handle)
            {
                if (handle != nullptr && handle != INVALID_HANDLE_VALUE)
                {
                    ::CloseHandle(handle);
                }

                handle = nullptr;
            }

            // Quoted so the C runtime and CommandLineToArgvW read it back as it was: whole within quotes when it holds a space, a tab or a quote, each quote escaped, and the backslashes before a quote doubled
            std::wstring QuoteArgument(std::wstring_view argument)
            {
                if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring_view::npos)
                {
                    return std::wstring(argument);
                }

                std::wstring l_Quoted = L"\"";
                std::size_t l_Backslashes = 0;
                for (const wchar_t it_Character : argument)
                {
                    if (it_Character == L'\\')
                    {
                        ++l_Backslashes;

                        continue;
                    }

                    l_Quoted.append(it_Character == L'"' ? l_Backslashes * 2 + 1 : l_Backslashes, L'\\');
                    l_Quoted.push_back(it_Character);
                    l_Backslashes = 0;
                }

                l_Quoted.append(l_Backslashes * 2, L'\\');
                l_Quoted.push_back(L'"');

                return l_Quoted;
            }

            // A block of name=value entries sorted by name without regard to case, as CreateProcessW expects one
            std::wstring MakeEnvironmentBlock(const EnvironmentVariables& variables)
            {
                std::vector<std::pair<std::wstring, std::wstring>> l_Variables;
                l_Variables.reserve(variables.size());
                for (const auto& [it_Name, it_Value] : variables)
                {
                    l_Variables.emplace_back(ToWide(it_Name), ToWide(it_Value));
                }

                std::ranges::stable_sort(l_Variables, [](const auto& left, const auto& right) { return ::CompareStringOrdinal(left.first.data(), static_cast<int>(left.first.size()), right.first.data(), static_cast<int>(right.first.size()), TRUE) == CSTR_LESS_THAN; });

                std::wstring l_Block;
                for (const auto& [it_Name, it_Value] : l_Variables)
                {
                    l_Block += it_Name;
                    l_Block += L'=';
                    l_Block += it_Value;
                    l_Block += L'\0';
                }

                l_Block += L'\0';

                return l_Block;
            }

            // A line as a program wrote it: UTF-8 when it is valid UTF-8, and otherwise in the console's code page, which compilers write in when their output is not a console
            std::string DecodeOutputLine(std::string_view line)
            {
                if (line.empty() || ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, line.data(), static_cast<int>(line.size()), nullptr, 0) != 0)
                {
                    return std::string(line);
                }

                const int l_Size = ::MultiByteToWideChar(CP_OEMCP, 0, line.data(), static_cast<int>(line.size()), nullptr, 0);
                std::wstring l_Wide(static_cast<std::size_t>(l_Size), L'\0');
                ::MultiByteToWideChar(CP_OEMCP, 0, line.data(), static_cast<int>(line.size()), l_Wide.data(), l_Size);

                return ToUtf8(l_Wide);
            }

            // Bytes from a process gathered into lines, each handed on without its line end once it is whole
            class LineReader
            {
            public:
                explicit LineReader(const std::function<void(std::string_view)>& onLine) : m_OnLine(onLine)
                {

                }

                void Add(std::string_view bytes)
                {
                    for (const char it_Byte : bytes)
                    {
                        if (it_Byte == '\n')
                        {
                            Flush();
                        }
                        else
                        {
                            m_Line.push_back(it_Byte);
                        }
                    }
                }

                void Flush()
                {
                    if (!m_Line.empty() && m_Line.back() == '\r')
                    {
                        m_Line.pop_back();
                    }

                    if (m_OnLine)
                    {
                        m_OnLine(DecodeOutputLine(m_Line));
                    }

                    m_Line.clear();
                }

                void Finish()
                {
                    if (!m_Line.empty())
                    {
                        Flush();
                    }
                }

            private:
                const std::function<void(std::string_view)>& m_OnLine;
                std::string m_Line;
            };

            // A watched folder and the read Windows fills in the background, whose buffer must stay where it is until the read ends
            struct DirectoryWatch
            {
                HANDLE Directory = INVALID_HANDLE_VALUE;
                OVERLAPPED Overlapped{};
                bool Pending = false;
                alignas(DWORD) std::array<std::byte, 64 * 1024> Buffer{};
            };

            constexpr DWORD c_WatchFilter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE;

            void StartWatchRead(DirectoryWatch& watch)
            {
                watch.Overlapped = {};
                watch.Pending = ::ReadDirectoryChangesW(watch.Directory, watch.Buffer.data(), static_cast<DWORD>(watch.Buffer.size()), TRUE, c_WatchFilter, nullptr, &watch.Overlapped, nullptr) != FALSE;
            }

            std::optional<std::filesystem::path> ShowFileDialogWithCom(const FileDialogRequest& request)
            {
                const bool l_Save = request.Kind == FileDialogKind::Save;

                IFileDialog* l_Dialog = nullptr;
                if (FAILED(::CoCreateInstance(l_Save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, l_Save ? IID_IFileSaveDialog : IID_IFileOpenDialog, reinterpret_cast<void**>(&l_Dialog))))
                {
                    return std::nullopt;
                }

                FILEOPENDIALOGOPTIONS l_Options = 0;
                l_Dialog->GetOptions(&l_Options);
                l_Options |= FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR | FOS_PATHMUSTEXIST;
                if (request.Kind == FileDialogKind::Folder)
                {
                    l_Options |= FOS_PICKFOLDERS;
                }
                else if (request.Kind == FileDialogKind::Open)
                {
                    l_Options |= FOS_FILEMUSTEXIST;
                }
                else
                {
                    l_Options |= FOS_OVERWRITEPROMPT;
                }

                l_Dialog->SetOptions(l_Options);
                l_Dialog->SetTitle(ToWide(request.Title).c_str());

                // The strings must outlive SetFileTypes, which keeps pointers to them
                std::vector<std::wstring> l_Names;
                std::vector<std::wstring> l_Patterns;
                std::vector<COMDLG_FILTERSPEC> l_Filters;
                l_Names.reserve(request.Filters.size());
                l_Patterns.reserve(request.Filters.size());
                for (const FileDialogFilter& it_Filter : request.Filters)
                {
                    l_Names.push_back(ToWide(it_Filter.Name));
                    l_Patterns.push_back(L"*." + ToWide(it_Filter.Extension));
                    l_Filters.push_back({ l_Names.back().c_str(), l_Patterns.back().c_str() });
                }

                if (!l_Filters.empty())
                {
                    l_Dialog->SetFileTypes(static_cast<UINT>(l_Filters.size()), l_Filters.data());
                    if (l_Save)
                    {
                        l_Dialog->SetDefaultExtension(ToWide(request.Filters.front().Extension).c_str());
                    }
                }

                std::error_code l_Error;
                if (!request.Folder.empty() && std::filesystem::is_directory(request.Folder, l_Error))
                {
                    IShellItem* l_Folder = nullptr;
                    if (SUCCEEDED(::SHCreateItemFromParsingName(request.Folder.wstring().c_str(), nullptr, IID_IShellItem, reinterpret_cast<void**>(&l_Folder))))
                    {
                        l_Dialog->SetFolder(l_Folder);
                        Release(l_Folder);
                    }
                }

                if (l_Save && !request.FileName.empty())
                {
                    l_Dialog->SetFileName(ToWide(request.FileName).c_str());
                }

                std::optional<std::filesystem::path> l_Result;
                IShellItem* l_Item = nullptr;
                if (SUCCEEDED(l_Dialog->Show(static_cast<HWND>(request.Owner))) && SUCCEEDED(l_Dialog->GetResult(&l_Item)))
                {
                    PWSTR l_Path = nullptr;
                    if (SUCCEEDED(l_Item->GetDisplayName(SIGDN_FILESYSPATH, &l_Path)))
                    {
                        l_Result = std::filesystem::path(l_Path);
                        ::CoTaskMemFree(l_Path);
                    }

                    Release(l_Item);
                }

                Release(l_Dialog);

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

            bool s_ComInitialized = false;
        }

        // The main thread joins a single-threaded apartment before anything else uses COM, as the file dialogs and ShellExecute want. Audio would otherwise put it in the multithreaded apartment as it starts, and leaves a thread that already has one as it is
        void Initialize()
        {
            ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            ::SetConsoleOutputCP(CP_UTF8);
            s_ComInitialized = SUCCEEDED(::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
        }

        void Shutdown()
        {
            if (s_ComInitialized)
            {
                ::CoUninitialize();
                s_ComInitialized = false;
            }
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
            return GetExecutablePath().parent_path();
        }

        std::filesystem::path GetExecutablePath()
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

                    return std::filesystem::path(l_Path);
                }

                l_Path.resize(l_Path.size() * 2);
            }
        }

        std::uint32_t GetProcessID()
        {
            return static_cast<std::uint32_t>(::GetCurrentProcessId());
        }

        bool IsDebuggerAttached()
        {
            return ::IsDebuggerPresent() != FALSE;
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

        EnvironmentVariables GetEnvironment()
        {
            EnvironmentVariables l_Variables;
            LPWCH l_Block = ::GetEnvironmentStringsW();
            if (l_Block == nullptr)
            {
                return l_Variables;
            }

            for (const wchar_t* it_Entry = l_Block; *it_Entry != L'\0'; it_Entry += std::wcslen(it_Entry) + 1)
            {
                // Entries such as =C:=C:\Folder keep each drive's current folder, and are not variables
                const std::wstring_view l_Entry(it_Entry);
                const std::size_t l_Equals = l_Entry.find(L'=', 1);
                if (l_Entry.front() != L'=' && l_Equals != std::wstring_view::npos)
                {
                    l_Variables.emplace_back(ToUtf8(l_Entry.substr(0, l_Equals)), ToUtf8(l_Entry.substr(l_Equals + 1)));
                }
            }

            ::FreeEnvironmentStringsW(l_Block);

            return l_Variables;
        }

        // Started suspended in a job of its own, so cancelling ends everything it started, and handed only its output pipe and NUL for its input, so no other handle Forge has open leaks into it. Without a console window, which also keeps a compiler from opening one
        std::optional<int> RunProcess(const ProcessRequest& request, const std::function<void(std::string_view line)>& onLine, std::string& error)
        {
            std::wstring l_CommandLine = QuoteArgument(request.Executable.wstring());
            for (const std::string& it_Argument : request.Arguments)
            {
                l_CommandLine += L' ';
                l_CommandLine += QuoteArgument(ToWide(it_Argument));
            }

            const std::wstring l_Environment = request.Environment != nullptr ? MakeEnvironmentBlock(*request.Environment) : std::wstring();

            SECURITY_ATTRIBUTES l_Inherited{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
            HANDLE l_Read = nullptr;
            HANDLE l_Write = nullptr;
            if (!::CreatePipe(&l_Read, &l_Write, &l_Inherited, 0))
            {
                error = std::format("no pipe for its output: {}", GetErrorMessage(::GetLastError()));

                return std::nullopt;
            }

            ::SetHandleInformation(l_Read, HANDLE_FLAG_INHERIT, 0);
            HANDLE l_Null = ::CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &l_Inherited, OPEN_EXISTING, 0, nullptr);

            std::array<HANDLE, 2> l_Handles{ l_Write, l_Null };
            const DWORD l_HandleCount = l_Null != INVALID_HANDLE_VALUE ? 2 : 1;
            SIZE_T l_AttributeSize = 0;
            ::InitializeProcThreadAttributeList(nullptr, 1, 0, &l_AttributeSize);
            std::vector<std::byte> l_AttributeStorage(l_AttributeSize);
            LPPROC_THREAD_ATTRIBUTE_LIST l_Attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(l_AttributeStorage.data());
            const bool l_HasAttributes = ::InitializeProcThreadAttributeList(l_Attributes, 1, 0, &l_AttributeSize) != FALSE;
            if (l_HasAttributes)
            {
                ::UpdateProcThreadAttribute(l_Attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, l_Handles.data(), sizeof(HANDLE) * l_HandleCount, nullptr, nullptr);
            }

            STARTUPINFOEXW l_Startup{};
            l_Startup.StartupInfo.cb = sizeof(STARTUPINFOEXW);
            l_Startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
            l_Startup.StartupInfo.hStdInput = l_Null != INVALID_HANDLE_VALUE ? l_Null : nullptr;
            l_Startup.StartupInfo.hStdOutput = l_Write;
            l_Startup.StartupInfo.hStdError = l_Write;
            l_Startup.lpAttributeList = l_HasAttributes ? l_Attributes : nullptr;

            const std::wstring l_Directory = request.WorkingDirectory.wstring();
            PROCESS_INFORMATION l_Process{};
            const DWORD l_Flags = CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED | (l_HasAttributes ? EXTENDED_STARTUPINFO_PRESENT : 0);
            const BOOL l_Started = ::CreateProcessW(request.Executable.c_str(), l_CommandLine.data(), nullptr, nullptr, TRUE, l_Flags, request.Environment != nullptr ? const_cast<wchar_t*>(l_Environment.c_str()) : nullptr, l_Directory.empty() ? nullptr : l_Directory.c_str(), &l_Startup.StartupInfo, &l_Process);
            const DWORD l_StartError = ::GetLastError();

            if (l_HasAttributes)
            {
                ::DeleteProcThreadAttributeList(l_Attributes);
            }

            // Only the child holds the pipe's writing end now, so the pipe ends with it
            CloseValidHandle(l_Write);
            CloseValidHandle(l_Null);
            if (!l_Started)
            {
                CloseValidHandle(l_Read);
                error = GetErrorMessage(l_StartError);

                return std::nullopt;
            }

            HANDLE l_Job = ::CreateJobObjectW(nullptr, nullptr);
            if (l_Job != nullptr && !::AssignProcessToJobObject(l_Job, l_Process.hProcess))
            {
                CloseValidHandle(l_Job);
            }

            ::ResumeThread(l_Process.hThread);
            CloseValidHandle(l_Process.hThread);

            LineReader l_Lines(onLine);
            std::array<char, 4096> l_Buffer{};
            const auto a_Drain = [&]()
            {
                DWORD l_Available = 0;
                while (::PeekNamedPipe(l_Read, nullptr, 0, nullptr, &l_Available, nullptr) && l_Available > 0)
                {
                    DWORD l_Count = 0;
                    if (!::ReadFile(l_Read, l_Buffer.data(), std::min<DWORD>(l_Available, static_cast<DWORD>(l_Buffer.size())), &l_Count, nullptr) || l_Count == 0)
                    {
                        break;
                    }

                    l_Lines.Add(std::string_view(l_Buffer.data(), l_Count));
                }
            };

            // It counts as ended when the program does, with what it wrote before then read, since a server a compiler starts may hold the pipe for minutes
            bool l_Cancelled = false;
            while (true)
            {
                if (!l_Cancelled && request.Cancel != nullptr && request.Cancel->load(std::memory_order_relaxed))
                {
                    if (l_Job != nullptr)
                    {
                        ::TerminateJobObject(l_Job, 1);
                    }
                    else
                    {
                        ::TerminateProcess(l_Process.hProcess, 1);
                    }

                    l_Cancelled = true;
                }

                a_Drain();
                if (::WaitForSingleObject(l_Process.hProcess, 20) != WAIT_TIMEOUT)
                {
                    a_Drain();

                    break;
                }
            }

            l_Lines.Finish();

            DWORD l_ExitCode = 1;
            ::GetExitCodeProcess(l_Process.hProcess, &l_ExitCode);
            CloseValidHandle(l_Process.hProcess);
            CloseValidHandle(l_Read);
            CloseValidHandle(l_Job);

            return static_cast<int>(l_ExitCode);
        }

        // Inheriting no handle of Forge's, without a console window, and in a process group of its own, so it outlives Forge and a Ctrl+C in Forge's console never reaches it
        bool StartProcess(const ProcessRequest& request, std::string& error)
        {
            std::wstring l_CommandLine = QuoteArgument(request.Executable.wstring());
            for (const std::string& it_Argument : request.Arguments)
            {
                l_CommandLine += L' ';
                l_CommandLine += QuoteArgument(ToWide(it_Argument));
            }

            const std::wstring l_Environment = request.Environment != nullptr ? MakeEnvironmentBlock(*request.Environment) : std::wstring();
            const std::wstring l_Directory = request.WorkingDirectory.wstring();
            STARTUPINFOW l_Startup{};
            l_Startup.cb = sizeof(STARTUPINFOW);
            PROCESS_INFORMATION l_Process{};
            const DWORD l_Flags = CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_PROCESS_GROUP;
            if (!::CreateProcessW(request.Executable.c_str(), l_CommandLine.data(), nullptr, nullptr, FALSE, l_Flags, request.Environment != nullptr ? const_cast<wchar_t*>(l_Environment.c_str()) : nullptr, l_Directory.empty() ? nullptr : l_Directory.c_str(), &l_Startup, &l_Process))
            {
                error = GetErrorMessage(::GetLastError());

                return false;
            }

            CloseValidHandle(l_Process.hThread);
            CloseValidHandle(l_Process.hProcess);

            return true;
        }

        // As Explorer opens it, so a folder opens in Explorer and a file in the program set for its type
        bool OpenWithSystem(const std::filesystem::path& path, std::string& error)
        {
            const HINSTANCE l_Result = ::ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            if (reinterpret_cast<INT_PTR>(l_Result) <= 32)
            {
                error = GetErrorMessage(::GetLastError());

                return false;
            }

            return true;
        }

        // Watched through the folder's handle, opened so the files in it can still be changed, renamed and deleted
        void* OpenDirectoryWatch(const std::filesystem::path& directory, std::string& error)
        {
            const HANDLE l_Directory = ::CreateFileW(directory.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
            if (l_Directory == INVALID_HANDLE_VALUE)
            {
                error = GetErrorMessage(::GetLastError());

                return nullptr;
            }

            DirectoryWatch* l_Watch = Memory::New<DirectoryWatch>(MemoryTag::FileSystem);
            l_Watch->Directory = l_Directory;
            StartWatchRead(*l_Watch);
            if (!l_Watch->Pending)
            {
                error = GetErrorMessage(::GetLastError());
                ::CloseHandle(l_Directory);
                Memory::Delete(l_Watch);

                return nullptr;
            }

            return l_Watch;
        }

        // A read that finished is taken and the next started. Windows tells of an overflow by a read with nothing in it, or by failing the read, and a read that could not be started loses what happens until the next, so each counts as one
        bool ReadDirectoryWatch(void* watch, std::vector<std::filesystem::path>& changed)
        {
            DirectoryWatch& l_Watch = *static_cast<DirectoryWatch*>(watch);
            if (!l_Watch.Pending)
            {
                StartWatchRead(l_Watch);

                return l_Watch.Pending;
            }

            DWORD l_Bytes = 0;
            if (!::GetOverlappedResult(l_Watch.Directory, &l_Watch.Overlapped, &l_Bytes, FALSE))
            {
                if (::GetLastError() == ERROR_IO_INCOMPLETE)
                {
                    return false;
                }

                StartWatchRead(l_Watch);

                return true;
            }

            const bool l_Overflowed = l_Bytes == 0;
            for (DWORD it_Offset = 0; !l_Overflowed;)
            {
                const FILE_NOTIFY_INFORMATION& l_Information = *reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(l_Watch.Buffer.data() + it_Offset);
                changed.emplace_back(std::wstring_view(l_Information.FileName, l_Information.FileNameLength / sizeof(WCHAR)));
                if (l_Information.NextEntryOffset == 0)
                {
                    break;
                }

                it_Offset += l_Information.NextEntryOffset;
            }

            StartWatchRead(l_Watch);

            return l_Overflowed;
        }

        // A read still running is cancelled and waited for, since Windows writes into the buffer until it ends
        void CloseDirectoryWatch(void* watch)
        {
            DirectoryWatch* l_Watch = static_cast<DirectoryWatch*>(watch);
            if (l_Watch->Pending && ::CancelIoEx(l_Watch->Directory, &l_Watch->Overlapped))
            {
                DWORD l_Bytes = 0;
                ::GetOverlappedResult(l_Watch->Directory, &l_Watch->Overlapped, &l_Bytes, TRUE);
            }

            ::CloseHandle(l_Watch->Directory);
            Memory::Delete(l_Watch);
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

        std::optional<ScreenPoint> GetCursorPosition()
        {
            POINT l_Point{};
            if (!::GetCursorPos(&l_Point))
            {
                return std::nullopt;
            }

            return ScreenPoint{ l_Point.x, l_Point.y };
        }

        // WindowFromPoint asks each window with WM_NCHITTEST, so a window answering HTTRANSPARENT is skipped
        void* GetWindowAt(ScreenPoint point)
        {
            return ::WindowFromPoint({ point.X, point.Y });
        }

        void* GetFocusedWindow()
        {
            return ::GetForegroundWindow();
        }

        // Any of this process's windows, so moving between Forge's floating windows keeps it
        bool IsForeground()
        {
            DWORD l_Process = 0;
            const HWND l_Window = ::GetForegroundWindow();

            return l_Window != nullptr && ::GetWindowThreadProcessId(l_Window, &l_Process) != 0 && l_Process == ::GetCurrentProcessId();
        }

        void ShowErrorMessage(std::string_view title, std::string_view message)
        {
            ::MessageBoxW(nullptr, ToWide(message).c_str(), ToWide(title).c_str(), MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
        }

        bool HasFileDialogs()
        {
            return true;
        }

        // The dialogs need a single-threaded apartment. A thread that already has a different one still tries, and leaves it as it was
        std::optional<std::filesystem::path> ShowFileDialog(const FileDialogRequest& request)
        {
            const HRESULT l_Com = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            std::optional<std::filesystem::path> l_Result = ShowFileDialogWithCom(request);
            if (SUCCEEDED(l_Com))
            {
                ::CoUninitialize();
            }

            return l_Result;
        }
    }
}
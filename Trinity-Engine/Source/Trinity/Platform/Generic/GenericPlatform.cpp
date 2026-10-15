#include "Trinity/Core/Platform.hpp"

#include "Trinity/Core/Memory.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <dlfcn.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(TR_PLATFORM_MACOS)
#include <crt_externs.h>
#include <mach-o/dyld.h>
#include <sys/sysctl.h>
#include <sys/types.h>

#include <cstdint>
#else
extern "C" char** environ;
#endif

namespace Trinity
{
    namespace Platform
    {
        namespace
        {
            std::string s_Clipboard;

            // A shared library cannot name environ on macOS, so it asks for it
            char** GetEnvironmentBlock()
            {
#if defined(TR_PLATFORM_MACOS)
                return *::_NSGetEnviron();
#else
                return environ;
#endif
            }

            // A file's size and last write, or zeros for a folder, whose own time changes with what is in it
            struct WatchedEntry
            {
                std::uintmax_t Size = 0;
                std::filesystem::file_time_type Time{};

                [[nodiscard]] bool operator==(const WatchedEntry&) const = default;
            };

            using WatchedTree = std::map<std::filesystem::path, WatchedEntry>;

            // Without a notification the system offers everywhere, the folder is walked again at most this often and compared with the last walk
            constexpr std::chrono::milliseconds c_WatchInterval{ 500 };

            struct DirectoryWatch
            {
                std::filesystem::path Directory;
                WatchedTree Entries;
                std::chrono::steady_clock::time_point NextScan;
            };

            // Everything below the folder, by its path relative to it. One that goes while it is walked is left out, as it would be next time
            WatchedTree ScanDirectory(const std::filesystem::path& directory)
            {
                WatchedTree l_Entries;
                std::error_code l_Error;
                for (std::filesystem::recursive_directory_iterator it_Entry(directory, std::filesystem::directory_options::skip_permission_denied, l_Error), l_End; !l_Error && it_Entry != l_End; it_Entry.increment(l_Error))
                {
                    std::error_code l_EntryError;
                    WatchedEntry l_Entry;
                    if (!it_Entry->is_directory(l_EntryError))
                    {
                        l_Entry.Size = it_Entry->file_size(l_EntryError);
                        l_Entry.Time = it_Entry->last_write_time(l_EntryError);
                    }

                    if (!l_EntryError)
                    {
                        l_Entries.emplace(it_Entry->path().lexically_relative(directory), l_Entry);
                    }
                }

                return l_Entries;
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
                        m_OnLine(m_Line);
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

            // What execve takes, all made before a fork, since a process with threads may only make the few calls that are safe between the fork and the program running
            struct Program
            {
                std::string Executable;
                std::string Directory;
                std::vector<char*> Arguments;
                std::vector<std::string> EnvironmentEntries;
                std::vector<char*> EnvironmentBlock;
                char** Environment = nullptr;
            };

            // The program checked, so one that cannot start says why rather than failing in the child. Its strings point into the request and into itself, so it is not copied
            [[nodiscard]] bool PrepareProgram(const ProcessRequest& request, Program& program, std::string& error)
            {
                program.Executable = request.Executable.string();
                program.Directory = request.WorkingDirectory.string();
                if (::access(program.Executable.c_str(), X_OK) != 0)
                {
                    error = std::strerror(errno);

                    return false;
                }

                std::error_code l_DirectoryError;
                if (!program.Directory.empty() && !std::filesystem::is_directory(request.WorkingDirectory, l_DirectoryError))
                {
                    error = std::format("its folder {} does not exist", program.Directory);

                    return false;
                }

                program.Arguments.reserve(request.Arguments.size() + 2);
                program.Arguments.push_back(const_cast<char*>(program.Executable.c_str()));
                for (const std::string& it_Argument : request.Arguments)
                {
                    program.Arguments.push_back(const_cast<char*>(it_Argument.c_str()));
                }

                program.Arguments.push_back(nullptr);
                if (request.Environment == nullptr)
                {
                    program.Environment = GetEnvironmentBlock();

                    return true;
                }

                program.EnvironmentEntries.reserve(request.Environment->size());
                for (const auto& [it_Name, it_Value] : *request.Environment)
                {
                    program.EnvironmentEntries.push_back(std::format("{}={}", it_Name, it_Value));
                }

                for (std::string& it_Entry : program.EnvironmentEntries)
                {
                    program.EnvironmentBlock.push_back(it_Entry.data());
                }

                program.EnvironmentBlock.push_back(nullptr);
                program.Environment = program.EnvironmentBlock.data();

                return true;
            }

            // A program by its name in the folders of this process's PATH, or empty
            [[nodiscard]] std::filesystem::path FindOnPath(std::string_view name)
            {
                const char* l_Path = std::getenv("PATH");
                std::string_view l_Folders = l_Path != nullptr ? std::string_view(l_Path) : std::string_view();
                while (!l_Folders.empty())
                {
                    const std::size_t l_End = std::min(l_Folders.find(':'), l_Folders.size());
                    const std::string_view l_Folder = l_Folders.substr(0, l_End);
                    l_Folders.remove_prefix(std::min(l_End + 1, l_Folders.size()));
                    if (l_Folder.empty())
                    {
                        continue;
                    }

                    const std::filesystem::path l_Candidate = std::filesystem::path(l_Folder) / name;
                    if (::access(l_Candidate.c_str(), X_OK) == 0)
                    {
                        return l_Candidate;
                    }
                }

                return {};
            }
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
            return GetExecutablePath().parent_path();
        }

        std::filesystem::path GetExecutablePath()
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

            return std::filesystem::canonical(l_Path, l_Error);
#else
            return std::filesystem::read_symlink("/proc/self/exe", l_Error);
#endif
        }

        std::uint32_t GetProcessID()
        {
            return static_cast<std::uint32_t>(::getpid());
        }

        // On Linux the status file is read whole, since /proc makes it as it is read, and its TracerPid line names the process tracing this one, or 0
        bool IsDebuggerAttached()
        {
#if defined(TR_PLATFORM_MACOS)
            std::array<int, 4> l_Name{ CTL_KERN, KERN_PROC, KERN_PROC_PID, ::getpid() };
            kinfo_proc l_Info{};
            std::size_t l_Size = sizeof(l_Info);

            return ::sysctl(l_Name.data(), static_cast<u_int>(l_Name.size()), &l_Info, &l_Size, nullptr, 0) == 0 && (l_Info.kp_proc.p_flag & P_TRACED) != 0;
#else
            const int l_File = ::open("/proc/self/status", O_RDONLY | O_CLOEXEC);
            if (l_File < 0)
            {
                return false;
            }

            std::array<char, 4096> l_Buffer{};
            std::size_t l_Length = 0;
            while (l_Length < l_Buffer.size())
            {
                const ssize_t l_Read = ::read(l_File, l_Buffer.data() + l_Length, l_Buffer.size() - l_Length);
                if (l_Read <= 0)
                {
                    break;
                }

                l_Length += static_cast<std::size_t>(l_Read);
            }

            ::close(l_File);
            constexpr std::string_view c_Tracer = "\nTracerPid:";
            const std::string_view l_Status(l_Buffer.data(), l_Length);
            const std::size_t l_At = l_Status.find(c_Tracer);
            if (l_At == std::string_view::npos)
            {
                return false;
            }

            const std::size_t l_Digit = l_Status.find_first_not_of(" \t", l_At + c_Tracer.size());

            return l_Digit != std::string_view::npos && l_Status[l_Digit] >= '1' && l_Status[l_Digit] <= '9';
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

        EnvironmentVariables GetEnvironment()
        {
            EnvironmentVariables l_Variables;
            for (char** it_Entry = GetEnvironmentBlock(); it_Entry != nullptr && *it_Entry != nullptr; ++it_Entry)
            {
                const std::string_view l_Entry(*it_Entry);
                const std::size_t l_Equals = l_Entry.find('=');
                if (l_Equals != std::string_view::npos && l_Equals != 0)
                {
                    l_Variables.emplace_back(l_Entry.substr(0, l_Equals), l_Entry.substr(l_Equals + 1));
                }
            }

            return l_Variables;
        }


        // Everything the child needs is made before it is forked, since a process with threads may only make the few calls that are safe until the child runs the program. The child leads a process group of its own, so cancelling ends everything it started
        std::optional<int> RunProcess(const ProcessRequest& request, const std::function<void(std::string_view line)>& onLine, std::string& error)
        {
            Program l_Program;
            if (!PrepareProgram(request, l_Program, error))
            {
                return std::nullopt;
            }

            std::array<int, 2> l_Pipe{ -1, -1 };
            if (::pipe(l_Pipe.data()) != 0)
            {
                error = std::format("no pipe for its output: {}", std::strerror(errno));

                return std::nullopt;
            }

            ::fcntl(l_Pipe[0], F_SETFD, FD_CLOEXEC);
            ::fcntl(l_Pipe[1], F_SETFD, FD_CLOEXEC);
            ::fcntl(l_Pipe[0], F_SETFL, ::fcntl(l_Pipe[0], F_GETFL) | O_NONBLOCK);

            const pid_t l_Child = ::fork();
            if (l_Child < 0)
            {
                error = std::strerror(errno);
                ::close(l_Pipe[0]);
                ::close(l_Pipe[1]);

                return std::nullopt;
            }

            if (l_Child == 0)
            {
                ::setpgid(0, 0);
                const int l_Null = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
                if (l_Null >= 0)
                {
                    ::dup2(l_Null, STDIN_FILENO);
                }

                ::dup2(l_Pipe[1], STDOUT_FILENO);
                ::dup2(l_Pipe[1], STDERR_FILENO);
                if (!l_Program.Directory.empty() && ::chdir(l_Program.Directory.c_str()) != 0)
                {
                    ::_exit(127);
                }

                ::execve(l_Program.Executable.c_str(), l_Program.Arguments.data(), l_Program.Environment);
                ::_exit(127);
            }

            // Set here too, so a cancel straight after the fork finds the group
            ::setpgid(l_Child, l_Child);
            ::close(l_Pipe[1]);

            LineReader l_Lines(onLine);
            std::array<char, 4096> l_Buffer{};
            // False once the pipe has ended, as it does when every process that held it has
            const auto a_Drain = [&]()
            {
                while (true)
                {
                    const ssize_t l_Read = ::read(l_Pipe[0], l_Buffer.data(), l_Buffer.size());
                    if (l_Read > 0)
                    {
                        l_Lines.Add(std::string_view(l_Buffer.data(), static_cast<std::size_t>(l_Read)));

                        continue;
                    }

                    if (l_Read < 0 && errno == EINTR)
                    {
                        continue;
                    }

                    return l_Read != 0;
                }
            };

            int l_Status = 0;
            bool l_Cancelled = false;
            while (true)
            {
                if (!l_Cancelled && request.Cancel != nullptr && request.Cancel->load(std::memory_order_relaxed))
                {
                    ::kill(-l_Child, SIGTERM);
                    l_Cancelled = true;
                }

                pollfd l_Poll{ l_Pipe[0], POLLIN, 0 };
                const bool l_Open = ::poll(&l_Poll, 1, 50) <= 0 || a_Drain();

                pid_t l_Waited = 0;
                do
                {
                    l_Waited = ::waitpid(l_Child, &l_Status, l_Open ? WNOHANG : 0);
                } while (l_Waited < 0 && errno == EINTR);

                if (l_Waited == l_Child || l_Waited < 0)
                {
                    // What it wrote before it ended is read, and nothing a process it left running writes later
                    static_cast<void>(a_Drain());

                    break;
                }
            }

            ::close(l_Pipe[0]);
            l_Lines.Finish();

            if (WIFEXITED(l_Status))
            {
                return WEXITSTATUS(l_Status);
            }

            return WIFSIGNALED(l_Status) ? 128 + WTERMSIG(l_Status) : -1;
        }

        // Forked twice, so the program is no child of this process and is never waited for, in a session of its own so it outlives it, and with nothing of this process's open but /dev/null. Why it could not run comes back through a pipe that closes as it runs
        bool StartProcess(const ProcessRequest& request, std::string& error)
        {
            Program l_Program;
            if (!PrepareProgram(request, l_Program, error))
            {
                return false;
            }

            std::array<int, 2> l_Pipe{ -1, -1 };
            if (::pipe(l_Pipe.data()) != 0)
            {
                error = std::format("no pipe to hear from it: {}", std::strerror(errno));

                return false;
            }

            ::fcntl(l_Pipe[0], F_SETFD, FD_CLOEXEC);
            ::fcntl(l_Pipe[1], F_SETFD, FD_CLOEXEC);

            const pid_t l_Child = ::fork();
            if (l_Child < 0)
            {
                error = std::strerror(errno);
                ::close(l_Pipe[0]);
                ::close(l_Pipe[1]);

                return false;
            }

            if (l_Child == 0)
            {
                ::setsid();
                const pid_t l_Grandchild = ::fork();
                if (l_Grandchild != 0)
                {
                    ::_exit(l_Grandchild < 0 ? 1 : 0);
                }

                const int l_Null = ::open("/dev/null", O_RDWR | O_CLOEXEC);
                if (l_Null >= 0)
                {
                    ::dup2(l_Null, STDIN_FILENO);
                    ::dup2(l_Null, STDOUT_FILENO);
                    ::dup2(l_Null, STDERR_FILENO);
                }

                if (l_Program.Directory.empty() || ::chdir(l_Program.Directory.c_str()) == 0)
                {
                    ::execve(l_Program.Executable.c_str(), l_Program.Arguments.data(), l_Program.Environment);
                }

                const int l_Failure = errno;
                static_cast<void>(::write(l_Pipe[1], &l_Failure, sizeof(l_Failure)));
                ::_exit(127);
            }

            ::close(l_Pipe[1]);
            int l_Status = 0;
            while (::waitpid(l_Child, &l_Status, 0) < 0 && errno == EINTR)
            {

            }

            int l_Failure = 0;
            ssize_t l_Read = 0;
            do
            {
                l_Read = ::read(l_Pipe[0], &l_Failure, sizeof(l_Failure));
            } while (l_Read < 0 && errno == EINTR);

            ::close(l_Pipe[0]);
            if (!WIFEXITED(l_Status) || WEXITSTATUS(l_Status) != 0)
            {
                error = "it could not be forked";

                return false;
            }

            if (l_Read == static_cast<ssize_t>(sizeof(l_Failure)))
            {
                error = std::strerror(l_Failure);

                return false;
            }

            return true;
        }

        // Through the desktop's own opener, open on macOS and xdg-open elsewhere
        bool OpenWithSystem(const std::filesystem::path& path, std::string& error)
        {
#if defined(TR_PLATFORM_MACOS)
            const std::filesystem::path l_Opener = "/usr/bin/open";
#else
            const std::filesystem::path l_Opener = FindOnPath("xdg-open");
#endif
            if (l_Opener.empty())
            {
                error = "there is no xdg-open on the PATH, which opens files as the desktop does";

                return false;
            }

            const std::array<std::string, 1> l_Arguments{ path.string() };
            ProcessRequest l_Request;
            l_Request.Executable = l_Opener;
            l_Request.Arguments = l_Arguments;

            return StartProcess(l_Request, error);
        }

        void* OpenDirectoryWatch(const std::filesystem::path& directory, std::string& error)
        {
            std::error_code l_Error;
            if (!std::filesystem::is_directory(directory, l_Error))
            {
                error = "it is not a folder";

                return nullptr;
            }

            DirectoryWatch* l_Watch = Memory::New<DirectoryWatch>(MemoryTag::FileSystem);
            l_Watch->Directory = directory;
            l_Watch->Entries = ScanDirectory(directory);
            l_Watch->NextScan = std::chrono::steady_clock::now() + c_WatchInterval;

            return l_Watch;
        }

        // Every path added, removed, or changed in size or time since the last walk. A walk tells everything, so it never overflows
        bool ReadDirectoryWatch(void* watch, std::vector<std::filesystem::path>& changed)
        {
            DirectoryWatch& l_Watch = *static_cast<DirectoryWatch*>(watch);
            const std::chrono::steady_clock::time_point l_Now = std::chrono::steady_clock::now();
            if (l_Now < l_Watch.NextScan)
            {
                return false;
            }

            l_Watch.NextScan = l_Now + c_WatchInterval;
            WatchedTree l_Entries = ScanDirectory(l_Watch.Directory);
            auto a_Old = l_Watch.Entries.begin();
            auto a_New = l_Entries.begin();
            while (a_Old != l_Watch.Entries.end() || a_New != l_Entries.end())
            {
                if (a_New == l_Entries.end() || (a_Old != l_Watch.Entries.end() && a_Old->first < a_New->first))
                {
                    changed.push_back(a_Old->first);
                    ++a_Old;
                }
                else if (a_Old == l_Watch.Entries.end() || a_New->first < a_Old->first)
                {
                    changed.push_back(a_New->first);
                    ++a_New;
                }
                else
                {
                    if (a_Old->second != a_New->second)
                    {
                        changed.push_back(a_New->first);
                    }

                    ++a_Old;
                    ++a_New;
                }
            }

            l_Watch.Entries = std::move(l_Entries);

            return false;
        }

        void CloseDirectoryWatch(void* watch)
        {
            Memory::Delete(static_cast<DirectoryWatch*>(watch));
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

        // No windows until the Linux platform layer exists, so the log holds the message alone
        void ShowErrorMessage(std::string_view, std::string_view)
        {

        }

        // No dialogs until the Linux platform layer exists, so Forge asks for paths itself
        bool HasFileDialogs()
        {
            return false;
        }

        std::optional<std::filesystem::path> ShowFileDialog(const FileDialogRequest&)
        {
            return std::nullopt;
        }
    }
}
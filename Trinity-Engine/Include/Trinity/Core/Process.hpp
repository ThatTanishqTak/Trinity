#pragma once

#include "Trinity/Core/Expected.hpp"
#include "Trinity/Core/Export.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Trinity
{
    namespace Process
    {
        // UTF-8, in no particular order
        using Environment = std::vector<std::pair<std::string, std::string>>;
    }

    // Empty fields use this process's folder and environment. Cancel ends it, Detach leaves it running
    struct ProcessOptions
    {
        std::filesystem::path WorkingDirectory;
        std::optional<Process::Environment> Environment;
        const std::atomic<bool>* Cancel = nullptr;
        const std::atomic<bool>* Detach = nullptr;
        std::function<void(std::string_view line)> OnLine;
    };

    namespace Process
    {
        // Blocks until the program ends. A batch file must run through cmd.exe. OnLine gets UTF-8 lines
        [[nodiscard]] TRINITY_API Expected<int, std::string> Run(const std::filesystem::path& executable, std::span<const std::string> arguments, const ProcessOptions& options = {});
        // Returns at once. The program outlives this process, and its output goes nowhere
        [[nodiscard]] TRINITY_API Expected<void, std::string> Start(const std::filesystem::path& executable, std::span<const std::string> arguments, const std::filesystem::path& workingDirectory = {});
        // Opens it with the desktop's program for that kind of file, without waiting
        [[nodiscard]] TRINITY_API Expected<void, std::string> OpenWithSystem(const std::filesystem::path& path);
        // Moves the file, or the folder with all that is in it, to the desktop's Recycle Bin or Trash, from where it can be put back. Blocks until it is there. Where the system has no bin for it the file stays as it is, and the error says so
        [[nodiscard]] TRINITY_API Expected<void, std::string> MoveToTrash(const std::filesystem::path& path);

        [[nodiscard]] TRINITY_API Environment GetEnvironment();
        // Windows matches the name without regard to case
        [[nodiscard]] TRINITY_API std::optional<std::string> FindVariable(const Environment& environment, std::string_view name);
        TRINITY_API void SetVariable(Environment& environment, std::string_view name, std::string_view value);
        // Searches the environment's PATH, or this process's with none. Empty when not found
        [[nodiscard]] TRINITY_API std::filesystem::path FindExecutable(std::string_view name, const Environment* environment = nullptr);
        // Local AppData on Windows, Application Support on macOS, XDG_DATA_HOME elsewhere, or empty
        [[nodiscard]] TRINITY_API std::filesystem::path GetUserDataDirectory();
        // Windows' known folder wherever it was moved, xdg-user-dirs' elsewhere, or empty
        [[nodiscard]] TRINITY_API std::filesystem::path GetDocumentsDirectory();
        // False where the platform has no windows, or the process shows none
        TRINITY_API bool BringToFront(std::uint32_t processID);
        [[nodiscard]] TRINITY_API std::filesystem::path GetExecutableDirectory();
        [[nodiscard]] TRINITY_API std::filesystem::path GetExecutablePath();
        [[nodiscard]] TRINITY_API std::uint32_t GetCurrentID();
        // Native debuggers only. ManagedScripts::IsDebuggerAttached tells of a managed one
        [[nodiscard]] TRINITY_API bool IsDebuggerAttached();
        // Modal. Does nothing where the platform has no windows, so log the message too
        TRINITY_API void ShowErrorMessage(std::string_view title, std::string_view message);
    }

    // Destroying it leaves the program running. On Linux, Poll until it ends or it stays a zombie
    class TRINITY_API StartedProcess
    {
    public:
        StartedProcess() = default;
        ~StartedProcess();

        StartedProcess(StartedProcess&& other) noexcept;
        StartedProcess& operator=(StartedProcess&& other) noexcept;

        StartedProcess(const StartedProcess&) = delete;
        StartedProcess& operator=(const StartedProcess&) = delete;

        [[nodiscard]] static Expected<StartedProcess, std::string> Start(const std::filesystem::path& executable, std::span<const std::string> arguments, const std::filesystem::path& workingDirectory = {});

        // The exit code once it has ended. Never waits
        [[nodiscard]] std::optional<int> Poll();

        [[nodiscard]] bool IsValid() const { return m_Handle != nullptr; }
        [[nodiscard]] std::uint32_t GetID() const { return m_ID; }

    private:
        StartedProcess(void* handle, std::uint32_t id);

        void Release();

        void* m_Handle = nullptr;
        std::uint32_t m_ID = 0;
    };
}
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
    // Programs run to their end, such as a module's build, each call blocking until its program ends, so a job runs it, and programs started to run on their own, such as an IDE
    namespace Process
    {
        // Names and values in UTF-8, in no particular order
        using Environment = std::vector<std::pair<std::string, std::string>>;
    }

    // How a program runs: the folder it starts in, this process's when empty, the environment it gets, this process's when there is none, a flag that ends it and everything it started once set, and what is handed each line it writes
    struct ProcessOptions
    {
        std::filesystem::path WorkingDirectory;
        std::optional<Process::Environment> Environment;
        const std::atomic<bool>* Cancel = nullptr;
        std::function<void(std::string_view line)> OnLine;
    };

    namespace Process
    {
        // The program's exit code, or why it could not start. It is a program, not a script, so a batch file runs through cmd.exe. Its output and errors reach OnLine in the order written, a line at a time, in UTF-8 and without the line end. It ends when the program does, though a server it left running still holds its output
        [[nodiscard]] TRINITY_API Expected<int, std::string> Run(const std::filesystem::path& executable, std::span<const std::string> arguments, const ProcessOptions& options = {});
        // Starts the program and returns at once, neither waiting for it nor ending it as this process ends, with its output going nowhere. It is a program, as with Run, so a batch file runs through cmd.exe. Why it could not start, or nothing
        [[nodiscard]] TRINITY_API Expected<void, std::string> Start(const std::filesystem::path& executable, std::span<const std::string> arguments, const std::filesystem::path& workingDirectory = {});
        // The file or folder opened as the desktop would open it, by the program it has for that kind of file, or a folder in the file manager, without waiting
        [[nodiscard]] TRINITY_API Expected<void, std::string> OpenWithSystem(const std::filesystem::path& path);

        [[nodiscard]] TRINITY_API Environment GetEnvironment();
        // A variable's value, by its name, which Windows matches without regard to case, or none
        [[nodiscard]] TRINITY_API std::optional<std::string> FindVariable(const Environment& environment, std::string_view name);
        // Sets a variable, or adds it when the environment has none by that name
        TRINITY_API void SetVariable(Environment& environment, std::string_view name, std::string_view value);
        // A program by its name in the folders of the environment's PATH, or this process's with none, trying the extensions Windows runs when the name has none, or a path to one as it is. Empty when there is none
        [[nodiscard]] TRINITY_API std::filesystem::path FindExecutable(std::string_view name, const Environment* environment = nullptr);
        // The folder this process's executable is in, where Trinity's build puts the engine and what is built beside it
        [[nodiscard]] TRINITY_API std::filesystem::path GetExecutableDirectory();
        // This process's executable itself, which a debugger launches or attaches to
        [[nodiscard]] TRINITY_API std::filesystem::path GetExecutablePath();
        // The ID a debugger attaches to this process by
        [[nodiscard]] TRINITY_API std::uint32_t GetCurrentID();
        // Whether a native debugger is attached: the one Windows reports, or one tracing this process elsewhere. A debugger of managed code alone is not, which ManagedScripts::IsDebuggerAttached tells of
        [[nodiscard]] TRINITY_API bool IsDebuggerAttached();
        // A modal error box, for a failure the program cannot start past, as a game's that cannot find its pack. Nothing where the platform has no windows, so the caller logs the message too
        TRINITY_API void ShowErrorMessage(std::string_view title, std::string_view message);
    }
}
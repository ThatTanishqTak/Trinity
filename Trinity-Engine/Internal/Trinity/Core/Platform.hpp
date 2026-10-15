#pragma once

#include "Trinity/Core/FileDialog.hpp"

#include <atomic>
#include <cstddef>
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
    namespace Platform
    {
        struct ScreenPoint
        {
            std::int32_t X = 0;
            std::int32_t Y = 0;
        };

        struct MonitorArea
        {
            std::int32_t X = 0;
            std::int32_t Y = 0;
            std::uint32_t Width = 0;
            std::uint32_t Height = 0;
        };

        // Areas in screen pixels. The work area leaves out the taskbar and docked toolbars
        struct MonitorInfo
        {
            std::string Name;
            MonitorArea Area;
            MonitorArea WorkArea;
            float DpiScale = 1.0f;
            bool Primary = false;
        };

        enum class FileDialogKind : std::uint8_t
        {
            Open,
            Save,
            Folder
        };

        struct FileDialogRequest
        {
            FileDialogKind Kind = FileDialogKind::Open;
            std::string_view Title;
            std::span<const FileDialogFilter> Filters;
            std::filesystem::path Folder;
            std::string_view FileName;
            void* Owner = nullptr;
        };

        using EnvironmentVariables = std::vector<std::pair<std::string, std::string>>;

        // A program to run: its full path, its arguments, the folder it starts in, empty for this process's, and the environment it gets, this process's when there is none. A script is not a program, so a batch file runs through cmd.exe
        struct ProcessRequest
        {
            std::filesystem::path Executable;
            std::span<const std::string> Arguments;
            std::filesystem::path WorkingDirectory;
            const EnvironmentVariables* Environment = nullptr;
            // Read now and then while it runs, and true ends it and every process it started
            const std::atomic<bool>* Cancel = nullptr;
        };

        void Initialize();
        void Shutdown();

        [[nodiscard]] const char* GetName();

        [[nodiscard]] void* Allocate(std::size_t size, std::size_t alignment);
        void Free(void* memory);

        [[nodiscard]] std::filesystem::path GetUserDataDirectory();
        [[nodiscard]] std::filesystem::path GetExecutableDirectory();
        [[nodiscard]] std::filesystem::path GetExecutablePath();

        [[nodiscard]] std::uint32_t GetProcessID();
        // A native debugger: the one Windows reports, the tracer Linux names in /proc, or the flag macOS sets on a traced process
        [[nodiscard]] bool IsDebuggerAttached();

        // Null on failure, with the reason in error
        [[nodiscard]] void* LoadSharedLibrary(const std::filesystem::path& path, std::string& error);
        [[nodiscard]] void* GetSharedLibrarySymbol(void* library, const char* name);
        void UnloadSharedLibrary(void* library);

        // This process's environment, names and values in UTF-8
        [[nodiscard]] EnvironmentVariables GetEnvironment();

        // Runs the program to its end, blocking, and hands each line of its output and errors, in the order written, to onLine as UTF-8 without its line end. Its exit code, or none with the reason in error when it could not start. It counts as ended when the program itself ends, so a server it left running, such as MSVC's PDB server, cannot hold the call open
        [[nodiscard]] std::optional<int> RunProcess(const ProcessRequest& request, const std::function<void(std::string_view line)>& onLine, std::string& error);
        // Starts the program to run on its own, neither waited for nor ended with this process, with its output going nowhere, such as an IDE opened at a file. Cancel is not read. False with the reason in error when it could not start
        [[nodiscard]] bool StartProcess(const ProcessRequest& request, std::string& error);
        // The file or folder opened as the desktop would open it, with the program it has for that kind of file, or a folder in its file manager. False with the reason in error
        [[nodiscard]] bool OpenWithSystem(const std::filesystem::path& path, std::string& error);

        // A folder and everything below it, watched without blocking. Null on failure, with the reason in error. Each read appends the paths that changed since the last one, relative to the folder, and returns true when more changed than it could tell, so everything should be looked at again
        [[nodiscard]] void* OpenDirectoryWatch(const std::filesystem::path& directory, std::string& error);
        [[nodiscard]] bool ReadDirectoryWatch(void* watch, std::vector<std::filesystem::path>& changed);
        void CloseDirectoryWatch(void* watch);

        // UTF-8, and empty when the clipboard holds no text
        [[nodiscard]] std::string GetClipboardText();
        void SetClipboardText(std::string_view text);

        // Primary first, and empty where the platform has no windows
        [[nodiscard]] std::vector<MonitorInfo> GetMonitors();

        // In screen pixels, and empty where the platform has no cursor
        [[nodiscard]] std::optional<ScreenPoint> GetCursorPosition();

        // Native window handles, null for none. GetWindowAt skips windows that let the mouse pass through
        [[nodiscard]] void* GetWindowAt(ScreenPoint point);
        [[nodiscard]] void* GetFocusedWindow();

        // A modal error box, for a failure the application cannot start past. Nothing where the platform has no windows, so the caller logs the message too
        void ShowErrorMessage(std::string_view title, std::string_view message);

        // Blocks until the person chooses or cancels. Empty when cancelled, or where the platform has no dialogs
        [[nodiscard]] bool HasFileDialogs();
        [[nodiscard]] std::optional<std::filesystem::path> ShowFileDialog(const FileDialogRequest& request);
    }
}
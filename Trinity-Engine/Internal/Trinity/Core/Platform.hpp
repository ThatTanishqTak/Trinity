#pragma once

#include "Trinity/Core/FileDialog.hpp"
#include "Trinity/Core/Http.hpp"
#include "Trinity/Input/GamepadCodes.hpp"

#include <array>
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

        // A batch file is not a program, and must run through cmd.exe
        struct ProcessRequest
        {
            std::filesystem::path Executable;
            std::span<const std::string> Arguments;
            std::filesystem::path WorkingDirectory;
            const EnvironmentVariables* Environment = nullptr;
            // Polled while it runs. True ends it and every process it started
            const std::atomic<bool>* Cancel = nullptr;
            // Polled as Cancel is. True stops reading its output and returns, leaving it running
            const std::atomic<bool>* Detach = nullptr;
        };

        // Buttons is bit 1 << GamepadButton for each button held. Axes are raw, by GamepadAxis
        struct GamepadReading
        {
            std::uint32_t Buttons = 0;
            std::array<float, std::to_underlying(GamepadAxis::Count)> Axes{};
        };

        // Slots are below Input::c_MaxGamepads. A name is valid only during the call
        class GamepadListener
        {
        public:
            virtual ~GamepadListener() = default;

            virtual void OnGamepadConnected(std::uint32_t slot, std::string_view name) = 0;
            virtual void OnGamepadDisconnected(std::uint32_t slot) = 0;
            virtual void OnGamepadReading(std::uint32_t slot, const GamepadReading& reading) = 0;
        };

        void Initialize();
        void Shutdown();

        [[nodiscard]] const char* GetName();

        [[nodiscard]] void* Allocate(std::size_t size, std::size_t alignment);
        void Free(void* memory);

        [[nodiscard]] std::filesystem::path GetUserDataDirectory();
        // Local AppData on Windows, ~/Library/Caches on macOS, XDG_CACHE_HOME elsewhere, or empty
        [[nodiscard]] std::filesystem::path GetUserCacheDirectory();
        [[nodiscard]] std::filesystem::path GetDocumentsDirectory();
        [[nodiscard]] std::filesystem::path GetExecutableDirectory();
        [[nodiscard]] std::filesystem::path GetExecutablePath();

        [[nodiscard]] std::uint32_t GetProcessID();
        // Native debuggers only
        [[nodiscard]] bool IsDebuggerAttached();

        // Null on failure, with the reason in error
        [[nodiscard]] void* LoadSharedLibrary(const std::filesystem::path& path, std::string& error);
        [[nodiscard]] void* GetSharedLibrarySymbol(void* library, const char* name);
        void UnloadSharedLibrary(void* library);

        // Names and values in UTF-8
        [[nodiscard]] EnvironmentVariables GetEnvironment();

        // Blocks. Ends when the program itself does, so a server it left running cannot hold the call open
        [[nodiscard]] std::optional<int> RunProcess(const ProcessRequest& request, const std::function<void(std::string_view line)>& onLine, std::string& error);
        // Neither waited for nor ended with this process. Cancel is not read
        [[nodiscard]] bool StartProcess(const ProcessRequest& request, std::string& error);
        // Null with the reason in error when it could not start
        [[nodiscard]] void* StartWatchedProcess(const ProcessRequest& request, std::uint32_t& processID, std::string& error);
        // On Linux the call that sees the program end also reaps it
        [[nodiscard]] std::optional<int> PollProcess(void* process);
        void ReleaseProcess(void* process);
        [[nodiscard]] bool OpenWithSystem(const std::filesystem::path& path, std::string& error);
        // False where the platform has no windows, or the process shows none
        bool BringProcessToFront(std::uint32_t processID);

        // Null with error empty when another process holds the lock. The system releases it at exit
        [[nodiscard]] void* TryLockFile(const std::filesystem::path& path, std::string_view text, std::string& error);
        void ReleaseFileLock(void* lock);
        [[nodiscard]] std::optional<std::string> ReadLockedFile(const std::filesystem::path& path);

        // Always on Windows, and elsewhere where libcurl could be loaded
        [[nodiscard]] bool HasHttp();
        // Follows redirects. Returning false from sink stops it. The response carries no body
        [[nodiscard]] Expected<HttpResponse, HttpError> HttpGet(const HttpRequest& request, const std::function<bool(std::span<const std::byte>)>& sink);

        // Null on failure. A read returns true when more changed than it could tell, so rescan everything
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
        // False where the platform has no windows. Input reads gamepads only while it is true
        [[nodiscard]] bool IsForeground();

        // A pad keeps its slot while connected. Where the platform's API is missing, one line is logged
        void InitializeGamepads();
        void ShutdownGamepads();
        // Main thread only. Reports pads gone, then pads come, then readings oldest first
        void PollGamepads(GamepadListener& listener, bool resync);
        // Each motor from 0 to 1, held until changed
        void SetGamepadRumble(std::uint32_t slot, float lowFrequency, float highFrequency);
        // True where the platform has nothing to install
        [[nodiscard]] bool HasGamepadRuntime();

        // Modal. Does nothing where the platform has no windows, so log the message too
        void ShowErrorMessage(std::string_view title, std::string_view message);

        // The dialogs block until the person chooses or cancels, and return empty when cancelled
        [[nodiscard]] bool HasFileDialogs();
        [[nodiscard]] std::optional<std::filesystem::path> ShowFileDialog(const FileDialogRequest& request);
    }
}
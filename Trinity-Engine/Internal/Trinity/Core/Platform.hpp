#pragma once

#include "Trinity/Core/FileDialog.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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

        void Initialize();
        void Shutdown();

        [[nodiscard]] const char* GetName();

        [[nodiscard]] void* Allocate(std::size_t size, std::size_t alignment);
        void Free(void* memory);

        [[nodiscard]] std::filesystem::path GetUserDataDirectory();
        [[nodiscard]] std::filesystem::path GetExecutableDirectory();

        // Null on failure, with the reason in error
        [[nodiscard]] void* LoadSharedLibrary(const std::filesystem::path& path, std::string& error);
        [[nodiscard]] void* GetSharedLibrarySymbol(void* library, const char* name);
        void UnloadSharedLibrary(void* library);

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

        // Blocks until the person chooses or cancels. Empty when cancelled, or where the platform has no dialogs
        [[nodiscard]] bool HasFileDialogs();
        [[nodiscard]] std::optional<std::filesystem::path> ShowFileDialog(const FileDialogRequest& request);
    }
}
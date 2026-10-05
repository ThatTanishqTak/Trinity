#include "Trinity/Core/FileDialog.hpp"

#include "Trinity/Core/Application.hpp"
#include "Trinity/Core/Platform.hpp"
#include "Trinity/Core/Window.hpp"

namespace Trinity
{
    namespace
    {
        // Owned by the main window, so the dialog stays above it and the window waits for it
        std::optional<std::filesystem::path> Show(Platform::FileDialogRequest request)
        {
            request.Owner = Application::Get().GetWindow().GetNativeHandle();

            return Platform::ShowFileDialog(request);
        }
    }

    namespace FileDialog
    {
        bool IsSupported()
        {
            return Platform::HasFileDialogs();
        }

        std::optional<std::filesystem::path> OpenFile(std::string_view title, std::span<const FileDialogFilter> filters, const std::filesystem::path& folder)
        {
            return Show({ Platform::FileDialogKind::Open, title, filters, folder, {}, nullptr });
        }

        std::optional<std::filesystem::path> SaveFile(std::string_view title, std::span<const FileDialogFilter> filters, const std::filesystem::path& folder, std::string_view fileName)
        {
            return Show({ Platform::FileDialogKind::Save, title, filters, folder, fileName, nullptr });
        }

        std::optional<std::filesystem::path> PickFolder(std::string_view title, const std::filesystem::path& folder)
        {
            return Show({ Platform::FileDialogKind::Folder, title, {}, folder, {}, nullptr });
        }
    }
}
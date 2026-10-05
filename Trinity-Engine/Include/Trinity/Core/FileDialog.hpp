#pragma once

#include "Trinity/Core/Export.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <string_view>

namespace Trinity
{
    struct FileDialogFilter
    {
        std::string_view Name;
        std::string_view Extension;
    };

    namespace FileDialog
    {
        [[nodiscard]] TRINITY_API bool IsSupported();

        [[nodiscard]] TRINITY_API std::optional<std::filesystem::path> OpenFile(std::string_view title, std::span<const FileDialogFilter> filters, const std::filesystem::path& folder = {});
        [[nodiscard]] TRINITY_API std::optional<std::filesystem::path> SaveFile(std::string_view title, std::span<const FileDialogFilter> filters, const std::filesystem::path& folder = {}, std::string_view fileName = {});
        [[nodiscard]] TRINITY_API std::optional<std::filesystem::path> PickFolder(std::string_view title, const std::filesystem::path& folder = {});
    }
}
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

// How Hub shows paths, which it keeps as UTF-8 in its files and in ImGui, and dates
namespace HubText
{
    [[nodiscard]] std::string ToUtf8(const std::filesystem::path& path);
    [[nodiscard]] std::filesystem::path FromUtf8(std::string_view text);
    // The day of a date in UTC as a manifest gives it, 2026-11-06T12:53:00Z as 6 Nov 2026, or unknown for none
    [[nodiscard]] std::string FormatDate(std::string_view date);
}
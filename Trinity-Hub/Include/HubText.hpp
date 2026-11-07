#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

// How Hub shows paths, which it keeps as UTF-8 in its files and in ImGui, and dates
namespace HubText
{
    [[nodiscard]] std::string ToUtf8(const std::filesystem::path& path);
    [[nodiscard]] std::filesystem::path FromUtf8(std::string_view text);
    // The day of a date in UTC as a manifest gives it, 2026-11-06T12:53:00Z as 6 Nov 2026, or unknown for none
    [[nodiscard]] std::string FormatDate(std::string_view date);

    // A time in UTC as manifests, GitHub and Hub's own files write one, 2026-11-07T05:41:29Z
    [[nodiscard]] std::optional<std::chrono::sys_seconds> ParseTime(std::string_view text);
    [[nodiscard]] std::string FormatTime(std::chrono::sys_seconds time);
    // The day and the minute, as 7 Nov 2026 at 05:41 UTC
    [[nodiscard]] std::string DescribeTime(std::chrono::sys_seconds time);
}
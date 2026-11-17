#pragma once

#include "Trinity/Core/Export.hpp"

#include <compare>
#include <cstddef>
#include <string>
#include <string_view>

namespace Trinity
{
    // project(VERSION) of the root CMakeLists.txt
    [[nodiscard]] TRINITY_API const char* GetVersionString();
    // All 40 characters, or empty for none
    [[nodiscard]] TRINITY_API std::string_view GetBuildCommit();
    [[nodiscard]] TRINITY_API bool IsBuildModified();
    // The commit's date in UTC, as 2026-11-06T12:53:00Z, or empty for none
    [[nodiscard]] TRINITY_API std::string_view GetBuildDate();
    // As TRINITY_PLATFORM names it, such as Windows, and as CMake names it, such as x86_64
    [[nodiscard]] TRINITY_API std::string_view GetBuildPlatform();
    [[nodiscard]] TRINITY_API std::string_view GetBuildArchitecture();

    // As git and GitHub show a commit
    constexpr std::size_t c_ShortCommitLength = 7;

    // As 209a33d, with ", modified" after it when the tree had changes
    [[nodiscard]] TRINITY_API std::string GetBuildCommitDescription();
    // As 0.11.0 (209a33d)
    [[nodiscard]] TRINITY_API std::string GetVersionDescription();

    // As Semantic Versioning orders them, a pre-release before its release
    [[nodiscard]] TRINITY_API std::strong_ordering CompareVersions(std::string_view left, std::string_view right);
}
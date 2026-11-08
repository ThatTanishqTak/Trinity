#pragma once

#include "Trinity/Core/Export.hpp"

#include <compare>
#include <cstddef>
#include <string>
#include <string_view>

namespace Trinity
{
    // The engine as its build made it: its version, which is project(VERSION) in the root CMakeLists.txt, and the commit it was built from, whether that tree had changes, and the commit's date, which the build reads from git as it starts. A tree that is not a git checkout gives no commit and no date. Two builds of one version from different commits are the same version
    [[nodiscard]] TRINITY_API const char* GetVersionString();
    // All 40 characters, or empty for none
    [[nodiscard]] TRINITY_API std::string_view GetBuildCommit();
    [[nodiscard]] TRINITY_API bool IsBuildModified();
    // The commit's date in UTC, as 2026-11-06T12:53:00Z, or empty for none
    [[nodiscard]] TRINITY_API std::string_view GetBuildDate();
    // The platform this build runs on, as TRINITY_PLATFORM names it, such as Windows or Linux, and its architecture as CMake names it, such as x86_64, which a version's manifest names too, so Hub knows which versions this machine can run
    [[nodiscard]] TRINITY_API std::string_view GetBuildPlatform();
    [[nodiscard]] TRINITY_API std::string_view GetBuildArchitecture();

    // How many of the commit's characters name it where it is shown short, as git and GitHub show it
    constexpr std::size_t c_ShortCommitLength = 7;

    // The commit short, then modified when the tree had changes, as 209a33d or 209a33d, modified, or no commit
    [[nodiscard]] TRINITY_API std::string GetBuildCommitDescription();
    // The version with its commit, as 0.11.0 (209a33d), which titles and logs show
    [[nodiscard]] TRINITY_API std::string GetVersionDescription();

    // Versions as Semantic Versioning orders them: major, minor and patch by number, a pre-release, whose version carries a hyphen, before its release, and its identifiers each by number or text. Hub orders its versions by it, and Forge refuses a project of a version newer than its own
    [[nodiscard]] TRINITY_API std::strong_ordering CompareVersions(std::string_view left, std::string_view right);
}
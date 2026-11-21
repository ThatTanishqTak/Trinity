#pragma once

#include <Trinity/Project/Project.hpp>

// The files an IDE debugs scripts in Forge with: launch.json and launchSettings.json. Main thread
namespace DebuggerSettings
{
    // Each file is written while missing or while its first line is still Forge's, so edits are kept
    void Write(const Trinity::Project& project);
}
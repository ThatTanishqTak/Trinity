#pragma once

#include <Trinity/Project/Project.hpp>

// The settings an IDE debugs the scripts in Forge with, written into the project as it opens and as it gets a language: .vscode/launch.json for VS Code, in the project's folder and in Source with a C++ module, since VS Code opens either, and Scripts/Properties/launchSettings.json with C# scripts, which Visual Studio and Rider start Forge from under their debugger, Visual Studio debugging native and managed code at once. Each launches this Forge on the project or attaches to a Forge running. Main thread only
namespace DebuggerSettings
{
    // Each file is written while it is missing or its first line is still Forge's, which says so, and kept as it is once someone has taken that line out, so changes of one's own stay
    void Write(const Trinity::Project& project);
}
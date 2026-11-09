#pragma once

#include <Trinity.hpp>

#include <atomic>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The tools a project's scripts and a game's player are built with, found as Forge's builds find them, so Hub says what a version needs on this machine without building anything: the .NET SDK, the C++ compiler, on Windows Visual Studio's through vswhere, CMake and Ninja, and on Windows the Vulkan loader a driver installs. Each tool is asked through its own program, so nothing here starts .NET
namespace Trinity::Tools
{
    // The oldest CMake Trinity builds with
    inline constexpr std::string_view c_MinimumCMake = "3.30";

    // A C++ compiler, by the name CMake gives its kind, MSVC, Clang, GNU or AppleClang, with its version and where it is, and on Windows the Visual Studio it comes with, the vcvars64.bat that sets it up and its toolset
    struct Compiler
    {
        std::string Id;
        std::string Version;
        std::filesystem::path Path;
        std::filesystem::path VisualStudio;
        std::filesystem::path VcVars;
        std::string Toolset;
    };

    // What a C++ build runs in, and on Windows the vcvars64.bat it was set up from, at the toolset it was asked for, or at Visual Studio's default for none
    struct BuildEnvironment
    {
        Process::Environment Variables;
        std::filesystem::path VcVars;
        std::string Toolset;
    };

    // dotnet: on the PATH, then where DOTNET_ROOT points, then where its installers put it. Empty when there is none
    [[nodiscard]] std::filesystem::path FindDotNet(const Process::Environment& environment);
    // dotnet's messages asked for in English, which a build's output is read in, and none of its logo or first-run text
    void SetDotNetVariables(Process::Environment& environment);
    // Each SDK's version dotnet --list-sdks names, which the dotnet host answers alone, starting no runtime
    [[nodiscard]] Expected<std::vector<std::string>, std::string> ListDotNetSdks(const std::filesystem::path& dotnet, const Process::Environment& environment, const std::atomic<bool>* cancel = nullptr);
    // The SDK of those installed that a global.json of this version and rollForward picks, as dotnet picks it, or none. An empty rollForward is latestPatch, as global.json's is, and a preview SDK is picked only for a preview version
    [[nodiscard]] std::optional<std::string> PickDotNetSdk(std::span<const std::string> installed, std::string_view version, std::string_view rollForward);
    // The SDKs PickDotNetSdk would take for them, as 10.0.100 or a newer 10.0 SDK, for the person to read
    [[nodiscard]] std::string DescribeDotNetSdks(std::string_view version, std::string_view rollForward);

    // As the first line of cmake --version names it, and as ninja --version writes it. None when the program did not run or named none
    [[nodiscard]] std::optional<std::string> ReadCMakeVersion(const std::filesystem::path& cmake, const Process::Environment& environment, const std::atomic<bool>* cancel = nullptr);
    [[nodiscard]] std::optional<std::string> ReadNinjaVersion(const std::filesystem::path& ninja, const Process::Environment& environment, const std::atomic<bool>* cancel = nullptr);
    // A compiler's kind and version from what it writes: cl's banner, which it writes run with nothing, and the first line of --version for the others
    [[nodiscard]] Expected<Compiler, std::string> ReadCompiler(const std::filesystem::path& compiler, const Process::Environment& environment, const std::atomic<bool>* cancel = nullptr);
    // Whether two compilers are the same kind at the same major version, which TrinityConfig.cmake asks of a module's or player's compiler against the engine's
    [[nodiscard]] bool IsSameKindAndMajor(std::string_view leftId, std::string_view leftVersion, std::string_view rightId, std::string_view rightVersion);

    // The C++ compiler a version's TrinityToolchain.cmake hands a module's build, the one the version was built with, by its path. Empty when the file names none
    [[nodiscard]] std::filesystem::path ReadToolchainCompiler(const std::filesystem::path& toolchain);

    // Windows: the Visual Studio a build uses, the one preferredCompiler is in, at that compiler's toolset, while it is there, else the newest with the C++ tools, as vswhere finds it, at its default toolset. Its Compiler names that toolset's cl.exe, which is not run here. Elsewhere there is none
    [[nodiscard]] Expected<Compiler, std::string> FindVisualStudio(const std::filesystem::path& preferredCompiler, const std::atomic<bool>* cancel = nullptr);
    // On Windows what FindVisualStudio's vcvars64.bat leaves, which takes seconds, so a caller keeps it, and elsewhere this process's environment. Visual Studio's messages are asked for in English, which a build's output is read in
    [[nodiscard]] Expected<BuildEnvironment, std::string> FindBuildEnvironment(const std::filesystem::path& preferredCompiler, const std::atomic<bool>* cancel = nullptr);
    // The compiler a module's or player's build uses in that environment: preferredCompiler while it is there, as a version's toolchain hands it to CMake, else the one CMake finds, cl on Windows, CXX and then c++ elsewhere
    [[nodiscard]] Expected<Compiler, std::string> FindCompiler(const std::filesystem::path& preferredCompiler, const Process::Environment& environment, const std::atomic<bool>* cancel = nullptr);

    // Windows' Vulkan loader, vulkan-1.dll in System32, which a graphics driver or the Vulkan runtime installs. Empty when there is none, and on every other platform, where it is not looked for
    [[nodiscard]] std::filesystem::path FindVulkanLoader();
}
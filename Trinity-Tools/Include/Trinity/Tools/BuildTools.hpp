#pragma once

#include <Trinity.hpp>

#include <atomic>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The tools scripts and players are built with, found as Forge's builds find them. Starts no .NET
namespace Trinity::Tools
{
    // The oldest CMake Trinity builds with
    inline constexpr std::string_view c_MinimumCMake = "3.30";

    // By the name CMake gives its kind, with its version and path, and on Windows its Visual Studio
    struct Compiler
    {
        std::string Id;
        std::string Version;
        std::filesystem::path Path;
        std::filesystem::path VisualStudio;
        std::filesystem::path VcVars;
        std::string Toolset;
    };

    // What a C++ build runs in, and on Windows the vcvars64.bat it was set up from, at its toolset
    struct BuildEnvironment
    {
        Process::Environment Variables;
        std::filesystem::path VcVars;
        std::string Toolset;
    };

    // dotnet: on the PATH, then where DOTNET_ROOT points, then where its installers put it, or empty
    [[nodiscard]] std::filesystem::path FindDotNet(const Process::Environment& environment);
    // dotnet's messages asked for in English, which a build's output is read in, without its logo
    void SetDotNetVariables(Process::Environment& environment);
    // Each SDK's version dotnet --list-sdks names, which the host answers alone, starting no runtime
    [[nodiscard]] Expected<std::vector<std::string>, std::string> ListDotNetSdks(const std::filesystem::path& dotnet, const Process::Environment& environment, const std::atomic<bool>* cancel = nullptr);
    // The installed SDK a global.json of this version and rollForward picks, as dotnet does, or none
    [[nodiscard]] std::optional<std::string> PickDotNetSdk(std::span<const std::string> installed, std::string_view version, std::string_view rollForward);
    // The SDKs PickDotNetSdk would take for them, as 10.0.100 or a newer 10.0 SDK, for a person to read
    [[nodiscard]] std::string DescribeDotNetSdks(std::string_view version, std::string_view rollForward);

    // As the first line of cmake --version names it, and as ninja --version writes it, or none
    [[nodiscard]] std::optional<std::string> ReadCMakeVersion(const std::filesystem::path& cmake, const Process::Environment& environment, const std::atomic<bool>* cancel = nullptr);
    [[nodiscard]] std::optional<std::string> ReadNinjaVersion(const std::filesystem::path& ninja, const Process::Environment& environment, const std::atomic<bool>* cancel = nullptr);
    // A compiler's kind and version from what it writes: cl's banner, or the first line of --version
    [[nodiscard]] Expected<Compiler, std::string> ReadCompiler(const std::filesystem::path& compiler, const Process::Environment& environment, const std::atomic<bool>* cancel = nullptr);
    // Whether two compilers are the same kind at the same major version, as TrinityConfig.cmake asks
    [[nodiscard]] bool IsSameKindAndMajor(std::string_view leftId, std::string_view leftVersion, std::string_view rightId, std::string_view rightVersion);

    // The C++ compiler a version's TrinityToolchain.cmake names, by its path. Empty when it names none
    [[nodiscard]] std::filesystem::path ReadToolchainCompiler(const std::filesystem::path& toolchain);

    // Windows: the Visual Studio preferredCompiler is in, else the newest with C++ tools. Else none
    [[nodiscard]] Expected<Compiler, std::string> FindVisualStudio(const std::filesystem::path& preferredCompiler, const std::atomic<bool>* cancel = nullptr);
    // On Windows what vcvars64.bat leaves, which takes seconds, and elsewhere this process's own
    [[nodiscard]] Expected<BuildEnvironment, std::string> FindBuildEnvironment(const std::filesystem::path& preferredCompiler, const std::atomic<bool>* cancel = nullptr);
    // preferredCompiler while it is there, else the one CMake finds: cl on Windows, else CXX then c++
    [[nodiscard]] Expected<Compiler, std::string> FindCompiler(const std::filesystem::path& preferredCompiler, const Process::Environment& environment, const std::atomic<bool>* cancel = nullptr);

    // Windows' Vulkan loader, vulkan-1.dll in System32. Empty if there is none, and on other platforms
    [[nodiscard]] std::filesystem::path FindVulkanLoader();
}
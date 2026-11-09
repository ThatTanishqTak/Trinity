#pragma once

#include "HubInstalls.hpp"

#include <Trinity.hpp>
#include <Trinity/Tools/BuildTools.hpp>

#include <atomic>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// What this machine has for each version's projects, found through Trinity-Tools as that version's Forge finds it: the .NET SDK band its C# builds with, and for C++ and for exports the compiler its manifest names, CMake 3.30 or newer and Ninja, and on Windows the GameInput runtime and a Vulkan loader, which Forge runs without. The tools are looked for on a job as Hub starts, on Check Again and once a version names a compiler not looked at yet, since Visual Studio's environment takes seconds, and each version's needs are read against what was found
class HubRequirements
{
public:
    // A tool a version needs, found or not
    struct Row
    {
        std::string Name;
        // Which one the version needs, as 3.30 or newer
        std::string Wanted;
        // As a language's refusal names it, as Clang 17 or CMake 3.30 or newer
        std::string Short;
        // What it is for
        std::string Purpose;
        bool Found = false;
        // What was found and where, or why nothing was
        std::string Detail;
        // Where to get one, for one missing
        std::string Remedy;
        // What to know of one found, as a compiler of another minor version than the version was built with
        std::string Note;
        // Without it a language's scripts cannot build, so New Project greys that language. A row stopping neither is only noted
        bool StopsCSharp = false;
        bool StopsCpp = false;
    };

    // A version's rows, and what they come to for each language
    struct Report
    {
        std::vector<Row> Rows;
        // Why a language's scripts cannot build with the version here, or empty when they can
        std::string CSharpRefusal;
        std::string CppRefusal;
        // What a C# project could not be exported without, though its scripts build
        std::string CSharpExportNote;

        [[nodiscard]] std::size_t CountMissing() const;
        [[nodiscard]] const std::string& GetRefusal(Trinity::ScriptLanguage language) const;
    };

    HubRequirements() = default;
    ~HubRequirements();

    HubRequirements(const HubRequirements&) = delete;
    HubRequirements& operator=(const HubRequirements&) = delete;

    // Every tool looked for again, on a job, for these versions, each version's toolchain read again. One asked for while a look is running starts once it ends
    void Check(const HubInstalls& installs);
    // Once a frame: a look taken once it ends, and another started once a version listed since names a compiler not looked at yet
    void Update(const HubInstalls& installs);

    [[nodiscard]] bool IsChecking() const { return m_Looking != nullptr; }
    // None until the first look ends, or for a version listed since, until its compiler is looked at
    [[nodiscard]] std::optional<Report> GetReport(const HubInstall& install) const;

private:
    // What a C++ build in a version's Forge would use, found once for each compiler the versions were built with, as on Windows each is set up from its own Visual Studio
    struct CppTools
    {
        std::string EnvironmentError;
        std::optional<Trinity::Tools::Compiler> Compiler;
        std::string CompilerError;
        std::filesystem::path CMake;
        std::optional<std::string> CMakeVersion;
        std::filesystem::path Ninja;
        std::optional<std::string> NinjaVersion;
        // Which builds C++ where Ninja is not there, but on Windows
        std::filesystem::path Make;
    };

    struct Machine
    {
        std::filesystem::path DotNet;
        std::optional<std::vector<std::string>> Sdks;
        std::string SdksError;
        std::map<std::filesystem::path, CppTools> Cpp;
        std::filesystem::path VulkanLoader;
        bool GamepadRuntime = true;
    };

    // Written by the job before Done
    struct Looking
    {
        std::vector<std::filesystem::path> Compilers;
        std::atomic<bool> Stop = false;
        std::atomic<bool> Done = false;
        Machine Found;
    };

    static void Look(Looking& looking);
    // What a version's TrinityToolchain.cmake hands a module's build, from the first of its configurations that has one
    [[nodiscard]] static std::filesystem::path FindPreferredCompiler(const HubInstall& install);
    [[nodiscard]] static Report Read(const Machine& machine, const HubInstall& install, const std::filesystem::path& compiler);

    std::optional<Machine> m_Machine;
    // Each version's compiler, by its folder, as its toolchain names it, read once it is listed
    std::map<std::filesystem::path, std::filesystem::path> m_Compilers;
    std::unique_ptr<Looking> m_Looking;
    bool m_LookAgain = false;
    Trinity::JobCounter m_Jobs;
};
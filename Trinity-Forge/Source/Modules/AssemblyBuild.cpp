#include "Modules/AssemblyBuild.hpp"

#include "Modules/BuildOutput.hpp"

#include <chrono>
#include <format>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
    using Clock = std::chrono::steady_clock;

    std::filesystem::path FromUtf8(std::string_view text)
    {
        return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
    }

    std::string ToUtf8(const std::filesystem::path& path)
    {
        const std::u8string l_Text = path.u8string();

        return std::string(reinterpret_cast<const char*>(l_Text.data()), l_Text.size());
    }

    // On the PATH, then where DOTNET_ROOT points, then where the installers put it
    std::filesystem::path FindDotNet(const Trinity::Process::Environment& environment)
    {
        if (std::filesystem::path l_Found = Trinity::Process::FindExecutable("dotnet", &environment); !l_Found.empty())
        {
            return l_Found;
        }

#if defined(TR_PLATFORM_WINDOWS)
        constexpr std::string_view c_Executable = "dotnet.exe";
        const std::vector<std::filesystem::path> l_Folders{ FromUtf8(Trinity::Process::FindVariable(environment, "DOTNET_ROOT").value_or("")), FromUtf8(Trinity::Process::FindVariable(environment, "ProgramFiles").value_or("C:\\Program Files")) / "dotnet" };
#else
        constexpr std::string_view c_Executable = "dotnet";
        const std::vector<std::filesystem::path> l_Folders{ FromUtf8(Trinity::Process::FindVariable(environment, "DOTNET_ROOT").value_or("")), "/usr/local/share/dotnet", "/usr/share/dotnet", "/usr/lib/dotnet" };
#endif
        std::error_code l_Error;
        for (const std::filesystem::path& it_Folder : l_Folders)
        {
            if (!it_Folder.empty() && std::filesystem::exists(it_Folder / c_Executable, l_Error))
            {
                return it_Folder / c_Executable;
            }
        }

        return {};
    }

    // This Forge's own platform, which a player built beside it runs on
    std::string_view GetRuntimeIdentifier()
    {
#if defined(_M_ARM64) || defined(__aarch64__)
        constexpr bool c_Arm64 = true;
#else
        constexpr bool c_Arm64 = false;
#endif
#if defined(TR_PLATFORM_WINDOWS)
        return c_Arm64 ? "win-arm64" : "win-x64";
#elif defined(TR_PLATFORM_MACOS)
        return c_Arm64 ? "osx-arm64" : "osx-x64";
#else
        return c_Arm64 ? "linux-arm64" : "linux-x64";
#endif
    }

    // A NativeAOT publish keeps its files apart from Forge's builds, so neither builds the other again
    std::filesystem::path GetCompiledArtifacts(const std::filesystem::path& projectDirectory)
    {
        return projectDirectory / "Build" / "ScriptsAot";
    }

    // dotnet's messages asked for in English, which the output is read in, and none of its first-run text
    bool BuildAssembly(const AssemblyBuild::Request& request, const std::atomic<bool>& cancel, BuildOutput& output)
    {
        Trinity::Process::Environment l_Environment = Trinity::Process::GetEnvironment();
        Trinity::Process::SetVariable(l_Environment, "DOTNET_NOLOGO", "1");
        Trinity::Process::SetVariable(l_Environment, "DOTNET_SKIP_FIRST_TIME_EXPERIENCE", "1");
        Trinity::Process::SetVariable(l_Environment, "DOTNET_CLI_UI_LANGUAGE", "en");

        const std::filesystem::path l_DotNet = FindDotNet(l_Environment);
        if (l_DotNet.empty())
        {
            output.LogError(std::format("Build: the .NET 10 SDK was not found, so the C# scripts cannot be built. Install it from https://dotnet.microsoft.com/download/dotnet/10.0, or point DOTNET_ROOT at it"));

            return false;
        }

        const std::filesystem::path l_Scripting = AssemblyBuild::GetScriptingDirectory();
        std::error_code l_Error;
        if (!std::filesystem::exists(l_Scripting / "Trinity-ScriptCore.dll", l_Error) || !std::filesystem::exists(l_Scripting / "Trinity-ScriptGenerator.dll", l_Error))
        {
            output.LogError(std::format("Build: {} has no Trinity-ScriptCore.dll and Trinity-ScriptGenerator.dll, so nothing can be built against them. Build the Trinity-ScriptCore and Trinity-ScriptGenerator targets", ToUtf8(l_Scripting)));

            return false;
        }

        // MSBuild's nodes would outlive the build and hold its output open, so none is left running, and the console logger writes plain lines, which the output is read a line at a time in
        const std::filesystem::path l_Project = request.ScriptsDirectory / FromUtf8(request.Name + ".csproj");
        std::vector<std::string> l_Arguments{ request.AheadOfTime ? "publish" : "build", ToUtf8(l_Project), "--nologo", "--verbosity", "quiet", "-tl:off", "-clp:NoSummary", "-nodeReuse:false", std::format("-p:TrinityScriptingDirectory={}", ToUtf8(l_Scripting)) };
        if (!request.AheadOfTime)
        {
            l_Arguments.insert(l_Arguments.end(), { "--configuration", std::string(AssemblyBuild::GetConfiguration()) });
        }
        else
        {
            // PublishAot and NativeLib are read before any project file is, so they come from here, and the targets beside ScriptCore compile it in and export its way in
            const std::filesystem::path l_Targets = l_Scripting / "Trinity-ScriptCore.Aot.targets";
            if (!std::filesystem::exists(l_Targets, l_Error))
            {
                output.LogError(std::format("Build: {} has no Trinity-ScriptCore.Aot.targets, so the scripts cannot be compiled ahead of time. Build the Trinity-ScriptCore target", ToUtf8(l_Scripting)));

                return false;
            }

            l_Arguments.insert(l_Arguments.end(), { "--configuration", "Release", "--runtime", std::string(GetRuntimeIdentifier()), "--output", ToUtf8(AssemblyBuild::GetCompiledPath(request.ProjectDirectory, request.Name).parent_path()), "-p:PublishAot=true", "-p:NativeLib=Shared", std::format("-p:ArtifactsPath={}", ToUtf8(GetCompiledArtifacts(request.ProjectDirectory))), std::format("-p:CustomAfterMicrosoftCommonTargets={}", ToUtf8(l_Targets)) });
        }

        Trinity::ProcessOptions l_Options;
        l_Options.WorkingDirectory = request.ScriptsDirectory;
        l_Options.Environment = std::move(l_Environment);
        l_Options.Cancel = &cancel;
        l_Options.OnLine = [&output](std::string_view line) { output.Read(line); };

        if (request.AheadOfTime)
        {
            TR_INFO("Build: compiling {} ahead of time with NativeAOT for {}", request.Name, GetRuntimeIdentifier());
        }
        else
        {
            TR_INFO("Build: building {} for {}", request.Name, AssemblyBuild::GetConfiguration());
        }

        const Trinity::Expected<int, std::string> l_ExitCode = Trinity::Process::Run(l_DotNet, l_Arguments, l_Options);
        output.Finish();
        if (!l_ExitCode)
        {
            output.LogError(std::format("Build: {}", l_ExitCode.GetError()));
        }

        return l_ExitCode && *l_ExitCode == 0 && !cancel.load(std::memory_order_relaxed);
    }
}

// The job ends with the build it runs, so it cannot outlive this
AssemblyBuild::~AssemblyBuild()
{
    Cancel();
}

void AssemblyBuild::Start(Request request)
{
    TR_ASSERT(!m_Running, "A C# build is already running");

    m_Running = std::make_unique<Running>();
    m_Running->Work = std::move(request);

    Running* l_Running = m_Running.get();
    Trinity::JobSystem::Submit([l_Running] { Run(*l_Running); }, &m_Jobs);
}

std::optional<AssemblyBuild::Result> AssemblyBuild::Update()
{
    if (!m_Running || !m_Running->Done.load(std::memory_order_acquire))
    {
        return std::nullopt;
    }

    Trinity::JobSystem::Wait(m_Jobs);
    const Result l_Result = m_Running->Outcome;
    m_Running.reset();

    return l_Result;
}

void AssemblyBuild::Cancel()
{
    if (!m_Running)
    {
        return;
    }

    m_Running->Cancel.store(true, std::memory_order_relaxed);
    Trinity::JobSystem::Wait(m_Jobs);
    m_Running.reset();
}

// C# works in every configuration, and every Forge builds the scripts in Debug, which a debugger steps through line by line and shows every field and local of, and which an IDE builds by default, so Forge loads its builds too
std::string_view AssemblyBuild::GetConfiguration()
{
    return "Debug";
}

std::filesystem::path AssemblyBuild::GetScriptingDirectory()
{
    return Trinity::Process::GetExecutableDirectory() / "Scripting";
}

// The artifacts layout names the configuration in lower case
std::filesystem::path AssemblyBuild::GetAssemblyPath(const std::filesystem::path& projectDirectory, std::string_view name)
{
    std::string l_Configuration(GetConfiguration());
    for (char& it_Character : l_Configuration)
    {
        it_Character = static_cast<char>(it_Character >= 'A' && it_Character <= 'Z' ? it_Character - 'A' + 'a' : it_Character);
    }

    return projectDirectory / "Build" / "Scripts" / "bin" / FromUtf8(name) / FromUtf8(l_Configuration) / FromUtf8(std::format("{}.dll", name));
}

std::filesystem::path AssemblyBuild::GetCompiledPath(const std::filesystem::path& projectDirectory, std::string_view name)
{
#if defined(TR_PLATFORM_WINDOWS)
    constexpr std::string_view c_Extension = ".dll";
#elif defined(TR_PLATFORM_MACOS)
    constexpr std::string_view c_Extension = ".dylib";
#else
    constexpr std::string_view c_Extension = ".so";
#endif

    return GetCompiledArtifacts(projectDirectory) / "publish" / FromUtf8(std::format("{}{}", name, c_Extension));
}

void AssemblyBuild::Run(Running& running)
{
    const Clock::time_point l_Start = Clock::now();
    const Request& l_Request = running.Work;
    BuildOutput l_Output(l_Request.ProjectDirectory, l_Request.ScriptsDirectory, l_Request.ScriptsDirectory);

    const bool l_Built = BuildAssembly(l_Request, running.Cancel, l_Output);
    Result& l_Result = running.Outcome;
    std::error_code l_Error;
    l_Result.Cancelled = running.Cancel.load(std::memory_order_relaxed);
    l_Result.Assembly = l_Request.AheadOfTime ? GetCompiledPath(l_Request.ProjectDirectory, l_Request.Name) : GetAssemblyPath(l_Request.ProjectDirectory, l_Request.Name);
    l_Result.Succeeded = l_Built && !l_Result.Cancelled && std::filesystem::exists(l_Result.Assembly, l_Error);
    l_Result.Errors = l_Output.GetErrors();
    l_Result.Warnings = l_Output.GetWarnings();
    l_Result.Seconds = std::chrono::duration<double>(Clock::now() - l_Start).count();

    if (l_Result.Cancelled)
    {
        TR_INFO("Build: {} was cancelled", l_Request.Name);
    }
    else if (l_Result.Succeeded)
    {
        TR_INFO("Build: {} {} in {:.1f} s{}", l_Request.Name, l_Request.AheadOfTime ? "compiled ahead of time" : "built", l_Result.Seconds, l_Result.Warnings > 0 ? std::format(", with {} warning(s)", l_Result.Warnings) : std::string());
    }
    else
    {
        l_Output.LogError(std::format("Build: {} failed after {:.1f} s, with {} error(s) and {} warning(s)", l_Request.Name, l_Result.Seconds, l_Result.Errors, l_Result.Warnings));
    }

    l_Result.ErrorLines = l_Output.GetErrorLines();

    running.Done.store(true, std::memory_order_release);
}
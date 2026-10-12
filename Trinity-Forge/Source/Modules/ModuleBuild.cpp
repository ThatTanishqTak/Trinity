#include "Modules/ModuleBuild.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <format>
#include <fstream>
#include <mutex>
#include <regex>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
    using Clock = std::chrono::steady_clock;

    // The oldest CMake Trinity builds with
    constexpr std::array<int, 2> c_MinimumCMake{ 3, 30 };

    std::filesystem::path FromUtf8(std::string_view text)
    {
        return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
    }

    std::string ToUtf8(const std::filesystem::path& path)
    {
        const std::u8string l_Text = path.u8string();

        return std::string(reinterpret_cast<const char*>(l_Text.data()), l_Text.size());
    }

    // With forward slashes, as CMake and the Console show paths
    std::string ToGenericUtf8(const std::filesystem::path& path)
    {
        const std::u8string l_Text = path.generic_u8string();

        return std::string(reinterpret_cast<const char*>(l_Text.data()), l_Text.size());
    }

    std::uint32_t ToNumber(const std::ssub_match& match)
    {
        const std::string l_Text = match.str();
        std::uint32_t l_Number = 0;
        std::from_chars(l_Text.data(), l_Text.data() + l_Text.size(), l_Number);

        return l_Number;
    }

    // A warning or an error a tool reported, with where it is when it said
    struct Diagnostic
    {
        std::string File;
        std::uint32_t Line = 0;
        std::uint32_t Column = 0;
        bool Error = false;
        std::string Code;
        std::string Message;
    };

    // Build output read a line at a time. A compiler's or linker's warning or error, as MSVC, Clang and GCC write them, is logged with its file and line, as is CMake's, with the indented lines below it that say what it means. Every other line goes to the log at trace, and those that still say they are an error at error
    class OutputReader
    {
    public:
        OutputReader(std::filesystem::path projectDirectory, std::filesystem::path sourceDirectory, std::filesystem::path buildDirectory) : m_Project(std::move(projectDirectory)), m_Source(std::move(sourceDirectory)), m_Build(std::move(buildDirectory))
        {

        }

        void Read(std::string_view text)
        {
            const std::string l_Line(text);
            if (m_CMake)
            {
                // CMake indents its message below its first line, with blank lines around it, and ends it with a line that is not indented
                if (l_Line.empty())
                {
                    return;
                }

                if (l_Line.starts_with("  "))
                {
                    const std::size_t l_Start = l_Line.find_first_not_of(' ');
                    m_CMake->Message += m_CMake->Message.empty() ? "" : " ";
                    m_CMake->Message += l_Line.substr(l_Start);

                    return;
                }

                Finish();
            }

            static const std::regex c_CMake(R"(^CMake (Error|Warning)(?: \(dev\))?(?: at (.+?):(\d+)(?: \([^)]*\))?)?:\s*(.*)$)");
            static const std::regex c_Msvc(R"(^\s*(.+?)\((\d+)(?:,(\d+))?\)\s*:\s*(fatal error|error|warning)\s*([A-Za-z]+\d+)?\s*:\s*(.*)$)");
            static const std::regex c_MsvcLinker(R"(^(.+?)\s*:\s*(fatal error|error|warning)\s+(LNK\d+)\s*:\s*(.*)$)");
            static const std::regex c_Gnu(R"(^(.+?):(\d+):(?:(\d+):)?\s*(fatal error|error|warning):\s*(.*)$)");

            std::smatch l_Match;
            if (std::regex_match(l_Line, l_Match, c_CMake))
            {
                // CMake names a file in the sources by its path from the source folder
                const std::filesystem::path l_File = FromUtf8(l_Match[2].str());
                m_CMake = Diagnostic{ l_Match[2].matched && l_File.is_relative() ? ToUtf8(m_Source / l_File) : l_Match[2].str(), ToNumber(l_Match[3]), 0, l_Match[1].str() == "Error", {}, l_Match[4].str() };
            }
            else if (std::regex_match(l_Line, l_Match, c_Msvc))
            {
                Report({ l_Match[1].str(), ToNumber(l_Match[2]), ToNumber(l_Match[3]), l_Match[4].str() != "warning", l_Match[5].str(), l_Match[6].str() });
            }
            else if (std::regex_match(l_Line, l_Match, c_MsvcLinker))
            {
                Report({ l_Match[1].str(), 0, 0, l_Match[2].str() != "warning", l_Match[3].str(), l_Match[4].str() });
            }
            else if (std::regex_match(l_Line, l_Match, c_Gnu))
            {
                Report({ l_Match[1].str(), ToNumber(l_Match[2]), ToNumber(l_Match[3]), l_Match[4].str() != "warning", {}, l_Match[5].str() });
            }
            else if (l_Line.find("error:") != std::string::npos || l_Line.find("undefined reference") != std::string::npos)
            {
                ++m_Errors;
                TR_ERROR("Build: {}", l_Line);
            }
            else if (!l_Line.empty())
            {
                TR_TRACE("Build: {}", l_Line);
            }
        }

        // A CMake message still open is logged
        void Finish()
        {
            if (std::optional<Diagnostic> l_Message = std::exchange(m_CMake, std::nullopt))
            {
                Report(*l_Message);
            }
        }

        [[nodiscard]] std::uint32_t GetErrors() const { return m_Errors; }
        [[nodiscard]] std::uint32_t GetWarnings() const { return m_Warnings; }

    private:
        // As Build: Source/Player.cpp(12,5): error C2065: message, the file relative to the project when it is in it
        void Report(const Diagnostic& diagnostic)
        {
            std::string l_Text = "Build: ";
            if (!diagnostic.File.empty())
            {
                l_Text += ShowPath(diagnostic.File);
                if (diagnostic.Line != 0)
                {
                    l_Text += diagnostic.Column != 0 ? std::format("({},{})", diagnostic.Line, diagnostic.Column) : std::format("({})", diagnostic.Line);
                }

                l_Text += ": ";
            }

            l_Text += diagnostic.Error ? "error" : "warning";
            if (!diagnostic.Code.empty())
            {
                l_Text += ' ';
                l_Text += diagnostic.Code;
            }

            l_Text += ": ";
            l_Text += diagnostic.Message;
            if (diagnostic.Error)
            {
                ++m_Errors;
                TR_ERROR("{}", l_Text);
            }
            else
            {
                ++m_Warnings;
                TR_WARN("{}", l_Text);
            }
        }

        // A relative path is the build tree's, where the compilers run
        [[nodiscard]] std::string ShowPath(std::string_view text) const
        {
            std::filesystem::path l_Path = FromUtf8(text);
            if (l_Path.is_relative())
            {
                l_Path = m_Build / l_Path;
            }

            l_Path = l_Path.lexically_normal();
            const std::filesystem::path l_Relative = l_Path.lexically_relative(m_Project);
            if (!l_Relative.empty() && *l_Relative.begin() != "..")
            {
                return ToGenericUtf8(l_Relative);
            }

            return ToUtf8(l_Path);
        }

        std::filesystem::path m_Project;
        std::filesystem::path m_Source;
        std::filesystem::path m_Build;
        std::optional<Diagnostic> m_CMake;
        std::uint32_t m_Errors = 0;
        std::uint32_t m_Warnings = 0;
    };

#if defined(TR_PLATFORM_WINDOWS)
    // The Visual Studio Forge was built with, found from its compiler's path, at the toolset it was built with. Otherwise the latest with the C++ tools, which vswhere finds. Its environment is what vcvars64.bat leaves, read from a batch file that runs it then lists every variable
    Trinity::Expected<Trinity::Process::Environment, std::string> FindMsvcEnvironment(const std::atomic<bool>* cancel)
    {
        std::filesystem::path l_VcVars;
        std::string l_Toolset;
#if defined(TR_FORGE_CXX_COMPILER)
        // Forge's compiler is <Visual Studio>/VC/Tools/MSVC/<toolset>/bin/Hostx64/x64/cl.exe
        const std::filesystem::path l_Compiler = FromUtf8(TR_FORGE_CXX_COMPILER);
        for (std::filesystem::path it_Folder = l_Compiler.parent_path(); it_Folder.has_relative_path(); it_Folder = it_Folder.parent_path())
        {
            const std::filesystem::path l_Tools = it_Folder.parent_path();
            if (l_Tools.filename() == "MSVC" && l_Tools.parent_path().filename() == "Tools" && l_Tools.parent_path().parent_path().filename() == "VC")
            {
                l_Toolset = ToUtf8(it_Folder.filename());
                l_VcVars = l_Tools.parent_path().parent_path() / "Auxiliary" / "Build" / "vcvars64.bat";

                break;
            }
        }
#endif

        std::error_code l_Error;
        const Trinity::Process::Environment l_Environment = Trinity::Process::GetEnvironment();
        if (l_VcVars.empty() || !std::filesystem::exists(l_VcVars, l_Error))
        {
            l_Toolset.clear();
            const std::string l_ProgramFiles = Trinity::Process::FindVariable(l_Environment, "ProgramFiles(x86)").value_or("C:\\Program Files (x86)");
            const std::filesystem::path l_VsWhere = FromUtf8(l_ProgramFiles) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe";
            std::string l_Installation;
            Trinity::ProcessOptions l_Options;
            l_Options.Cancel = cancel;
            l_Options.OnLine = [&l_Installation](std::string_view line)
            {
                if (l_Installation.empty() && !line.empty())
                {
                    l_Installation = line;
                }
            };

            const std::vector<std::string> l_Arguments{ "-latest", "-prerelease", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath" };
            if (!std::filesystem::exists(l_VsWhere, l_Error) || !Trinity::Process::Run(l_VsWhere, l_Arguments, l_Options) || l_Installation.empty())
            {
                return Trinity::Unexpected{ std::string("Visual Studio with its C++ tools was not found, so the module cannot be built. Install the Desktop development with C++ workload") };
            }

            l_VcVars = FromUtf8(l_Installation) / "VC" / "Auxiliary" / "Build" / "vcvars64.bat";
        }

        const std::filesystem::path l_Batch = std::filesystem::temp_directory_path(l_Error) / "TrinityModuleEnvironment.bat";
        {
            std::ofstream l_File(l_Batch, std::ios::binary | std::ios::trunc);
            l_File << "@call \"" << ToUtf8(l_VcVars) << "\"" << (l_Toolset.empty() ? std::string() : " -vcvars_ver=" + l_Toolset) << " >nul\r\n@if errorlevel 1 exit /b 1\r\n@set\r\n";
            if (!l_File)
            {
                return Trinity::Unexpected{ std::format("{} could not be written", ToUtf8(l_Batch)) };
            }
        }

        Trinity::Process::Environment l_Result;
        Trinity::ProcessOptions l_Options;
        l_Options.Cancel = cancel;
        l_Options.OnLine = [&l_Result](std::string_view line)
        {
            const std::size_t l_Equals = line.find('=');
            if (l_Equals != std::string_view::npos && l_Equals != 0)
            {
                l_Result.emplace_back(line.substr(0, l_Equals), line.substr(l_Equals + 1));
            }
        };

        const std::filesystem::path l_Shell = FromUtf8(Trinity::Process::FindVariable(l_Environment, "ComSpec").value_or("C:\\Windows\\System32\\cmd.exe"));
        const std::vector<std::string> l_Arguments{ "/d", "/c", ToUtf8(l_Batch) };
        const Trinity::Expected<int, std::string> l_Ran = Trinity::Process::Run(l_Shell, l_Arguments, l_Options);
        std::filesystem::remove(l_Batch, l_Error);
        if (!l_Ran || *l_Ran != 0 || !Trinity::Process::FindVariable(l_Result, "INCLUDE"))
        {
            return Trinity::Unexpected{ std::format("{} did not set up the compiler{}", ToUtf8(l_VcVars), l_Ran ? std::string() : ": " + l_Ran.GetError()) };
        }

        TR_INFO("Build: using the compiler environment of {}{}", ToUtf8(l_VcVars), l_Toolset.empty() ? std::string() : std::format(" at toolset {}", l_Toolset));

        return l_Result;
    }
#endif

    // Found once a session, since vcvars64.bat takes seconds, with Visual Studio's messages asked for in English, which the output is read in
    Trinity::Expected<Trinity::Process::Environment, std::string> GetBuildEnvironment(const std::atomic<bool>* cancel)
    {
        static std::mutex s_Mutex;
        static std::optional<Trinity::Process::Environment> s_Environment;

        std::lock_guard l_Lock(s_Mutex);
        if (!s_Environment)
        {
#if defined(TR_PLATFORM_WINDOWS)
            Trinity::Expected<Trinity::Process::Environment, std::string> l_Found = FindMsvcEnvironment(cancel);
            if (!l_Found)
            {
                return l_Found;
            }

            s_Environment = std::move(*l_Found);
#else
            static_cast<void>(cancel);
            s_Environment = Trinity::Process::GetEnvironment();
#endif
            Trinity::Process::SetVariable(*s_Environment, "VSLANG", "1033");
        }

        return *s_Environment;
    }

    // CMake's major and minor version, from the first line of cmake --version
    std::optional<std::array<int, 2>> ReadCMakeVersion(const std::filesystem::path& cmake, const Trinity::Process::Environment& environment)
    {
        std::optional<std::array<int, 2>> l_Version;
        Trinity::ProcessOptions l_Options;
        l_Options.Environment = environment;
        l_Options.OnLine = [&l_Version](std::string_view line)
        {
            static const std::regex c_Version(R"(^cmake version (\d+)\.(\d+).*$)");
            std::match_results<std::string_view::const_iterator> l_Match;
            if (!l_Version && std::regex_match(line.begin(), line.end(), l_Match, c_Version))
            {
                l_Version = std::array<int, 2>{ std::stoi(l_Match[1].str()), std::stoi(l_Match[2].str()) };
            }
        };

        const std::vector<std::string> l_Arguments{ "--version" };
        if (!Trinity::Process::Run(cmake, l_Arguments, l_Options))
        {
            return std::nullopt;
        }

        return l_Version;
    }

    // A value CMakeCache.txt holds, by its name and type, or nothing
    std::optional<std::string> ReadCacheValue(const std::filesystem::path& cache, std::string_view key)
    {
        std::ifstream l_File(cache);
        std::string l_Line;
        while (std::getline(l_File, l_Line))
        {
            if (l_Line.starts_with(key) && l_Line.size() > key.size() && l_Line[key.size()] == '=')
            {
                while (!l_Line.empty() && l_Line.back() == '\r')
                {
                    l_Line.pop_back();
                }

                return l_Line.substr(key.size() + 1);
            }
        }

        return std::nullopt;
    }

    // A tree configured with another generator is made again, since CMake will not change one in place. A tree for another Forge's engine, or one whose configuring failed, is configured again
    bool NeedsConfigure(const std::filesystem::path& buildDirectory, const std::filesystem::path& engineDirectory, std::string_view generator)
    {
        std::error_code l_Error;
        const std::filesystem::path l_Cache = buildDirectory / "CMakeCache.txt";
        if (!std::filesystem::exists(l_Cache, l_Error))
        {
            return true;
        }

        if (ReadCacheValue(l_Cache, "CMAKE_GENERATOR:INTERNAL") != std::optional<std::string>(generator))
        {
            std::filesystem::remove_all(buildDirectory, l_Error);

            return true;
        }

        const std::optional<std::string> l_Engine = ReadCacheValue(l_Cache, "Trinity_DIR:PATH");
        const bool l_Generated = std::filesystem::exists(buildDirectory / (generator == "Ninja" ? "build.ninja" : "Makefile"), l_Error);

        return !l_Engine || FromUtf8(*l_Engine).lexically_normal() != engineDirectory.lexically_normal() || !l_Generated;
    }

    // The tools, then configuring when the tree needs it, then the build. False at the first step that fails, which has said why
    bool BuildModule(const ModuleBuild::Request& request, const std::atomic<bool>& cancel, OutputReader& output)
    {
        const Trinity::Expected<Trinity::Process::Environment, std::string> l_Environment = GetBuildEnvironment(&cancel);
        if (!l_Environment)
        {
            TR_ERROR("Build: {}", l_Environment.GetError());

            return false;
        }

        const std::filesystem::path l_CMake = Trinity::Process::FindExecutable("cmake", &*l_Environment);
        const std::optional<std::array<int, 2>> l_Version = l_CMake.empty() ? std::nullopt : ReadCMakeVersion(l_CMake, *l_Environment);
        if (!l_Version || *l_Version < c_MinimumCMake)
        {
            TR_ERROR("Build: CMake {}.{} or newer must be on the PATH to build C++ modules, and {}", c_MinimumCMake[0], c_MinimumCMake[1], l_CMake.empty() ? std::string("none was found") : l_Version ? std::format("{} is {}.{}", ToUtf8(l_CMake), (*l_Version)[0], (*l_Version)[1]) : std::format("{} did not say its version", ToUtf8(l_CMake)));

            return false;
        }

        // Ninja comes with Visual Studio. Elsewhere Make does when Ninja is not there
        const std::filesystem::path l_Ninja = Trinity::Process::FindExecutable("ninja", &*l_Environment);
#if defined(TR_PLATFORM_WINDOWS)
        if (l_Ninja.empty())
        {
            TR_ERROR("Build: Ninja was not found on the PATH Visual Studio's environment gives, which its C++ CMake tools put there. Install them, or put ninja.exe on the PATH");

            return false;
        }
#endif
        const std::string l_Generator = l_Ninja.empty() ? "Unix Makefiles" : "Ninja";

        const std::filesystem::path l_Engine = Trinity::Process::GetExecutableDirectory();
        std::error_code l_Error;
        if (!std::filesystem::exists(l_Engine / "TrinityConfig.cmake", l_Error))
        {
            TR_ERROR("Build: {} has no TrinityConfig.cmake beside the engine, so nothing can be built against it. Configure Trinity again, which writes it", ToUtf8(l_Engine));

            return false;
        }

        Trinity::ProcessOptions l_Options;
        l_Options.Environment = *l_Environment;
        l_Options.Cancel = &cancel;
        l_Options.OnLine = [&output](std::string_view line) { output.Read(line); };

        const auto a_Run = [&](const std::vector<std::string>& arguments)
        {
            const Trinity::Expected<int, std::string> l_ExitCode = Trinity::Process::Run(l_CMake, arguments, l_Options);
            output.Finish();
            if (!l_ExitCode)
            {
                TR_ERROR("Build: {}", l_ExitCode.GetError());
            }

            return l_ExitCode && *l_ExitCode == 0 && !cancel.load(std::memory_order_relaxed);
        };

        if (NeedsConfigure(request.BuildDirectory, l_Engine, l_Generator))
        {
            TR_INFO("Build: configuring {} for {} with {} {}.{}", request.Name, ModuleBuild::GetConfiguration(), ToUtf8(l_CMake), (*l_Version)[0], (*l_Version)[1]);
            std::vector<std::string> l_Arguments{ "-S", ToUtf8(request.SourceDirectory), "-B", ToUtf8(request.BuildDirectory), "-G", l_Generator, std::format("-DCMAKE_BUILD_TYPE={}", ModuleBuild::GetConfiguration()), std::format("-DTrinity_DIR:PATH={}", ToGenericUtf8(l_Engine)), std::format("-DCMAKE_TOOLCHAIN_FILE={}", ToGenericUtf8(l_Engine / "TrinityToolchain.cmake")), "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON" };
            if (!l_Ninja.empty())
            {
                l_Arguments.push_back(std::format("-DCMAKE_MAKE_PROGRAM={}", ToGenericUtf8(l_Ninja)));
            }

            if (!a_Run(l_Arguments))
            {
                return false;
            }
        }

        // Ninja goes on past a failing file, so one build shows every error
        std::vector<std::string> l_Arguments{ "--build", ToUtf8(request.BuildDirectory) };
        if (!l_Ninja.empty())
        {
            l_Arguments.insert(l_Arguments.end(), { "--", "-k", "0" });
        }

        TR_INFO("Build: building {} for {}", request.Name, ModuleBuild::GetConfiguration());

        return a_Run(l_Arguments);
    }
}

// The job ends with the build it runs, so it cannot outlive this
ModuleBuild::~ModuleBuild()
{
    Cancel();
}

void ModuleBuild::Start(Request request)
{
    TR_ASSERT(!m_Running, "A module build is already running");

    m_Running = std::make_unique<Running>();
    m_Running->Work = std::move(request);

    Running* l_Running = m_Running.get();
    Trinity::JobSystem::Submit([l_Running] { Run(*l_Running); }, &m_Jobs);
}

std::optional<ModuleBuild::Result> ModuleBuild::Update()
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

void ModuleBuild::Cancel()
{
    if (!m_Running)
    {
        return;
    }

    m_Running->Cancel.store(true, std::memory_order_relaxed);
    Trinity::JobSystem::Wait(m_Jobs);
    m_Running.reset();
}

std::string_view ModuleBuild::GetConfiguration()
{
#if defined(TR_DEBUG)
    return "Debug";
#elif defined(TR_RELEASE)
    return "Release";
#else
    return "Distribution";
#endif
}

bool ModuleBuild::IsSupported()
{
#if defined(TR_ENGINE_SHARED)
    return true;
#else
    return false;
#endif
}

std::filesystem::path ModuleBuild::GetBuildDirectory(const std::filesystem::path& projectBuildDirectory)
{
    return projectBuildDirectory / FromUtf8(GetConfiguration());
}

// Where the module's CMake puts it, with the name a loadable module has on each platform
std::filesystem::path ModuleBuild::GetLibraryPath(const std::filesystem::path& buildDirectory, std::string_view name)
{
#if defined(TR_PLATFORM_WINDOWS)
    constexpr std::string_view c_Extension = ".dll";
#else
    constexpr std::string_view c_Extension = ".so";
#endif

    return buildDirectory / "Modules" / FromUtf8(std::format("{}{}", name, c_Extension));
}

void ModuleBuild::Run(Running& running)
{
    const Clock::time_point l_Start = Clock::now();
    const Request& l_Request = running.Work;
    OutputReader l_Output(l_Request.ProjectDirectory, l_Request.SourceDirectory, l_Request.BuildDirectory);

    std::error_code l_Error;
    std::filesystem::create_directories(l_Request.BuildDirectory, l_Error);

    const bool l_Built = BuildModule(l_Request, running.Cancel, l_Output);
    Result& l_Result = running.Outcome;
    l_Result.Cancelled = running.Cancel.load(std::memory_order_relaxed);
    l_Result.Library = GetLibraryPath(l_Request.BuildDirectory, l_Request.Name);
    l_Result.Succeeded = l_Built && !l_Result.Cancelled && std::filesystem::exists(l_Result.Library, l_Error);
    l_Result.Errors = l_Output.GetErrors();
    l_Result.Warnings = l_Output.GetWarnings();
    l_Result.Seconds = std::chrono::duration<double>(Clock::now() - l_Start).count();

    if (l_Result.Cancelled)
    {
        TR_INFO("Build: {} was cancelled", l_Request.Name);
    }
    else if (l_Result.Succeeded)
    {
        TR_INFO("Build: {} built in {:.1f} s{}", l_Request.Name, l_Result.Seconds, l_Result.Warnings > 0 ? std::format(", with {} warning(s)", l_Result.Warnings) : std::string());
    }
    else
    {
        TR_ERROR("Build: {} failed after {:.1f} s, with {} error(s) and {} warning(s)", l_Request.Name, l_Result.Seconds, l_Result.Errors, l_Result.Warnings);
    }

    running.Done.store(true, std::memory_order_release);
}
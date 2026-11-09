#include "Trinity/Tools/BuildTools.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <iterator>
#include <regex>
#include <system_error>
#include <tuple>
#include <utility>

namespace Trinity::Tools
{
    namespace
    {
        std::filesystem::path FromUtf8(std::string_view text)
        {
            return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
        }

        std::string ToUtf8(const std::filesystem::path& path)
        {
            const std::u8string l_Text = path.u8string();

            return std::string(reinterpret_cast<const char*>(l_Text.data()), l_Text.size());
        }

        // What a program writes, its output and its errors a line at a time, and its exit code, or why it could not start
        Expected<std::pair<int, std::vector<std::string>>, std::string> ReadLines(const std::filesystem::path& program, std::span<const std::string> arguments, const Process::Environment& environment, const std::atomic<bool>* cancel)
        {
            std::vector<std::string> l_Lines;
            ProcessOptions l_Options;
            l_Options.Environment = environment;
            l_Options.Cancel = cancel;
            l_Options.OnLine = [&l_Lines](std::string_view line) { l_Lines.emplace_back(line); };
            const Expected<int, std::string> l_ExitCode = Process::Run(program, arguments, l_Options);
            if (!l_ExitCode)
            {
                return Unexpected{ l_ExitCode.GetError() };
            }

            return std::pair(*l_ExitCode, std::move(l_Lines));
        }

        // The first group of the first line the pattern is found in
        std::optional<std::string> FindFirst(std::span<const std::string> lines, const std::regex& pattern)
        {
            for (const std::string& it_Line : lines)
            {
                std::smatch l_Match;
                if (std::regex_search(it_Line, l_Match, pattern))
                {
                    return l_Match[1].str();
                }
            }

            return std::nullopt;
        }

        // The digits a version begins with, as 19 of 19.44.35211, or empty for none
        std::string_view GetMajor(std::string_view version)
        {
            const std::size_t l_End = static_cast<std::size_t>(std::ranges::find_if(version, [](char character) { return character < '0' || character > '9'; }) - version.begin());

            return version.substr(0, l_End);
        }

        // An SDK's version as dotnet groups them: 10.0.114 is major 10, minor 0, feature band 1 and patch 14, and a preview, after a hyphen, comes before its release
        struct SdkVersion
        {
            std::string Text;
            int Major = 0;
            int Minor = 0;
            int Band = 0;
            int Patch = 0;
            bool Preview = false;
        };

        std::optional<SdkVersion> ParseSdkVersion(std::string_view text)
        {
            static const std::regex c_Pattern(R"(^(\d{1,6})\.(\d{1,6})\.(\d{1,6})(-.*)?$)");
            const std::string l_Text(text);
            std::smatch l_Match;
            if (!std::regex_match(l_Text, l_Match, c_Pattern))
            {
                return std::nullopt;
            }

            const int l_Third = std::stoi(l_Match[3].str());

            return SdkVersion{ l_Text, std::stoi(l_Match[1].str()), std::stoi(l_Match[2].str()), l_Third / 100, l_Third % 100, l_Match[4].matched };
        }

#if defined(TR_PLATFORM_WINDOWS)
        // The first line of a small text file, without its line end, or empty
        std::string ReadFirstLine(const std::filesystem::path& file)
        {
            std::ifstream l_File(file);
            std::string l_Line;
            std::getline(l_File, l_Line);
            while (!l_Line.empty() && std::isspace(static_cast<unsigned char>(l_Line.back())))
            {
                l_Line.pop_back();
            }

            return l_Line;
        }
#endif
    }

    // On the PATH, then where DOTNET_ROOT points, then where the installers put it
    std::filesystem::path FindDotNet(const Process::Environment& environment)
    {
        if (std::filesystem::path l_Found = Process::FindExecutable("dotnet", &environment); !l_Found.empty())
        {
            return l_Found;
        }

#if defined(TR_PLATFORM_WINDOWS)
        constexpr std::string_view c_Executable = "dotnet.exe";
        const std::vector<std::filesystem::path> l_Folders{ FromUtf8(Process::FindVariable(environment, "DOTNET_ROOT").value_or("")), FromUtf8(Process::FindVariable(environment, "ProgramFiles").value_or("C:\\Program Files")) / "dotnet" };
#else
        constexpr std::string_view c_Executable = "dotnet";
        const std::vector<std::filesystem::path> l_Folders{ FromUtf8(Process::FindVariable(environment, "DOTNET_ROOT").value_or("")), "/usr/local/share/dotnet", "/usr/share/dotnet", "/usr/lib/dotnet" };
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

    void SetDotNetVariables(Process::Environment& environment)
    {
        Process::SetVariable(environment, "DOTNET_NOLOGO", "1");
        Process::SetVariable(environment, "DOTNET_SKIP_FIRST_TIME_EXPERIENCE", "1");
        Process::SetVariable(environment, "DOTNET_CLI_UI_LANGUAGE", "en");
    }

    // Each line is a version and the folder it is in, as 10.0.114 [/usr/lib/dotnet/sdk]
    Expected<std::vector<std::string>, std::string> ListDotNetSdks(const std::filesystem::path& dotnet, const Process::Environment& environment, const std::atomic<bool>* cancel)
    {
        Process::Environment l_Environment = environment;
        SetDotNetVariables(l_Environment);
        const std::vector<std::string> l_Arguments{ "--list-sdks" };
        const Expected<std::pair<int, std::vector<std::string>>, std::string> l_Ran = ReadLines(dotnet, l_Arguments, l_Environment, cancel);
        if (!l_Ran)
        {
            return Unexpected{ std::format("{} could not be run: {}", ToUtf8(dotnet), l_Ran.GetError()) };
        }

        if (l_Ran->first != 0)
        {
            return Unexpected{ std::format("{} --list-sdks ended with exit code {}", ToUtf8(dotnet), l_Ran->first) };
        }

        static const std::regex c_Line(R"(^(\S+) \[)");
        std::vector<std::string> l_Sdks;
        for (const std::string& it_Line : l_Ran->second)
        {
            std::smatch l_Match;
            if (std::regex_search(it_Line, l_Match, c_Line))
            {
                l_Sdks.push_back(l_Match[1].str());
            }
        }

        return l_Sdks;
    }

    // As dotnet reads global.json: the policy first keeps the SDKs within the version's feature band, minor or major, then those that roll forward the least pick the lowest band above it that has one, and the highest of those left is taken
    std::optional<std::string> PickDotNetSdk(std::span<const std::string> installed, std::string_view version, std::string_view rollForward)
    {
        const std::optional<SdkVersion> l_Wanted = ParseSdkVersion(version);
        if (!l_Wanted)
        {
            return std::nullopt;
        }

        const std::string_view l_Policy = rollForward.empty() ? std::string_view("latestPatch") : rollForward;
        if (l_Policy == "disable")
        {
            const auto a_Exact = std::ranges::find(installed, version);

            return a_Exact != installed.end() ? std::optional<std::string>(*a_Exact) : std::nullopt;
        }

        const bool l_Band = l_Policy == "patch" || l_Policy == "latestPatch";
        const bool l_Minor = l_Band || l_Policy == "feature" || l_Policy == "latestFeature";
        const bool l_Major = l_Minor || l_Policy == "minor" || l_Policy == "latestMinor";
        const bool l_Any = l_Major || l_Policy == "major" || l_Policy == "latestMajor";
        if (!l_Any)
        {
            return std::nullopt;
        }

        std::vector<SdkVersion> l_Allowed;
        for (const std::string& it_Installed : installed)
        {
            const std::optional<SdkVersion> l_Sdk = ParseSdkVersion(it_Installed);
            if (!l_Sdk || (l_Sdk->Preview && !l_Wanted->Preview) || CompareVersions(l_Sdk->Text, l_Wanted->Text) < 0)
            {
                continue;
            }

            if ((l_Major && l_Sdk->Major != l_Wanted->Major) || (l_Minor && l_Sdk->Minor != l_Wanted->Minor) || (l_Band && l_Sdk->Band != l_Wanted->Band))
            {
                continue;
            }

            l_Allowed.push_back(*l_Sdk);
        }

        if (l_Allowed.empty())
        {
            return std::nullopt;
        }

        if (l_Policy == "patch")
        {
            if (const auto a_Exact = std::ranges::find(l_Allowed, l_Wanted->Text, &SdkVersion::Text); a_Exact != l_Allowed.end())
            {
                return a_Exact->Text;
            }
        }

        // The policies that roll forward the least go no further than the first band above that has an SDK
        if (l_Policy == "feature" || l_Policy == "minor" || l_Policy == "major")
        {
            const auto l_Group = [](const SdkVersion& sdk) { return std::tuple(sdk.Major, sdk.Minor, sdk.Band); };
            const auto l_Lowest = l_Group(*std::ranges::min_element(l_Allowed, {}, l_Group));
            std::erase_if(l_Allowed, [&l_Group, &l_Lowest](const SdkVersion& sdk) { return l_Group(sdk) != l_Lowest; });
        }

        return std::ranges::max_element(l_Allowed, [](const SdkVersion& left, const SdkVersion& right) { return CompareVersions(left.Text, right.Text) < 0; })->Text;
    }

    std::string DescribeDotNetSdks(std::string_view version, std::string_view rollForward)
    {
        const std::optional<SdkVersion> l_Wanted = ParseSdkVersion(version);
        if (!l_Wanted)
        {
            return std::string(version);
        }

        const std::string_view l_Policy = rollForward.empty() ? std::string_view("latestPatch") : rollForward;
        if (l_Policy == "disable")
        {
            return std::format("exactly {}", version);
        }

        if (l_Policy == "patch" || l_Policy == "latestPatch")
        {
            return std::format("{} or a newer {}.{}.{}xx SDK", version, l_Wanted->Major, l_Wanted->Minor, l_Wanted->Band);
        }

        if (l_Policy == "feature" || l_Policy == "latestFeature")
        {
            return std::format("{} or a newer {}.{} SDK", version, l_Wanted->Major, l_Wanted->Minor);
        }

        if (l_Policy == "minor" || l_Policy == "latestMinor")
        {
            return std::format("{} or a newer {}.x SDK", version, l_Wanted->Major);
        }

        return std::format("{} or newer", version);
    }

    std::optional<std::string> ReadCMakeVersion(const std::filesystem::path& cmake, const Process::Environment& environment, const std::atomic<bool>* cancel)
    {
        const std::vector<std::string> l_Arguments{ "--version" };
        const Expected<std::pair<int, std::vector<std::string>>, std::string> l_Ran = ReadLines(cmake, l_Arguments, environment, cancel);
        static const std::regex c_Version(R"(^cmake version (\d+(?:\.\d+)+\S*))");

        return l_Ran ? FindFirst(l_Ran->second, c_Version) : std::nullopt;
    }

    std::optional<std::string> ReadNinjaVersion(const std::filesystem::path& ninja, const Process::Environment& environment, const std::atomic<bool>* cancel)
    {
        const std::vector<std::string> l_Arguments{ "--version" };
        const Expected<std::pair<int, std::vector<std::string>>, std::string> l_Ran = ReadLines(ninja, l_Arguments, environment, cancel);
        static const std::regex c_Version(R"(^(\d+(?:\.\d+)+\S*)$)");

        return l_Ran ? FindFirst(l_Ran->second, c_Version) : std::nullopt;
    }

    // Kinds by CMake's names for them: cl says Compiler Version, Apple's Clang and every other Clang say clang version, and GCC names the Free Software Foundation, its version ending its first line
    Expected<Compiler, std::string> ReadCompiler(const std::filesystem::path& compiler, const Process::Environment& environment, const std::atomic<bool>* cancel)
    {
        std::string l_Stem = ToUtf8(compiler.stem());
        std::ranges::transform(l_Stem, l_Stem.begin(), [](char character) { return static_cast<char>(std::tolower(static_cast<unsigned char>(character))); });
        const bool l_Cl = l_Stem == "cl";
        const std::vector<std::string> l_Arguments = l_Cl ? std::vector<std::string>() : std::vector<std::string>{ "--version" };
        const Expected<std::pair<int, std::vector<std::string>>, std::string> l_Ran = ReadLines(compiler, l_Arguments, environment, cancel);
        if (!l_Ran)
        {
            return Unexpected{ std::format("{} could not be run: {}", ToUtf8(compiler), l_Ran.GetError()) };
        }

        const std::vector<std::string>& l_Lines = l_Ran->second;
        static const std::regex c_Msvc(R"(Compiler Version (\d+(?:\.\d+)+))");
        static const std::regex c_AppleClang(R"(Apple clang version (\d+(?:\.\d+)+))");
        static const std::regex c_Clang(R"(clang version (\d+(?:\.\d+)+))");
        static const std::regex c_GnuVersion(R"((\d+(?:\.\d+)+)\s*$)");
        Compiler l_Compiler;
        l_Compiler.Path = compiler;
        if (std::optional<std::string> l_Version = FindFirst(l_Lines, c_Msvc))
        {
            l_Compiler.Id = "MSVC";
            l_Compiler.Version = std::move(*l_Version);
        }
        else if (std::optional<std::string> l_Apple = FindFirst(l_Lines, c_AppleClang))
        {
            l_Compiler.Id = "AppleClang";
            l_Compiler.Version = std::move(*l_Apple);
        }
        else if (std::optional<std::string> l_Clang = FindFirst(l_Lines, c_Clang))
        {
            l_Compiler.Id = "Clang";
            l_Compiler.Version = std::move(*l_Clang);
        }
        else if (std::ranges::any_of(l_Lines, [](const std::string& line) { return line.find("Free Software Foundation") != std::string::npos; }) && !l_Lines.empty())
        {
            std::smatch l_Match;
            if (std::regex_search(l_Lines.front(), l_Match, c_GnuVersion))
            {
                l_Compiler.Id = "GNU";
                l_Compiler.Version = l_Match[1].str();
            }
        }

        if (l_Compiler.Id.empty())
        {
            return Unexpected{ std::format("{} did not say which compiler it is", ToUtf8(compiler)) };
        }

        return l_Compiler;
    }

    bool IsSameKindAndMajor(std::string_view leftId, std::string_view leftVersion, std::string_view rightId, std::string_view rightVersion)
    {
        return leftId == rightId && !GetMajor(leftVersion).empty() && GetMajor(leftVersion) == GetMajor(rightVersion);
    }

    // The line CMake/Package/TrinityToolchain.cmake.in writes for it, set(CMAKE_CXX_COMPILER [==[<path>]==])
    std::filesystem::path ReadToolchainCompiler(const std::filesystem::path& toolchain)
    {
        std::ifstream l_File(toolchain, std::ios::binary);
        const std::string l_Text{ std::istreambuf_iterator<char>(l_File), std::istreambuf_iterator<char>() };
        static const std::regex c_Compiler(R"(set\(CMAKE_CXX_COMPILER \[==\[([^\]]*)\]==\]\))");
        std::smatch l_Match;

        return std::regex_search(l_Text, l_Match, c_Compiler) ? FromUtf8(l_Match[1].str()) : std::filesystem::path();
    }

#if defined(TR_PLATFORM_WINDOWS)
    // The preferred compiler is <Visual Studio>/VC/Tools/MSVC/<toolset>/bin/Hostx64/x64/cl.exe, and vswhere, which every Visual Studio installer puts in the same place, names the newest with the C++ tools, whose default toolset Microsoft.VCToolsVersion.default.txt names
    Expected<Compiler, std::string> FindVisualStudio(const std::filesystem::path& preferredCompiler, const std::atomic<bool>* cancel)
    {
        Compiler l_Studio;
        l_Studio.Id = "MSVC";
        std::error_code l_Error;
        for (std::filesystem::path it_Folder = preferredCompiler.parent_path(); !preferredCompiler.empty() && it_Folder.has_relative_path(); it_Folder = it_Folder.parent_path())
        {
            const std::filesystem::path l_Tools = it_Folder.parent_path();
            if (l_Tools.filename() == "MSVC" && l_Tools.parent_path().filename() == "Tools" && l_Tools.parent_path().parent_path().filename() == "VC")
            {
                const std::filesystem::path l_Vc = l_Tools.parent_path().parent_path();
                const std::filesystem::path l_VcVars = l_Vc / "Auxiliary" / "Build" / "vcvars64.bat";
                if (std::filesystem::exists(l_VcVars, l_Error))
                {
                    l_Studio.Path = preferredCompiler;
                    l_Studio.VisualStudio = l_Vc.parent_path();
                    l_Studio.VcVars = l_VcVars;
                    l_Studio.Toolset = ToUtf8(it_Folder.filename());

                    return l_Studio;
                }

                break;
            }
        }

        const Process::Environment l_Environment = Process::GetEnvironment();
        const std::string l_ProgramFiles = Process::FindVariable(l_Environment, "ProgramFiles(x86)").value_or("C:\\Program Files (x86)");
        const std::filesystem::path l_VsWhere = FromUtf8(l_ProgramFiles) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe";
        const std::vector<std::string> l_Arguments{ "-latest", "-prerelease", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath" };
        std::string l_Installation;
        if (std::filesystem::exists(l_VsWhere, l_Error))
        {
            if (const Expected<std::pair<int, std::vector<std::string>>, std::string> l_Ran = ReadLines(l_VsWhere, l_Arguments, l_Environment, cancel))
            {
                const auto a_Line = std::ranges::find_if(l_Ran->second, [](const std::string& line) { return !line.empty(); });
                l_Installation = a_Line != l_Ran->second.end() ? *a_Line : std::string();
            }
        }

        if (l_Installation.empty())
        {
            return Unexpected{ std::string("Visual Studio with its C++ tools was not found. Install the Desktop development with C++ workload") };
        }

        l_Studio.VisualStudio = FromUtf8(l_Installation);
        const std::filesystem::path l_Build = l_Studio.VisualStudio / "VC" / "Auxiliary" / "Build";
        l_Studio.VcVars = l_Build / "vcvars64.bat";
        l_Studio.Toolset = ReadFirstLine(l_Build / "Microsoft.VCToolsVersion.default.txt");
        if (!l_Studio.Toolset.empty())
        {
            l_Studio.Path = l_Studio.VisualStudio / "VC" / "Tools" / "MSVC" / FromUtf8(l_Studio.Toolset) / "bin" / "Hostx64" / "x64" / "cl.exe";
        }

        return l_Studio;
    }

    // vcvars64.bat runs in a batch file that then lists every variable, at the preferred compiler's toolset when it is that one's Visual Studio
    Expected<BuildEnvironment, std::string> FindBuildEnvironment(const std::filesystem::path& preferredCompiler, const std::atomic<bool>* cancel)
    {
        const Expected<Compiler, std::string> l_Studio = FindVisualStudio(preferredCompiler, cancel);
        if (!l_Studio)
        {
            return Unexpected{ l_Studio.GetError() };
        }

        const std::string l_Toolset = !preferredCompiler.empty() && l_Studio->Path == preferredCompiler ? l_Studio->Toolset : std::string();
        std::error_code l_Error;
        const std::filesystem::path l_Batch = std::filesystem::temp_directory_path(l_Error) / "TrinityModuleEnvironment.bat";
        {
            std::ofstream l_File(l_Batch, std::ios::binary | std::ios::trunc);
            l_File << "@call \"" << ToUtf8(l_Studio->VcVars) << "\"" << (l_Toolset.empty() ? std::string() : " -vcvars_ver=" + l_Toolset) << " >nul\r\n@if errorlevel 1 exit /b 1\r\n@set\r\n";
            if (!l_File)
            {
                return Unexpected{ std::format("{} could not be written", ToUtf8(l_Batch)) };
            }
        }

        BuildEnvironment l_Result;
        l_Result.VcVars = l_Studio->VcVars;
        l_Result.Toolset = l_Toolset;
        ProcessOptions l_Options;
        l_Options.Cancel = cancel;
        l_Options.OnLine = [&l_Result](std::string_view line)
        {
            const std::size_t l_Equals = line.find('=');
            if (l_Equals != std::string_view::npos && l_Equals != 0)
            {
                l_Result.Variables.emplace_back(line.substr(0, l_Equals), line.substr(l_Equals + 1));
            }
        };

        const Process::Environment l_Environment = Process::GetEnvironment();
        const std::filesystem::path l_Shell = FromUtf8(Process::FindVariable(l_Environment, "ComSpec").value_or("C:\\Windows\\System32\\cmd.exe"));
        const std::vector<std::string> l_Arguments{ "/d", "/c", ToUtf8(l_Batch) };
        const Expected<int, std::string> l_Ran = Process::Run(l_Shell, l_Arguments, l_Options);
        std::filesystem::remove(l_Batch, l_Error);
        if (!l_Ran || *l_Ran != 0 || !Process::FindVariable(l_Result.Variables, "INCLUDE"))
        {
            return Unexpected{ std::format("{} did not set up the compiler{}", ToUtf8(l_Studio->VcVars), l_Ran ? std::string() : ": " + l_Ran.GetError()) };
        }

        Process::SetVariable(l_Result.Variables, "VSLANG", "1033");

        return l_Result;
    }

    // vulkan-1.dll is found in System32 as the engine loads it, where every Vulkan driver's installer puts it
    std::filesystem::path FindVulkanLoader()
    {
        const Process::Environment l_Environment = Process::GetEnvironment();
        const std::filesystem::path l_Loader = FromUtf8(Process::FindVariable(l_Environment, "SystemRoot").value_or("C:\\Windows")) / "System32" / "vulkan-1.dll";
        std::error_code l_Error;

        return std::filesystem::is_regular_file(l_Loader, l_Error) ? l_Loader : std::filesystem::path();
    }
#else
    Expected<Compiler, std::string> FindVisualStudio(const std::filesystem::path&, const std::atomic<bool>*)
    {
        return Unexpected{ std::string("Visual Studio is looked for on Windows alone") };
    }

    Expected<BuildEnvironment, std::string> FindBuildEnvironment(const std::filesystem::path&, const std::atomic<bool>*)
    {
        BuildEnvironment l_Result;
        l_Result.Variables = Process::GetEnvironment();
        Process::SetVariable(l_Result.Variables, "VSLANG", "1033");

        return l_Result;
    }

    std::filesystem::path FindVulkanLoader()
    {
        return {};
    }
#endif

    // As CMake finds one: the toolchain's while it is there, then cl on Windows, and CXX, then c++, elsewhere
    Expected<Compiler, std::string> FindCompiler(const std::filesystem::path& preferredCompiler, const Process::Environment& environment, const std::atomic<bool>* cancel)
    {
        std::error_code l_Error;
        if (!preferredCompiler.empty() && std::filesystem::is_regular_file(preferredCompiler, l_Error))
        {
            return ReadCompiler(preferredCompiler, environment, cancel);
        }

#if defined(TR_PLATFORM_WINDOWS)
        const std::filesystem::path l_Found = Process::FindExecutable("cl", &environment);
        if (l_Found.empty())
        {
            return Unexpected{ std::string("cl.exe is not on the PATH Visual Studio's environment gives") };
        }
#else
        const std::optional<std::string> l_Variable = Process::FindVariable(environment, "CXX");
        const std::filesystem::path l_Found = Process::FindExecutable(l_Variable && !l_Variable->empty() ? std::string_view(*l_Variable) : std::string_view("c++"), &environment);
        if (l_Found.empty())
        {
            return Unexpected{ l_Variable && !l_Variable->empty() ? std::format("CXX names {}, which was not found", *l_Variable) : std::string("no C++ compiler, c++, is on the PATH") };
        }
#endif

        return ReadCompiler(l_Found, environment, cancel);
    }
}
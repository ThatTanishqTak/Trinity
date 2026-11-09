#include "HubRequirements.hpp"

#include "HubText.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <string_view>
#include <system_error>
#include <utility>

namespace
{
    // As Forge's are looked for, so the toolchain read is the one beside the Forge a project opens in
    constexpr std::array<std::string_view, 3> c_Configurations{ "Release", "Debug", "Distribution" };

#if defined(TR_PLATFORM_WINDOWS)
    constexpr bool c_Windows = true;
#else
    constexpr bool c_Windows = false;
#endif

    // The digits before the first point, as 19 of 19.44.35211
    std::string_view GetMajor(std::string_view version)
    {
        return version.substr(0, version.find('.'));
    }

    // Major and minor, as 10.0 of 10.0.100
    std::string GetMajorMinor(std::string_view version)
    {
        const std::size_t l_First = version.find('.');
        const std::size_t l_Second = l_First == std::string_view::npos ? std::string_view::npos : version.find('.', l_First + 1);

        return std::string(version.substr(0, l_Second));
    }

    std::string Join(const std::vector<std::string>& parts)
    {
        std::string l_Text;
        for (std::size_t it_Index = 0; it_Index < parts.size(); ++it_Index)
        {
            l_Text += std::format("{}{}", it_Index == 0 ? "" : it_Index + 1 == parts.size() ? " and " : ", ", parts[it_Index]);
        }

        return l_Text;
    }

    // As people know the compilers CMake names
    std::string_view DescribeCompilerKind(std::string_view id)
    {
        if (id == "GNU")
        {
            return "GCC";
        }

        return id == "AppleClang" ? std::string_view("Apple Clang") : id;
    }

    // Why a language cannot build, naming each tool it stops at
    std::string DescribeRefusal(std::string_view language, std::string_view version, const std::vector<std::string>& missing)
    {
        return std::format("Trinity {} builds {} with {}, which this machine is missing or has too old", version, language, Join(missing));
    }
}

std::size_t HubRequirements::Report::CountMissing() const
{
    return static_cast<std::size_t>(std::ranges::count_if(Rows, [](const Row& row) { return !row.Found; }));
}

const std::string& HubRequirements::Report::GetRefusal(Trinity::ScriptLanguage language) const
{
    return language == Trinity::ScriptLanguage::CSharp ? CSharpRefusal : CppRefusal;
}

// A look still going is let finish the program it is running, and dropped
HubRequirements::~HubRequirements()
{
    if (m_Looking)
    {
        m_Looking->Stop.store(true, std::memory_order_relaxed);
        Trinity::JobSystem::Wait(m_Jobs);
    }
}

void HubRequirements::Check(const HubInstalls& installs)
{
    if (m_Looking)
    {
        m_LookAgain = true;

        return;
    }

    m_Compilers.clear();
    m_Looking = std::make_unique<Looking>();
    for (const HubInstall& it_Install : installs.GetInstalls())
    {
        if (!HubInstalls::IsUsable(it_Install))
        {
            continue;
        }

        const std::filesystem::path l_Compiler = FindPreferredCompiler(it_Install);
        m_Compilers.insert_or_assign(it_Install.Directory, l_Compiler);
        if (std::ranges::find(m_Looking->Compilers, l_Compiler) == m_Looking->Compilers.end())
        {
            m_Looking->Compilers.push_back(l_Compiler);
        }
    }

    // GameInput is asked of on the main thread, as the engine reads pads there
    m_Looking->Found.GamepadRuntime = Trinity::Input::HasGamepadRuntime();
    Looking* l_Looking = m_Looking.get();
    Trinity::JobSystem::Submit([l_Looking] { Look(*l_Looking); }, &m_Jobs);
}

// A version listed since the last look is read once, and looked at again only for a compiler no version named before
void HubRequirements::Update(const HubInstalls& installs)
{
    if (m_Looking && m_Looking->Done.load(std::memory_order_acquire))
    {
        Trinity::JobSystem::Wait(m_Jobs);
        m_Machine = std::move(m_Looking->Found);
        m_Looking.reset();
        if (std::exchange(m_LookAgain, false))
        {
            Check(installs);

            return;
        }
    }

    if (m_Looking || !m_Machine)
    {
        return;
    }

    bool l_LookAgain = false;
    for (const HubInstall& it_Install : installs.GetInstalls())
    {
        if (HubInstalls::IsUsable(it_Install) && !m_Compilers.contains(it_Install.Directory))
        {
            const std::filesystem::path l_Compiler = FindPreferredCompiler(it_Install);
            m_Compilers.emplace(it_Install.Directory, l_Compiler);
            l_LookAgain |= !m_Machine->Cpp.contains(l_Compiler);
        }
    }

    if (l_LookAgain)
    {
        Check(installs);
    }
}

std::optional<HubRequirements::Report> HubRequirements::GetReport(const HubInstall& install) const
{
    const auto a_Compiler = m_Compilers.find(install.Directory);
    if (!m_Machine || a_Compiler == m_Compilers.end() || !m_Machine->Cpp.contains(a_Compiler->second))
    {
        return std::nullopt;
    }

    return Read(*m_Machine, install, a_Compiler->second);
}

// The .NET SDKs once for every version, and the C++ tools once for each compiler, in the environment that compiler's Forge builds in
void HubRequirements::Look(Looking& looking)
{
    Machine& l_Found = looking.Found;
    const Trinity::Process::Environment l_Environment = Trinity::Process::GetEnvironment();
    l_Found.DotNet = Trinity::Tools::FindDotNet(l_Environment);
    if (!l_Found.DotNet.empty())
    {
        Trinity::Expected<std::vector<std::string>, std::string> l_Sdks = Trinity::Tools::ListDotNetSdks(l_Found.DotNet, l_Environment, &looking.Stop);
        if (l_Sdks)
        {
            l_Found.Sdks = std::move(*l_Sdks);
        }
        else
        {
            l_Found.SdksError = l_Sdks.GetError();
        }
    }

    l_Found.VulkanLoader = Trinity::Tools::FindVulkanLoader();
    for (const std::filesystem::path& it_Compiler : looking.Compilers)
    {
        if (looking.Stop.load(std::memory_order_relaxed))
        {
            break;
        }

        CppTools l_Tools;
        const Trinity::Expected<Trinity::Tools::BuildEnvironment, std::string> l_Build = Trinity::Tools::FindBuildEnvironment(it_Compiler, &looking.Stop);
        if (!l_Build)
        {
            l_Tools.EnvironmentError = l_Build.GetError();
            l_Found.Cpp.insert_or_assign(it_Compiler, std::move(l_Tools));

            continue;
        }

        const Trinity::Process::Environment& l_Variables = l_Build->Variables;
        if (Trinity::Expected<Trinity::Tools::Compiler, std::string> l_Compiler = Trinity::Tools::FindCompiler(it_Compiler, l_Variables, &looking.Stop))
        {
            l_Tools.Compiler = std::move(*l_Compiler);
        }
        else
        {
            l_Tools.CompilerError = l_Compiler.GetError();
        }

        l_Tools.CMake = Trinity::Process::FindExecutable("cmake", &l_Variables);
        l_Tools.CMakeVersion = l_Tools.CMake.empty() ? std::nullopt : Trinity::Tools::ReadCMakeVersion(l_Tools.CMake, l_Variables, &looking.Stop);
        l_Tools.Ninja = Trinity::Process::FindExecutable("ninja", &l_Variables);
        l_Tools.NinjaVersion = l_Tools.Ninja.empty() ? std::nullopt : Trinity::Tools::ReadNinjaVersion(l_Tools.Ninja, l_Variables, &looking.Stop);
        l_Tools.Make = c_Windows ? std::filesystem::path() : Trinity::Process::FindExecutable("make", &l_Variables);
        l_Found.Cpp.insert_or_assign(it_Compiler, std::move(l_Tools));
    }

    looking.Done.store(true, std::memory_order_release);
}

std::filesystem::path HubRequirements::FindPreferredCompiler(const HubInstall& install)
{
    std::error_code l_Error;
    for (const std::string_view it_Configuration : c_Configurations)
    {
        if (!install.Manifest.HasConfiguration(it_Configuration))
        {
            continue;
        }

        const std::filesystem::path l_Toolchain = install.Manifest.GetConfigurationDirectory(install.Directory, it_Configuration) / "TrinityToolchain.cmake";
        if (std::filesystem::is_regular_file(l_Toolchain, l_Error))
        {
            return Trinity::Tools::ReadToolchainCompiler(l_Toolchain);
        }
    }

    return {};
}

// C# builds with the .NET SDK alone, though NativeAOT links an exported game with a C++ compiler, and C++ needs the compiler, CMake, and Ninja, or on Linux Make in its place. GameInput and the Vulkan loader stop neither, as Forge runs without them
HubRequirements::Report HubRequirements::Read(const Machine& machine, const HubInstall& install, const std::filesystem::path& compiler)
{
    const Trinity::EngineManifest& l_Manifest = install.Manifest;
    const CppTools& l_Tools = machine.Cpp.at(compiler);
    const std::string_view l_Where = c_Windows ? " on the PATH Visual Studio's environment gives" : " on the PATH";
    Report l_Report;

    if (!l_Manifest.DotNetSdk.empty())
    {
        Row l_Row;
        l_Row.Name = ".NET SDK";
        l_Row.Wanted = Trinity::Tools::DescribeDotNetSdks(l_Manifest.DotNetSdk, l_Manifest.DotNetRollForward);
        l_Row.Purpose = "Builds C# scripts, and compiles a C# game ahead of time as it is exported";
        l_Row.StopsCSharp = true;
        const std::optional<std::string> l_Picked = machine.Sdks ? Trinity::Tools::PickDotNetSdk(*machine.Sdks, l_Manifest.DotNetSdk, l_Manifest.DotNetRollForward) : std::nullopt;
        l_Row.Found = l_Picked.has_value();
        if (machine.DotNet.empty())
        {
            l_Row.Detail = "dotnet was not found on the PATH, where DOTNET_ROOT points or where its installers put it";
        }
        else if (!machine.Sdks)
        {
            l_Row.Detail = machine.SdksError;
        }
        else if (l_Picked)
        {
            l_Row.Detail = std::format("{} at {}", *l_Picked, HubText::ToUtf8(machine.DotNet));
        }
        else
        {
            l_Row.Detail = machine.Sdks->empty() ? std::format("{} has no SDK, only runtimes", HubText::ToUtf8(machine.DotNet)) : std::format("{} has {}, and none is {}", HubText::ToUtf8(machine.DotNet), Join(*machine.Sdks), l_Row.Wanted);
        }

        const std::string l_Band = GetMajorMinor(l_Manifest.DotNetSdk);
        l_Row.Short = std::format("the .NET {} SDK", l_Band);
        l_Row.Remedy = std::format("Install the .NET {} SDK from https://dotnet.microsoft.com/download/dotnet/{}, or point DOTNET_ROOT at one", l_Band, l_Band);
        l_Report.Rows.push_back(std::move(l_Row));
    }
    else
    {
        l_Report.CSharpRefusal = std::format("Trinity {} was built without C#, so its projects are C++", l_Manifest.Version);
    }

    {
        Row l_Row;
        l_Row.Name = "C++ compiler";
        l_Row.Wanted = l_Manifest.CompilerId.empty() ? std::string("Any") : std::format("{} {}, or another {}.x", DescribeCompilerKind(l_Manifest.CompilerId), l_Manifest.CompilerVersion, GetMajor(l_Manifest.CompilerVersion));
        l_Row.Short = l_Manifest.CompilerId.empty() ? std::string("a C++ compiler") : std::format("{} {}", DescribeCompilerKind(l_Manifest.CompilerId), GetMajor(l_Manifest.CompilerVersion));
        l_Row.Purpose = "Builds C++ modules and the players of C++ games, and links a C# game NativeAOT compiles";
        l_Row.StopsCpp = true;
        if (!l_Tools.EnvironmentError.empty())
        {
            l_Row.Detail = l_Tools.EnvironmentError;
        }
        else if (!l_Tools.Compiler)
        {
            l_Row.Detail = l_Tools.CompilerError;
        }
        else
        {
            const Trinity::Tools::Compiler& l_Found = *l_Tools.Compiler;
            const std::string l_Described = std::format("{} {} at {}", DescribeCompilerKind(l_Found.Id), l_Found.Version, HubText::ToUtf8(l_Found.Path));
            l_Row.Found = l_Manifest.CompilerId.empty() || Trinity::Tools::IsSameKindAndMajor(l_Found.Id, l_Found.Version, l_Manifest.CompilerId, l_Manifest.CompilerVersion);
            l_Row.Detail = l_Row.Found ? l_Described : std::format("{}, which is not {} {}, as a module of Trinity {} must be", l_Described, DescribeCompilerKind(l_Manifest.CompilerId), GetMajor(l_Manifest.CompilerVersion), l_Manifest.Version);
            if (l_Row.Found && !l_Manifest.CompilerVersion.empty() && GetMajorMinor(l_Found.Version) != GetMajorMinor(l_Manifest.CompilerVersion))
            {
                l_Row.Note = std::format("Trinity {} was built with {}, and this one builds its modules too, as only the major version must match", l_Manifest.Version, l_Manifest.CompilerVersion);
            }
        }

        l_Row.Remedy = c_Windows ? std::string("Install Visual Studio's Desktop development with C++ workload, from https://visualstudio.microsoft.com/downloads") : std::format("Install {} {}, or point CXX at one", DescribeCompilerKind(l_Manifest.CompilerId), GetMajor(l_Manifest.CompilerVersion));
        if (!l_Tools.EnvironmentError.empty() || !l_Tools.Compiler)
        {
            l_Report.CSharpExportNote = std::format("Exporting a C# game also needs {}, which NativeAOT links it with", c_Windows ? "Visual Studio's C++ tools" : "a C++ compiler");
        }

        l_Report.Rows.push_back(std::move(l_Row));
    }

    {
        Row l_Row;
        l_Row.Name = "CMake";
        l_Row.Wanted = std::format("{} or newer", Trinity::Tools::c_MinimumCMake);
        l_Row.Short = std::format("CMake {}", l_Row.Wanted);
        l_Row.Purpose = "Configures and builds C++ modules and the players of C++ games";
        l_Row.StopsCpp = true;
        l_Row.Found = l_Tools.CMakeVersion && Trinity::CompareVersions(*l_Tools.CMakeVersion, Trinity::Tools::c_MinimumCMake) >= 0;
        if (!l_Tools.EnvironmentError.empty())
        {
            l_Row.Detail = "Not looked for, as Visual Studio's environment could not be set up";
        }
        else if (l_Tools.CMake.empty())
        {
            l_Row.Detail = std::format("cmake is not{}", l_Where);
        }
        else
        {
            l_Row.Detail = !l_Tools.CMakeVersion ? std::format("{} did not say its version", HubText::ToUtf8(l_Tools.CMake)) : l_Row.Found ? std::format("{} at {}", *l_Tools.CMakeVersion, HubText::ToUtf8(l_Tools.CMake)) : std::format("{} at {}, older than {}", *l_Tools.CMakeVersion, HubText::ToUtf8(l_Tools.CMake), Trinity::Tools::c_MinimumCMake);
        }

        l_Row.Remedy = c_Windows ? std::format("Install CMake {} or newer from https://cmake.org/download, or Visual Studio's C++ CMake tools for Windows", Trinity::Tools::c_MinimumCMake) : std::format("Install CMake {} or newer from https://cmake.org/download, or a package manager's", Trinity::Tools::c_MinimumCMake);
        l_Report.Rows.push_back(std::move(l_Row));
    }

    {
        Row l_Row;
        l_Row.Name = "Ninja";
        l_Row.Wanted = "Any";
        l_Row.Found = !l_Tools.Ninja.empty();
        // Make builds in its place on Linux, more slowly, so only having neither stops C++
        l_Row.StopsCpp = c_Windows || l_Tools.Make.empty();
        l_Row.Short = c_Windows ? "Ninja" : "Ninja or Make";
        l_Row.Purpose = c_Windows ? "Runs the builds of C++ modules and players, file by file, on every core" : "Runs the builds of C++ modules and players, file by file, on every core, where Make would otherwise";
        if (!l_Tools.EnvironmentError.empty())
        {
            l_Row.Detail = "Not looked for, as Visual Studio's environment could not be set up";
        }
        else if (l_Row.Found)
        {
            l_Row.Detail = std::format("{} at {}", l_Tools.NinjaVersion.value_or("A version it did not say"), HubText::ToUtf8(l_Tools.Ninja));
        }
        else
        {
            l_Row.Detail = l_Row.StopsCpp ? std::format("ninja is not{}{}", l_Where, c_Windows ? "" : ", and nor is make") : std::format("ninja is not on the PATH, so Make builds C++ instead, at {}", HubText::ToUtf8(l_Tools.Make));
        }

        l_Row.Remedy = c_Windows ? std::string("Install Visual Studio's C++ CMake tools for Windows, which bring it, or put ninja.exe on the PATH") : std::string("Install Ninja from https://ninja-build.org, or a package manager's");
        l_Report.Rows.push_back(std::move(l_Row));
    }

    if constexpr (c_Windows)
    {
        Row l_GameInput;
        l_GameInput.Name = "GameInput runtime";
        l_GameInput.Wanted = "Any";
        l_GameInput.Purpose = "Reads gamepads in Forge and in games, which have none without it";
        l_GameInput.Found = machine.GamepadRuntime;
        l_GameInput.Detail = l_GameInput.Found ? std::string("Installed") : std::string("Not installed");
        std::error_code l_Error;
        const std::filesystem::path l_Redist = install.Manifest.GetConfigurationDirectory(install.Directory, "Release") / "Redist" / "GameInputRedist.msi";
        l_GameInput.Remedy = std::filesystem::is_regular_file(l_Redist, l_Error) ? std::format("Run {}, which installs it", HubText::ToUtf8(l_Redist)) : std::string("Install the GameInput redistributable, which every Trinity version carries in its Redist folder");
        l_Report.Rows.push_back(std::move(l_GameInput));

        Row l_Vulkan;
        l_Vulkan.Name = "Vulkan loader";
        l_Vulkan.Wanted = "Any";
        l_Vulkan.Purpose = "Lets Forge and games draw with Vulkan. Without it they draw with D3D12 alone";
        l_Vulkan.Found = !machine.VulkanLoader.empty();
        l_Vulkan.Detail = l_Vulkan.Found ? HubText::ToUtf8(machine.VulkanLoader) : std::string("vulkan-1.dll is not in System32");
        l_Vulkan.Remedy = "Install the graphics card's driver, which brings it, or the Vulkan Runtime from https://vulkan.lunarg.com";
        l_Report.Rows.push_back(std::move(l_Vulkan));
    }

    std::vector<std::string> l_CSharp;
    std::vector<std::string> l_Cpp;
    for (const Row& it_Row : l_Report.Rows)
    {
        if (!it_Row.Found && it_Row.StopsCSharp)
        {
            l_CSharp.push_back(it_Row.Short);
        }

        if (!it_Row.Found && it_Row.StopsCpp)
        {
            l_Cpp.push_back(it_Row.Short);
        }
    }

    if (l_Report.CSharpRefusal.empty() && !l_CSharp.empty())
    {
        l_Report.CSharpRefusal = DescribeRefusal("C# scripts", l_Manifest.Version, l_CSharp);
    }

    if (!l_Cpp.empty())
    {
        l_Report.CppRefusal = DescribeRefusal("C++ modules", l_Manifest.Version, l_Cpp);
    }

    return l_Report;
}
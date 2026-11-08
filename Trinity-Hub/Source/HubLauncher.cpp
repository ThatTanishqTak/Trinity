#include "HubLauncher.hpp"

#include "HubText.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <fstream>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
    constexpr std::array<std::string_view, 3> c_ForgeConfigurations{ "Release", "Debug", "Distribution" };
    constexpr std::chrono::seconds c_LookInterval{ 1 };

#if defined(TR_PLATFORM_WINDOWS)
    constexpr std::string_view c_ForgeName = "Trinity-Forge.exe";
#else
    constexpr std::string_view c_ForgeName = "Trinity-Forge";
#endif

    std::optional<std::filesystem::file_time_type> GetWriteTime(const std::filesystem::path& path)
    {
        std::error_code l_Error;
        const std::filesystem::file_time_type l_Time = std::filesystem::last_write_time(path, l_Error);

        return l_Error ? std::nullopt : std::optional(l_Time);
    }

    // A crash on Windows ends a program with an NTSTATUS, which reads as one in hexadecimal
    std::string DescribeExitCode(int exitCode)
    {
        return exitCode < 0 || exitCode > 255 ? std::format("{:#x}", static_cast<std::uint32_t>(exitCode)) : std::to_string(exitCode);
    }

    // The message of the last line the log gives as an error or worse, without its time, logger and level, as [2026-11-07 05:41:29.123] FORGE   error    message, or empty when it has none
    std::string FindLastError(const std::filesystem::path& log)
    {
        std::ifstream l_Stream(log, std::ios::binary);
        std::string l_Line;
        std::string l_Last;
        while (std::getline(l_Stream, l_Line))
        {
            if (!l_Line.empty() && l_Line.back() == '\r')
            {
                l_Line.pop_back();
            }

            const std::size_t l_TimeEnd = l_Line.find("] ");
            if (l_Line.empty() || l_Line.front() != '[' || l_TimeEnd == std::string::npos)
            {
                continue;
            }

            const std::size_t l_LoggerStart = l_Line.find_first_not_of(' ', l_TimeEnd + 2);
            const std::size_t l_LoggerEnd = l_LoggerStart == std::string::npos ? std::string::npos : l_Line.find(' ', l_LoggerStart);
            const std::size_t l_LevelStart = l_LoggerEnd == std::string::npos ? std::string::npos : l_Line.find_first_not_of(' ', l_LoggerEnd);
            const std::size_t l_LevelEnd = l_LevelStart == std::string::npos ? std::string::npos : l_Line.find(' ', l_LevelStart);
            if (l_LevelEnd == std::string::npos)
            {
                continue;
            }

            const std::string_view l_Level = std::string_view(l_Line).substr(l_LevelStart, l_LevelEnd - l_LevelStart);
            const std::size_t l_MessageStart = l_Line.find_first_not_of(' ', l_LevelEnd);
            if ((l_Level == "error" || l_Level == "critical") && l_MessageStart != std::string::npos)
            {
                l_Last = l_Line.substr(l_MessageStart);
            }
        }

        return l_Last;
    }
}

HubLauncher::HubLauncher(std::filesystem::path hubDirectory) : m_HubDirectory(std::move(hubDirectory))
{

}

std::filesystem::path HubLauncher::FindForge(const HubInstall& install)
{
    for (const std::string_view it_Configuration : c_ForgeConfigurations)
    {
        if (!install.Manifest.HasConfiguration(it_Configuration))
        {
            continue;
        }

        const std::filesystem::path l_Forge = install.Manifest.GetConfigurationDirectory(install.Directory, it_Configuration) / HubText::FromUtf8(c_ForgeName);
        std::error_code l_Error;
        if (std::filesystem::is_regular_file(l_Forge, l_Error))
        {
            return l_Forge;
        }
    }

    return {};
}


HubLauncher::Report HubLauncher::Open(const HubProject& project, const HubInstall& install)
{
    return Start(project, install, Purpose::Open, { std::format("--project={}", HubText::ToUtf8(project.Entry.File)) });
}

// Forge backs the project up before anything of it changes, and ends with an error, the project as it was, when it cannot, and when one older than format 10 with both languages or neither is given none
HubLauncher::Report HubLauncher::Upgrade(const HubProject& project, const HubInstall& install, std::optional<Trinity::ScriptLanguage> language)
{
    std::vector<std::string> l_Options{ std::format("--upgrade-project={}", HubText::ToUtf8(project.Entry.File)) };
    if (language)
    {
        l_Options.push_back(std::format("--language={}", Trinity::ToString(*language)));
    }

    return Start(project, install, Purpose::Upgrade, std::move(l_Options));
}

// The lock is only looked at, and let go of at once, so the Forge started takes it. One Hub started moments ago may not have taken it yet, so that one is asked after too. A project open in a Forge is not moved, as that Forge has it in its own version
HubLauncher::Report HubLauncher::Start(const HubProject& project, const HubInstall& install, Purpose purpose, std::vector<std::string> options)
{
    const std::string& l_Name = project.GetName();
    const std::filesystem::path l_Directory = project.Entry.File.parent_path();
    const std::string l_Version = HubInstalls::Describe(install.Manifest);
    const bool l_Upgrade = purpose == Purpose::Upgrade;
    const std::string_view l_Done = l_Upgrade ? "moved" : "opened";
    const auto l_Now = std::chrono::steady_clock::now();
    const auto a_Opening = std::ranges::find_if(m_Launched, [&project, l_Now](Launched& launched)
    {
        return (l_Now - launched.StartedAt < c_StartWindow || IsUnmoved(launched)) && !launched.Process.Poll() && Trinity::ProjectList::IsSameFile(launched.ProjectFile, project.Entry.File);
    });

    if (a_Opening != m_Launched.end())
    {
        const std::string_view l_Doing = a_Opening->Doing == Purpose::Create ? "being made" : a_Opening->Doing == Purpose::Upgrade ? "being moved to a newer version" : "opening";

        return { std::format("{} is {} in Forge already", l_Name, l_Doing), false };
    }

    if (const Trinity::Expected<Trinity::FileLock, Trinity::FileLockRefusal> l_Lock = Trinity::FileLock::TryAcquire(Trinity::Project::GetLockFile(l_Directory)); !l_Lock && l_Lock.GetError().Held)
    {
        const std::uint32_t l_Holder = l_Lock.GetError().HolderID;
        const bool l_Forward = l_Holder != 0 && Trinity::Process::BringToFront(l_Holder);
        const std::string l_Open = std::format("{} is open in Forge already{}{}", l_Name, l_Holder != 0 ? std::format(", process {}", l_Holder) : std::string(), l_Forward ? ", which is brought forward" : "");

        return l_Upgrade ? Report{ std::format("{}, so it is not moved to {} until that Forge closes it", l_Open, l_Version), true } : Report{ l_Open, false };
    }

    const std::filesystem::path l_Forge = FindForge(install);
    if (l_Forge.empty())
    {
        return { std::format("{} was not {}, as {} in {} has no Forge in its Release, Debug or Distribution build", l_Name, l_Done, l_Version, HubText::ToUtf8(install.Directory)), true };
    }

    const std::filesystem::path l_Logs = l_Directory / "Cache" / "Logs";
    std::vector<std::string> l_Arguments = std::move(options);
    l_Arguments.push_back(std::format("--log-dir={}", HubText::ToUtf8(l_Logs)));
    l_Arguments.push_back(std::format("--hub={}", HubText::ToUtf8(m_HubDirectory)));
    std::filesystem::path l_Log = l_Logs / l_Forge.stem();
    l_Log += ".log";
    const std::optional<std::filesystem::file_time_type> l_LogTime = GetWriteTime(l_Log);

    Trinity::Expected<Trinity::StartedProcess, std::string> l_Process = Trinity::StartedProcess::Start(l_Forge, l_Arguments, l_Directory);
    if (!l_Process)
    {
        return { std::format("{} was not {}, as {}", l_Name, l_Done, l_Process.GetError()), true };
    }

    const std::uint32_t l_ID = l_Process->GetID();
    m_Launched.push_back({ project.Entry.File, l_Name, std::move(*l_Process), l_Now, l_Log, l_LogTime, GetWriteTime(Trinity::GetLastExportPath(l_Directory)), purpose, false, l_Upgrade ? project.GetEngineVersion() : std::string() });
    if (l_Upgrade)
    {
        const std::string l_From = project.GetEngineVersion().empty() ? std::string(", which names no version,") : std::format(" from Trinity {}", project.GetEngineVersion());

        return { std::format("Moving {}{} to {}, backing it up first, process {}", l_Name, l_From, l_Version, l_ID), false };
    }


    return { std::format("Opening {} in {}, process {}", l_Name, l_Version, l_ID), false };
}

// Started in Hub's own folder, logging into its Logs, as the project's folder must be new or empty for Forge to make it. Forge adds the project to the list as it makes it, through --hub, and opens it once made
HubLauncher::Report HubLauncher::Create(const Creation& creation, const HubInstall& install)
{
    const std::string l_Name = HubText::ToUtf8(creation.Directory.filename());
    const std::string l_Folder = HubText::ToUtf8(creation.Directory);
    const std::string l_Version = HubInstalls::Describe(install.Manifest);
    std::error_code l_Error;
    const bool l_Existed = std::filesystem::exists(creation.Directory, l_Error);
    if (l_Existed && (!std::filesystem::is_directory(creation.Directory, l_Error) || !std::filesystem::is_empty(creation.Directory, l_Error)))
    {
        return { std::format("{} was not made, as {} is there already and is not an empty folder", l_Name, l_Folder), true };
    }

    const std::filesystem::path l_Forge = FindForge(install);
    if (l_Forge.empty())
    {
        return { std::format("{} was not made, as {} in {} has no Forge in its Release, Debug or Distribution build", l_Name, l_Version, HubText::ToUtf8(install.Directory)), true };
    }

    const std::filesystem::path l_Logs = m_HubDirectory / std::filesystem::path(c_LogsFolder);
    std::vector<std::string> l_Arguments{ std::format("--new-project={}", l_Folder), std::format("--language={}", Trinity::ToString(creation.Language)), std::format("--log-dir={}", HubText::ToUtf8(l_Logs)), std::format("--hub={}", HubText::ToUtf8(m_HubDirectory)) };
    if (!creation.Template.empty())
    {
        l_Arguments.push_back(std::format("--template={}", creation.Template));
    }

    std::filesystem::path l_Log = l_Logs / l_Forge.stem();
    l_Log += ".log";
    const std::optional<std::filesystem::file_time_type> l_LogTime = GetWriteTime(l_Log);
    Trinity::Expected<Trinity::StartedProcess, std::string> l_Process = Trinity::StartedProcess::Start(l_Forge, l_Arguments, m_HubDirectory);
    if (!l_Process)
    {
        return { std::format("{} was not made, as {}", l_Name, l_Process.GetError()), true };
    }

    const std::uint32_t l_ID = l_Process->GetID();
    std::filesystem::path l_File = creation.Directory / HubText::FromUtf8(l_Name);
    l_File += Trinity::Project::c_Extension;
    m_Launched.push_back({ std::move(l_File), l_Name, std::move(*l_Process), std::chrono::steady_clock::now(), l_Log, l_LogTime, std::nullopt, Purpose::Create, !l_Existed, std::string() });
    const std::string l_Language = creation.Language == Trinity::ScriptLanguage::CSharp ? "C#" : "C++";

    return { std::format("Making {} in {}, {}, its scripts in {}, in {}, process {}", l_Name, l_Folder, creation.Template.empty() ? std::string("as an empty project") : std::format("from the {} template", creation.Template), l_Language, l_Version, l_ID), false };
}

// Read again as it is asked for, since the list may be older than the last export
HubLauncher::Report HubLauncher::RunBuild(const HubProject& project)
{
    const std::string& l_Name = project.GetName();
    const Trinity::Expected<Trinity::LastExport, std::string> l_Export = Trinity::ReadLastExport(project.Entry.File.parent_path());
    if (!l_Export)
    {
        return { std::format("{} has no build to run, as {}", l_Name, l_Export.GetError()), true };
    }

    const std::filesystem::path l_Folder = l_Export->Executable.parent_path();
    std::error_code l_Error;
    if (!std::filesystem::is_regular_file(l_Export->Executable, l_Error))
    {
        return { std::format("{}'s last export is gone from {}", l_Export->Product, HubText::ToUtf8(l_Folder)), true };
    }

    if (const Trinity::Expected<void, std::string> l_Started = Trinity::Process::Start(l_Export->Executable, std::span<const std::string>(), l_Folder); !l_Started)
    {
        return { std::format("{} was not run, as {}", l_Export->Product, l_Started.GetError()), true };
    }

    const std::optional<std::chrono::sys_seconds> l_Finished = HubText::ParseTime(l_Export->FinishedAt);

    return { std::format("Running {}{}, exported {}from {}", l_Export->Product, l_Export->Development ? ", a Development Build" : "", l_Finished ? std::format("{} ", HubText::DescribeAge(*l_Finished)) : std::string(), HubText::ToUtf8(l_Folder)), false };
}

// Each Forge is asked every frame, which costs a call to the system, and the exports they record are looked at once a second. One moving a project fails until the project names its new version, as its backup may take far longer than the first seconds
std::optional<HubLauncher::Report> HubLauncher::Update()
{
    if (m_Launched.empty())
    {
        return std::nullopt;
    }

    const auto l_Now = std::chrono::steady_clock::now();
    const bool l_Look = l_Now >= m_NextLook;
    if (l_Look)
    {
        m_NextLook = l_Now + c_LookInterval;
    }

    std::optional<Report> l_Report;
    std::erase_if(m_Launched, [this, &l_Report, l_Now, l_Look](Launched& launched)
    {
        const std::optional<int> l_ExitCode = launched.Process.Poll();
        if (!l_ExitCode)
        {
            if (l_Look)
            {
                const std::optional<std::filesystem::file_time_type> l_ExportTime = GetWriteTime(Trinity::GetLastExportPath(launched.ProjectFile.parent_path()));
                if (l_ExportTime != launched.ExportTime)
                {
                    launched.ExportTime = l_ExportTime;
                    m_Changed = true;
                }
            }

            return false;
        }

        m_Changed = true;
        if (*l_ExitCode != 0 && (l_Now - launched.StartedAt < c_StartWindow || IsUnmoved(launched)))
        {
            l_Report = Report{ DescribeFailure(launched, *l_ExitCode) + (launched.Doing == Purpose::Create ? RemoveFailedCreation(launched) : std::string()), true };
        }
        else
        {
            TR_INFO("Hub: the Forge with {} ended, with exit code {}", launched.Name, DescribeExitCode(*l_ExitCode));
        }

        return true;
    });

    return l_Report;
}

bool HubLauncher::TakeChanged()
{
    return std::exchange(m_Changed, false);
}

// A Forge that ended before its log was opened, as one missing a library does, left the log of the time before, if any, which is not read
std::string HubLauncher::DescribeFailure(const Launched& launched, int exitCode)
{
    const std::string l_Code = DescribeExitCode(exitCode);
    const std::string_view l_Doing = launched.Doing == Purpose::Create ? "make" : launched.Doing == Purpose::Upgrade ? "move" : "open";
    const std::string l_Left = launched.Doing == Purpose::Upgrade && IsUnmoved(launched) ? std::format(". {} is left as it was, {}", launched.Name, launched.FromVersion.empty() ? std::string("naming no version") : std::format("in Trinity {}", launched.FromVersion)) : std::string();
    const std::optional<std::filesystem::file_time_type> l_LogTime = GetWriteTime(launched.Log);
    if (!l_LogTime || l_LogTime == launched.LogTime)
    {
        return std::format("Forge ended with exit code {} before it could {} {}, and wrote no log{}", l_Code, l_Doing, launched.Name, l_Left);
    }

    const std::string l_Error = FindLastError(launched.Log);


    return std::format("Forge could not {} {}, and ended with exit code {}{}{}. Its log is {}", l_Doing, launched.Name, l_Code, l_Error.empty() ? std::string() : std::format(": {}", l_Error), l_Left, HubText::ToUtf8(launched.Log));
}

// Read from the disk as it ends, since the Forge writes the project's file with its new version once it is backed up and opened
bool HubLauncher::IsUnmoved(const Launched& launched)
{
    if (launched.Doing != Purpose::Upgrade)
    {
        return false;
    }

    const Trinity::Expected<Trinity::ProjectFileInfo, std::string> l_Info = Trinity::Project::ReadInfo(launched.ProjectFile);

    return l_Info && l_Info->EngineVersion == launched.FromVersion;
}

// A Forge that failed by itself took away what it made, so a folder still there is what one that crashed left. Only a folder that was not there before Create is taken away, so nothing the person had is touched, and the project comes off the list, which Forge may have added it to
std::string HubLauncher::RemoveFailedCreation(const Launched& launched) const
{
    const std::filesystem::path l_Directory = launched.ProjectFile.parent_path();
    std::error_code l_Error;
    if (!std::filesystem::exists(l_Directory, l_Error))
    {
        return std::string();
    }

    if (!launched.RemoveOnFailure)
    {
        return std::format(". What it made is left in {}, which was there before", HubText::ToUtf8(l_Directory));
    }

    std::filesystem::remove_all(l_Directory, l_Error);
    if (l_Error)
    {
        return std::format(". What it left in {} could not all be taken away: {}", HubText::ToUtf8(l_Directory), l_Error.message());
    }

    if (const Trinity::Expected<void, std::string> l_Removed = Trinity::ProjectList::Remove(m_HubDirectory / std::filesystem::path(Trinity::ProjectList::c_FileName), launched.ProjectFile); !l_Removed)
    {
        TR_WARN("Hub: {} was not taken off the list, as {}", launched.Name, l_Removed.GetError());
    }

    return std::format(". Hub took away what it left in {}", HubText::ToUtf8(l_Directory));
}
#include "HubInstaller.hpp"

#include "HubInstalls.hpp"
#include "HubText.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <string_view>
#include <system_error>

namespace
{
    constexpr std::string_view c_StagingSuffix = ".staging";
    constexpr std::string_view c_ReplacedSuffix = ".old";
    constexpr std::string_view c_DeletingSuffix = ".removing";
    constexpr std::string_view c_DownloadSuffix = ".download";
    constexpr std::size_t c_HashFileLimit = 4096;
    // Left free beyond what a version unpacks to, for what else is writing to the disk meanwhile
    constexpr std::uint64_t c_SpareBytes = 64ull * 1024 * 1024;
    constexpr std::size_t c_HashReadSize = 1024 * 1024;
    constexpr std::size_t c_ManifestLimit = 64 * 1024;
    constexpr std::size_t c_HashLength = 64;

    // A folder beside a version's, which starts with a dot so file managers keep it out of sight
    std::filesystem::path GetSideFolder(const std::filesystem::path& directory, std::string_view suffix)
    {
        return directory.parent_path() / HubText::FromUtf8(std::format(".{}{}", HubText::ToUtf8(directory.filename()), suffix));
    }

    // A path in a zip that stays inside the folder it unpacks into, on every platform: no root, drive or backslash, and no folder that is empty, . or ..
    bool IsSafePath(std::string_view path)
    {
        if (path.empty() || path.front() == '/' || path.find_first_of("\\:") != std::string_view::npos)
        {
            return false;
        }

        if (path.back() == '/')
        {
            path.remove_suffix(1);
        }

        while (true)
        {
            const std::size_t l_End = path.find('/');
            const std::string_view l_Part = path.substr(0, l_End);
            if (l_Part.empty() || l_Part == "." || l_Part == "..")
            {
                return false;
            }

            if (l_End == std::string_view::npos)
            {
                return true;
            }

            path.remove_prefix(l_End + 1);
        }
    }

    bool IsInside(const std::filesystem::path& path, const std::filesystem::path& folder)
    {
        std::error_code l_Error;
        const std::filesystem::path l_Path = std::filesystem::weakly_canonical(path, l_Error);
        const std::filesystem::path l_Folder = std::filesystem::weakly_canonical(folder, l_Error);
        const auto a_Mismatch = std::mismatch(l_Folder.begin(), l_Folder.end(), l_Path.begin(), l_Path.end());

        return !l_Folder.empty() && a_Mismatch.first == l_Folder.end();
    }

    // The first word of a .sha256, as sha256sum and Package.cmake write one, in lowercase, or empty when it is no SHA-256
    std::string ParseExpectedHash(std::string_view text)
    {
        const std::size_t l_Start = text.find_first_not_of(" \t\r\n");
        if (l_Start == std::string::npos || text.size() - l_Start < c_HashLength)
        {
            return {};
        }

        std::string l_Hash(text.substr(l_Start, c_HashLength));
        const bool l_Hex = std::ranges::all_of(l_Hash, [](char character) { return std::isxdigit(static_cast<unsigned char>(character)) != 0; });
        const bool l_Ends = text.size() - l_Start == c_HashLength || std::isspace(static_cast<unsigned char>(text[l_Start + c_HashLength])) != 0;
        if (!l_Hex || !l_Ends)
        {
            return {};
        }

        std::ranges::transform(l_Hash, l_Hash.begin(), [](char character) { return static_cast<char>(std::tolower(static_cast<unsigned char>(character))); });

        return l_Hash;
    }

    std::string ReadExpectedHash(const std::filesystem::path& path)
    {
        std::ifstream l_File(path, std::ios::binary);
        std::string l_Text(256, '\0');
        l_File.read(l_Text.data(), static_cast<std::streamsize>(l_Text.size()));
        l_Text.resize(static_cast<std::size_t>(std::max<std::streamsize>(l_File.gcount(), 0)));

        return ParseExpectedHash(l_Text);
    }

    using HubText::ToUtf8;
}

HubInstaller::~HubInstaller()
{
    if (m_Running)
    {
        m_Running->Stop.store(true, std::memory_order_relaxed);
        Trinity::JobSystem::Wait(m_Jobs);
        if (!m_Running->DownloadFolder.empty())
        {
            // Closed first, as Windows deletes no file still open
            m_Running->Archive = Trinity::ZipArchive();
            std::error_code l_Error;
            std::filesystem::remove_all(m_Running->DownloadFolder, l_Error);
        }
    }
}

void HubInstaller::Install(std::filesystem::path zip, std::filesystem::path installsDirectory, std::vector<std::pair<std::string, std::filesystem::path>> installed)
{
    TR_ASSERT(!m_Running, "Hub is installing or deleting a version already");

    m_Running = std::make_unique<Running>();
    m_Running->Zip = std::move(zip);
    m_Running->InstallsDirectory = std::move(installsDirectory);
    m_Running->Installed = std::move(installed);
    Submit(&Prepare);
}

void HubInstaller::Download(const HubRelease& release, std::filesystem::path installsDirectory, std::vector<std::pair<std::string, std::filesystem::path>> installed)
{
    TR_ASSERT(!m_Running, "Hub is installing or deleting a version already");

    m_Running = std::make_unique<Running>();
    m_Running->Release = release;
    m_Running->Zip = HubText::FromUtf8(release.ZipName);
    m_Running->DownloadFolder = installsDirectory / HubText::FromUtf8(std::format(".{}{}", release.ZipName, c_DownloadSuffix));
    m_Running->InstallsDirectory = std::move(installsDirectory);
    m_Running->Installed = std::move(installed);
    m_Running->Current.store(Stage::Downloading);
    Submit(&Receive);
}

void HubInstaller::Replace()
{
    if (GetStage() != Stage::Asking)
    {
        return;
    }

    Trinity::JobSystem::Wait(m_Jobs);
    m_Running->Finished.store(false, std::memory_order_relaxed);
    m_Running->Current.store(Stage::Unpacking);
    Submit(&Unpack);
}

void HubInstaller::KeepInstalled()
{
    if (GetStage() != Stage::Asking)
    {
        return;
    }

    Trinity::JobSystem::Wait(m_Jobs);
    m_Running->Outcome.Cancelled = true;
    m_Running->Outcome.Message = std::format("Kept {} in {}, and installed nothing from {}", HubInstalls::Describe(m_Running->Manifest), ToUtf8(m_Running->Destination), ToUtf8(m_Running->Zip.filename()));
    m_Running->Current.store(Stage::Idle);
}

void HubInstaller::Delete(std::filesystem::path directory, Trinity::EngineManifest manifest)
{
    TR_ASSERT(!m_Running, "Hub is installing or deleting a version already");

    m_Running = std::make_unique<Running>();
    m_Running->Destination = std::move(directory);
    m_Running->Manifest = std::move(manifest);
    m_Running->Current.store(Stage::Deleting);
    Submit(&Remove);
}

void HubInstaller::Cancel()
{
    if (!m_Running)
    {
        return;
    }

    if (GetStage() == Stage::Asking)
    {
        KeepInstalled();

        return;
    }

    m_Running->Stop.store(true, std::memory_order_relaxed);
}

std::optional<HubInstaller::Result> HubInstaller::Update()
{
    if (!m_Running || !m_Running->Finished.load(std::memory_order_acquire) || m_Running->Current.load() == Stage::Asking)
    {
        return std::nullopt;
    }

    Trinity::JobSystem::Wait(m_Jobs);
    Result l_Result = std::move(m_Running->Outcome);
    if (!m_Running->DownloadFolder.empty())
    {
        // Closed first, as Windows deletes no file still open
        m_Running->Archive = Trinity::ZipArchive();
        std::error_code l_Error;
        std::filesystem::remove_all(m_Running->DownloadFolder, l_Error);
    }

    m_Running.reset();

    return l_Result;
}

HubInstaller::Stage HubInstaller::GetStage() const
{
    return m_Running ? m_Running->Current.load() : Stage::Idle;
}

float HubInstaller::GetProgress() const
{
    if (!m_Running)
    {
        return -1.0f;
    }

    const std::uint64_t l_Total = m_Running->Total.load(std::memory_order_relaxed);
    if (l_Total == 0)
    {
        return -1.0f;
    }

    return static_cast<float>(static_cast<double>(m_Running->Done.load(std::memory_order_relaxed)) / static_cast<double>(l_Total));
}

// What the stage names was written before the stage was, so it is read here as the job left it
std::string HubInstaller::Describe() const
{
    if (!m_Running)
    {
        return {};
    }

    const std::string l_Zip = ToUtf8(m_Running->Zip.filename());
    switch (m_Running->Current.load())
    {
        case Stage::Downloading:
        {
            const std::uint64_t l_Total = m_Running->Total.load(std::memory_order_relaxed);
            const std::string_view l_Url = m_Running->Release ? std::string_view(m_Running->Release->ZipUrl) : std::string_view();
            const std::string_view l_Host = l_Url.substr(l_Url.find("://") == std::string_view::npos ? 0 : l_Url.find("://") + 3);

            return std::format("Downloading {} from {}{}", l_Zip, l_Host.substr(0, l_Host.find('/')), l_Total > 0 ? std::format(", {} of {}", Trinity::Memory::FormatBytes(m_Running->Done.load(std::memory_order_relaxed)), Trinity::Memory::FormatBytes(l_Total)) : std::string());
        }
        case Stage::Checking:
        {
            return std::format("Checking {} against its .sha256", l_Zip);
        }
        case Stage::Reading:
        {
            return std::format("Reading {}", l_Zip);
        }
        case Stage::Asking:
        {
            return std::format("{} for {} on {} is installed already in {}. Replace it with the one in {}?", HubInstalls::Describe(m_Running->Manifest), m_Running->Manifest.Platform, m_Running->Manifest.Architecture, ToUtf8(m_Running->Destination), l_Zip);
        }
        case Stage::Unpacking:
        {
            return std::format("Unpacking {} into {}", HubInstalls::Describe(m_Running->Manifest), ToUtf8(m_Running->Destination));
        }
        case Stage::Deleting:
        {
            return std::format("Deleting {} in {}", HubInstalls::Describe(m_Running->Manifest), ToUtf8(m_Running->Destination));
        }
        case Stage::Idle:
        {
            break;
        }
    }

    return {};
}

void HubInstaller::CleanUp(const std::filesystem::path& installsDirectory)
{
    std::error_code l_Error;
    std::vector<std::filesystem::path> l_Left;
    for (std::filesystem::directory_iterator it_Entry(installsDirectory, l_Error); !l_Error && it_Entry != std::filesystem::directory_iterator(); it_Entry.increment(l_Error))
    {
        const std::string l_Name = ToUtf8(it_Entry->path().filename());
        if (l_Name.starts_with('.') && (l_Name.ends_with(c_StagingSuffix) || l_Name.ends_with(c_ReplacedSuffix) || l_Name.ends_with(c_DeletingSuffix) || l_Name.ends_with(c_DownloadSuffix)))
        {
            l_Left.push_back(it_Entry->path());
        }
    }

    for (const std::filesystem::path& it_Path : l_Left)
    {
        const std::string l_Name = ToUtf8(it_Path.filename());
        std::error_code l_PathError;
        if (l_Name.ends_with(c_ReplacedSuffix))
        {
            const std::filesystem::path l_Original = installsDirectory / HubText::FromUtf8(std::string_view(l_Name).substr(1, l_Name.size() - 1 - c_ReplacedSuffix.size()));
            if (!std::filesystem::exists(l_Original, l_PathError))
            {
                std::filesystem::rename(it_Path, l_Original, l_PathError);
                TR_INFO("Hub: {} was being replaced as Hub last ended, and is put back", ToUtf8(l_Original));

                continue;
            }
        }

        std::filesystem::remove_all(it_Path, l_PathError);
        TR_INFO("Hub: deleted {}, which an install or delete cut short left{}", ToUtf8(it_Path), l_PathError ? std::format(", though not all of it: {}", l_PathError.message()) : std::string());
    }
}

void HubInstaller::Submit(void (*work)(Running&))
{
    Running* l_Running = m_Running.get();
    Trinity::JobSystem::Submit([l_Running, work] { work(*l_Running); }, &m_Jobs);
}

// The release's .sha256 first, when it has one, then its zip, hashed as it arrives, which goes on to be installed only when it matches. GitHub serves a release's files by a redirect, which is followed
void HubInstaller::Receive(Running& running)
{
    const HubRelease& l_Release = *running.Release;
    const std::string& l_Zip = l_Release.ZipName;
    const auto l_Finish = [&running](std::string message, bool cancelled = false)
    {
        running.Outcome.Message = std::move(message);
        running.Outcome.Cancelled = cancelled;
        running.Finished.store(true, std::memory_order_release);
    };

    const auto l_Failed = [&l_Finish, &l_Zip](const Trinity::HttpError& error, std::string_view what)
    {
        if (error.Failure == Trinity::HttpFailure::Cancelled)
        {
            l_Finish(std::format("Downloading {} was cancelled, and nothing was installed", l_Zip), true);

            return;
        }

        l_Finish(std::format("{} was not downloaded, as {} could not be{}: {}", l_Zip, what, error.Failure == Trinity::HttpFailure::Unreachable ? ", as this machine may be offline" : "", error.Reason));
    };

    std::error_code l_Error;
    std::filesystem::remove_all(running.DownloadFolder, l_Error);
    if (!std::filesystem::create_directories(running.DownloadFolder, l_Error) || l_Error)
    {
        l_Finish(std::format("{} was not downloaded, as {} could not be made: {}", l_Zip, ToUtf8(running.DownloadFolder), l_Error.message()));

        return;
    }

    std::string l_Expected = l_Release.Digest;
    std::string l_Source = "GitHub's digest of it";
    if (!l_Release.Sha256Url.empty())
    {
        Trinity::HttpRequest l_Request;
        l_Request.Url = l_Release.Sha256Url;
        l_Request.UserAgent = HubReleases::GetUserAgent();
        l_Request.BodyLimit = c_HashFileLimit;
        l_Request.Cancel = &running.Stop;
        const Trinity::Expected<Trinity::HttpResponse, Trinity::HttpError> l_Response = Trinity::Http::Get(l_Request);
        if (!l_Response)
        {
            l_Failed(l_Response.GetError(), "its .sha256");

            return;
        }

        if (l_Response->Status != 200)
        {
            l_Finish(std::format("{} was not downloaded, as its .sha256 was answered with {}", l_Zip, l_Response->Status));

            return;
        }

        l_Expected = ParseExpectedHash(l_Response->Body);
        l_Source = std::format("{}.sha256", l_Zip);
        if (l_Expected.empty())
        {
            l_Finish(std::format("{} was not downloaded, as its .sha256 holds no SHA-256", l_Zip));

            return;
        }
    }

    const std::filesystem::path l_Path = running.DownloadFolder / HubText::FromUtf8(l_Zip);
    std::ofstream l_File(l_Path, std::ios::binary | std::ios::trunc);
    if (!l_File)
    {
        l_Finish(std::format("{} was not downloaded, as {} could not be written", l_Zip, ToUtf8(l_Path)));

        return;
    }

    running.Total.store(l_Release.ZipSize, std::memory_order_relaxed);
    running.Done.store(0, std::memory_order_relaxed);
    Trinity::Hash::Sha256 l_Hash;
    bool l_WriteFailed = false;
    Trinity::HttpRequest l_Request;
    l_Request.Url = l_Release.ZipUrl;
    l_Request.UserAgent = HubReleases::GetUserAgent();
    l_Request.Cancel = &running.Stop;
    l_Request.OnBody = [&running, &l_File, &l_Hash, &l_WriteFailed](std::span<const std::byte> piece)
    {
        l_File.write(reinterpret_cast<const char*>(piece.data()), static_cast<std::streamsize>(piece.size()));
        if (!l_File)
        {
            l_WriteFailed = true;

            return false;
        }

        l_Hash.Update(piece);
        running.Done.fetch_add(piece.size(), std::memory_order_relaxed);

        return true;
    };

    const Trinity::Expected<Trinity::HttpResponse, Trinity::HttpError> l_Response = Trinity::Http::Get(l_Request);
    l_File.close();
    if (l_WriteFailed || (l_Response && l_File.fail()))
    {
        l_Finish(std::format("{} was not downloaded, as {} could not be written, perhaps for want of space", l_Zip, ToUtf8(l_Path)));

        return;
    }

    if (!l_Response)
    {
        l_Failed(l_Response.GetError(), "it");

        return;
    }

    if (l_Response->Status != 200)
    {
        l_Finish(std::format("{} was not downloaded, as it was answered with {}", l_Zip, l_Response->Status));

        return;
    }

    const std::uint64_t l_Received = running.Done.load(std::memory_order_relaxed);
    if (l_Release.ZipSize != 0 && l_Received != l_Release.ZipSize)
    {
        l_Finish(std::format("{} was not installed, as {} of the {} bytes GitHub gives came, so the download was cut short", l_Zip, l_Received, l_Release.ZipSize));

        return;
    }

    const std::string l_Actual = Trinity::Hash::Sha256::ToHex(l_Hash.Finish());
    if (l_Actual != l_Expected)
    {
        l_Finish(std::format("{} was downloaded, but not installed, as it does not match {}, so it was damaged on its way: it hashes to {}, and {} was expected", l_Zip, l_Source, l_Actual, l_Expected));

        return;
    }

    running.Zip = l_Path;
    running.Verified = true;
    running.Current.store(Stage::Reading);
    Prepare(running);
}

// Everything that can refuse the zip does so here, before a file is unpacked
void HubInstaller::Prepare(Running& running)
{
    const std::string l_Zip = ToUtf8(running.Zip.filename());
    const auto l_Finish = [&running](std::string message, bool cancelled = false)
    {
        running.Outcome.Message = std::move(message);
        running.Outcome.Cancelled = cancelled;
        running.Finished.store(true, std::memory_order_release);
    };

    std::error_code l_Error;
    std::filesystem::path l_HashPath = running.Zip;
    l_HashPath += ".sha256";
    if (!running.Verified && std::filesystem::is_regular_file(l_HashPath, l_Error))
    {
        const std::string l_Expected = ReadExpectedHash(l_HashPath);
        if (l_Expected.empty())
        {
            l_Finish(std::format("{} was not installed, as {}.sha256 beside it holds no SHA-256", l_Zip, l_Zip));

            return;
        }

        std::ifstream l_File(running.Zip, std::ios::binary);
        if (!l_File)
        {
            l_Finish(std::format("{} could not be opened", l_Zip));

            return;
        }

        running.Total.store(std::filesystem::file_size(running.Zip, l_Error), std::memory_order_relaxed);
        Trinity::Hash::Sha256 l_Hash;
        std::vector<char> l_Buffer(c_HashReadSize);
        while (l_File.read(l_Buffer.data(), static_cast<std::streamsize>(l_Buffer.size())) || l_File.gcount() > 0)
        {
            if (running.Stop.load(std::memory_order_relaxed))
            {
                l_Finish(std::format("Installing {} was cancelled, and nothing was installed", l_Zip), true);

                return;
            }

            const std::size_t l_Read = static_cast<std::size_t>(l_File.gcount());
            l_Hash.Update(std::as_bytes(std::span(l_Buffer.data(), l_Read)));
            running.Done.fetch_add(l_Read, std::memory_order_relaxed);
        }

        const std::string l_Actual = Trinity::Hash::Sha256::ToHex(l_Hash.Finish());
        if (l_Actual != l_Expected)
        {
            l_Finish(std::format("{} was not installed, as it does not match {}.sha256, so it was damaged on its way or is not the file the hash is for: it hashes to {}, and the .sha256 gives {}", l_Zip, l_Zip, l_Actual, l_Expected));

            return;
        }
    }

    running.Current.store(Stage::Reading);
    running.Total.store(0, std::memory_order_relaxed);
    running.Done.store(0, std::memory_order_relaxed);

    Trinity::Expected<Trinity::ZipArchive, std::string> l_Archive = Trinity::ZipArchive::Open(running.Zip);
    if (!l_Archive)
    {
        l_Finish(std::format("{} was not installed, as {}", l_Zip, l_Archive.GetError()));

        return;
    }

    running.Archive = std::move(*l_Archive);
    const std::vector<Trinity::ZipEntry>& l_Entries = running.Archive.GetEntries();

    // At the zip's top, or in the one folder that holds everything else, as Package.cmake zips a version
    const Trinity::ZipEntry* l_ManifestEntry = running.Archive.Find(Trinity::EngineManifest::c_FileName);
    if (l_ManifestEntry == nullptr && !l_Entries.empty())
    {
        const std::size_t l_Slash = l_Entries.front().Name.find('/');
        if (l_Slash != std::string::npos)
        {
            const std::string l_Top = l_Entries.front().Name.substr(0, l_Slash + 1);
            if (std::ranges::all_of(l_Entries, [&l_Top](const Trinity::ZipEntry& entry) { return entry.Name.starts_with(l_Top); }))
            {
                l_ManifestEntry = running.Archive.Find(l_Top + std::string(Trinity::EngineManifest::c_FileName));
                running.Root = l_ManifestEntry != nullptr ? l_Top : std::string();
            }
        }
    }

    if (l_ManifestEntry == nullptr)
    {
        l_Finish(std::format("{} was not installed, as it holds no {} at its top, so it is no engine version", l_Zip, Trinity::EngineManifest::c_FileName));

        return;
    }

    const Trinity::Expected<std::string, std::string> l_ManifestText = running.Archive.ReadText(*l_ManifestEntry, c_ManifestLimit);
    if (!l_ManifestText)
    {
        l_Finish(std::format("{} was not installed, as its {} could not be read: {}", l_Zip, Trinity::EngineManifest::c_FileName, l_ManifestText.GetError()));

        return;
    }

    Trinity::Expected<Trinity::EngineManifest, std::string> l_Manifest = Trinity::ParseEngineManifest(*l_ManifestText, std::format("{} in {}", Trinity::EngineManifest::c_FileName, l_Zip));
    if (!l_Manifest)
    {
        running.Outcome.NeedsNewerHub = HubText::ReadFormat(std::string_view(*l_ManifestText)).value_or(0) > Trinity::EngineManifest::c_FormatVersion;
        l_Finish(std::format("{} was not installed: {}", l_Zip, l_Manifest.GetError()));

        return;
    }

    running.Manifest = std::move(*l_Manifest);
    const Trinity::EngineManifest& l_Version = running.Manifest;
    if (l_Version.Platform != Trinity::GetBuildPlatform() || l_Version.Architecture != Trinity::GetBuildArchitecture())
    {
        l_Finish(std::format("{} was not installed, as it holds {} for {} on {}, and this machine runs {} on {}", l_Zip, HubInstalls::Describe(l_Version), l_Version.Platform, l_Version.Architecture, Trinity::GetBuildPlatform(), Trinity::GetBuildArchitecture()));

        return;
    }

    if (l_Version.Version.find_first_not_of("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-+") != std::string::npos)
    {
        l_Finish(std::format("{} was not installed, as its version, {}, cannot name a folder", l_Zip, l_Version.Version));

        return;
    }

    std::uint64_t l_Total = 0;
    for (const Trinity::ZipEntry& it_Entry : l_Entries)
    {
        const std::string_view l_Path = std::string_view(it_Entry.Name).substr(running.Root.size());
        if (l_Path.empty())
        {
            continue;
        }

        if (!IsSafePath(l_Path))
        {
            l_Finish(std::format("{} was not installed, as it holds {}, whose path leads outside the version's folder", l_Zip, it_Entry.Name));

            return;
        }

        if (it_Entry.IsLink())
        {
            l_Finish(std::format("{} was not installed, as it holds a link, {}, and an engine version holds none", l_Zip, it_Entry.Name));

            return;
        }

        l_Total += it_Entry.Size;
    }

    const std::string l_Name = HubInstalls::GetInstallName(l_Version);
    const auto a_Installed = std::ranges::find(running.Installed, l_Name, &std::pair<std::string, std::filesystem::path>::first);
    running.Destination = a_Installed != running.Installed.end() ? a_Installed->second : running.InstallsDirectory / HubText::FromUtf8(l_Name);
    running.Replacing = std::filesystem::exists(running.Destination, l_Error);

    std::filesystem::path l_Disk = running.Destination.parent_path();
    while (!l_Disk.empty() && !std::filesystem::exists(l_Disk, l_Error) && l_Disk.has_parent_path() && l_Disk.parent_path() != l_Disk)
    {
        l_Disk = l_Disk.parent_path();
    }

    const std::filesystem::space_info l_Space = std::filesystem::space(l_Disk, l_Error);
    if (!l_Error && l_Space.available < l_Total + c_SpareBytes)
    {
        l_Finish(std::format("{} was not installed, as it unpacks to {}, and {} has {} free", l_Zip, Trinity::Memory::FormatBytes(l_Total), ToUtf8(l_Disk), Trinity::Memory::FormatBytes(l_Space.available)));

        return;
    }

    if (running.Replacing && IsInside(Trinity::Process::GetExecutableDirectory(), running.Destination))
    {
        l_Finish(std::format("{} was not installed, as it would replace {} in {}, which Hub runs from and cannot replace while it is open", l_Zip, HubInstalls::Describe(l_Version), ToUtf8(running.Destination)));

        return;
    }

    if (running.Stop.load(std::memory_order_relaxed))
    {
        l_Finish(std::format("Installing {} was cancelled, and nothing was installed", l_Zip), true);

        return;
    }

    running.Total.store(l_Total, std::memory_order_relaxed);
    if (running.Replacing)
    {
        running.Current.store(Stage::Asking);
        running.Finished.store(true, std::memory_order_release);

        return;
    }

    running.Current.store(Stage::Unpacking);
    Unpack(running);
}

// Into the staging folder, which takes the version's place once every file is in it. Whatever stops it deletes the staging folder, and puts back a version it was replacing
void HubInstaller::Unpack(Running& running)
{
    const std::string l_Zip = ToUtf8(running.Zip.filename());
    const std::filesystem::path l_Staging = GetSideFolder(running.Destination, c_StagingSuffix);
    const auto l_Abandon = [&running, &l_Staging](std::string message, bool cancelled = false)
    {
        std::error_code l_Error;
        std::filesystem::remove_all(l_Staging, l_Error);
        running.Outcome.Message = std::move(message);
        running.Outcome.Cancelled = cancelled;
        running.Finished.store(true, std::memory_order_release);
    };

    std::error_code l_Error;
    std::filesystem::remove_all(l_Staging, l_Error);
    if (!std::filesystem::create_directories(l_Staging, l_Error) || l_Error)
    {
        l_Abandon(std::format("{} was not installed, as {} could not be made: {}", l_Zip, ToUtf8(l_Staging), l_Error.message()));

        return;
    }

    running.Done.store(0, std::memory_order_relaxed);
    for (const Trinity::ZipEntry& it_Entry : running.Archive.GetEntries())
    {
        if (running.Stop.load(std::memory_order_relaxed))
        {
            l_Abandon(std::format("Installing {} was cancelled, and nothing was installed", l_Zip), true);

            return;
        }

        const std::string_view l_Path = std::string_view(it_Entry.Name).substr(running.Root.size());
        if (l_Path.empty())
        {
            continue;
        }

        const std::filesystem::path l_Target = l_Staging / HubText::FromUtf8(l_Path);
        if (it_Entry.IsDirectory())
        {
            std::filesystem::create_directories(l_Target, l_Error);
            if (l_Error)
            {
                l_Abandon(std::format("{} was not installed, as {} could not be made: {}", l_Zip, ToUtf8(l_Target), l_Error.message()));

                return;
            }

            continue;
        }

        std::filesystem::create_directories(l_Target.parent_path(), l_Error);
        std::ofstream l_File(l_Target, std::ios::binary | std::ios::trunc);
        if (l_Error || !l_File)
        {
            l_Abandon(std::format("{} was not installed, as {} could not be written", l_Zip, ToUtf8(l_Target)));

            return;
        }

        bool l_WriteFailed = false;
        const Trinity::Expected<void, std::string> l_Extracted = running.Archive.Extract(it_Entry, [&running, &l_File, &l_WriteFailed](std::span<const std::byte> piece)
        {
            l_File.write(reinterpret_cast<const char*>(piece.data()), static_cast<std::streamsize>(piece.size()));
            if (!l_File)
            {
                l_WriteFailed = true;

                return false;
            }

            running.Done.fetch_add(piece.size(), std::memory_order_relaxed);

            return !running.Stop.load(std::memory_order_relaxed);
        });

        l_File.close();
        if (running.Stop.load(std::memory_order_relaxed))
        {
            l_Abandon(std::format("Installing {} was cancelled, and nothing was installed", l_Zip), true);

            return;
        }

        if (l_WriteFailed || l_File.fail())
        {
            l_Abandon(std::format("{} was not installed, as {} could not be written, perhaps for want of space", l_Zip, ToUtf8(l_Target)));

            return;
        }

        if (!l_Extracted)
        {
            l_Abandon(std::format("{} was not installed, as {}", l_Zip, l_Extracted.GetError()));

            return;
        }

#if !defined(TR_PLATFORM_WINDOWS)
        // As zipped, so the programs can run
        if ((it_Entry.UnixMode & 0777u) != 0)
        {
            std::filesystem::permissions(l_Target, static_cast<std::filesystem::perms>(it_Entry.UnixMode & 0777u), std::filesystem::perm_options::replace, l_Error);
        }
#endif
    }

    if (running.Replacing)
    {
        const std::filesystem::path l_Replaced = GetSideFolder(running.Destination, c_ReplacedSuffix);
        std::filesystem::remove_all(l_Replaced, l_Error);
        std::filesystem::rename(running.Destination, l_Replaced, l_Error);
        if (l_Error)
        {
            l_Abandon(std::format("{} was not installed, as {} in {} could not be moved aside to be replaced, perhaps as a program runs from it: {}", l_Zip, HubInstalls::Describe(running.Manifest), ToUtf8(running.Destination), l_Error.message()));

            return;
        }

        std::filesystem::rename(l_Staging, running.Destination, l_Error);
        if (l_Error)
        {
            std::error_code l_Back;
            std::filesystem::rename(l_Replaced, running.Destination, l_Back);
            l_Abandon(std::format("{} was not installed, as it could not take the place of the version in {}, which is kept: {}", l_Zip, ToUtf8(running.Destination), l_Error.message()));

            return;
        }

        std::filesystem::remove_all(l_Replaced, l_Error);
    }
    else
    {
        std::filesystem::rename(l_Staging, running.Destination, l_Error);
        if (l_Error)
        {
            l_Abandon(std::format("{} was not installed, as it could not be put in {}: {}", l_Zip, ToUtf8(running.Destination), l_Error.message()));

            return;
        }
    }

    running.Outcome.Succeeded = true;
    running.Outcome.Directory = running.Destination;
    running.Outcome.Manifest = running.Manifest;
    running.Outcome.Message = std::format("{} {} in {}", running.Replacing ? "Replaced" : "Installed", HubInstalls::Describe(running.Manifest), ToUtf8(running.Destination));
    running.Finished.store(true, std::memory_order_release);
}

// Moved aside first, which fails at once while a program runs from it, so a version is deleted whole or not at all
void HubInstaller::Remove(Running& running)
{
    const std::string l_Version = HubInstalls::Describe(running.Manifest);
    const std::string l_Directory = ToUtf8(running.Destination);
    const auto l_Finish = [&running](bool deleted, std::string message)
    {
        running.Outcome.Succeeded = deleted;
        running.Outcome.Deleted = deleted;
        running.Outcome.Directory = running.Destination;
        running.Outcome.Manifest = running.Manifest;
        running.Outcome.Message = std::move(message);
        running.Finished.store(true, std::memory_order_release);
    };

    std::error_code l_Error;
    if (!std::filesystem::exists(running.Destination, l_Error))
    {
        l_Finish(true, std::format("Took {} off the list, whose folder {} was gone already", l_Version, l_Directory));

        return;
    }

    if (IsInside(Trinity::Process::GetExecutableDirectory(), running.Destination))
    {
        l_Finish(false, std::format("{} in {} was not deleted, as Hub runs from it and cannot delete it while it is open", l_Version, l_Directory));

        return;
    }

    const std::filesystem::path l_Aside = GetSideFolder(running.Destination, c_DeletingSuffix);
    std::filesystem::remove_all(l_Aside, l_Error);
    std::filesystem::rename(running.Destination, l_Aside, l_Error);
    if (l_Error)
    {
        l_Finish(false, std::format("{} was not deleted, as {} could not be moved, perhaps as a program runs from it: {}", l_Version, l_Directory, l_Error.message()));

        return;
    }

    std::filesystem::remove_all(l_Aside, l_Error);
    if (l_Error)
    {
        l_Finish(true, std::format("Deleted {} from {}, though some of its files are left in {}: {}", l_Version, l_Directory, ToUtf8(l_Aside), l_Error.message()));

        return;
    }

    l_Finish(true, std::format("Deleted {} and its files in {}", l_Version, l_Directory));
}
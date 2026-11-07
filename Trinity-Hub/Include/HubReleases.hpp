#pragma once

#include <Trinity.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// A release of Trinity on GitHub that holds a zip for this platform, as Package.cmake names one
struct HubRelease
{
    // As the zip names it, which is the version its manifest gives
    std::string Version;
    std::string Tag;
    std::string Name;
    // In UTC, as 2026-11-07T02:30:00Z
    std::string PublishedAt;
    // The release's page on GitHub, with its notes
    std::string Page;
    // Marked so on GitHub, or a version that carries a hyphen
    bool Prerelease = false;
    std::string ZipName;
    std::string ZipUrl;
    std::uint64_t ZipSize = 0;
    // The .sha256 uploaded beside the zip, or empty
    std::string Sha256Url;
    // GitHub's own SHA-256 of the zip, in lowercase, or empty where it gives none
    std::string Digest;

    // A download is checked against the .sha256, or else against GitHub's digest, and one with neither is not offered
    [[nodiscard]] bool CanCheck() const { return !Sha256Url.empty() || !Digest.empty(); }
};

// The GitHub Releases of Trinity, through GitHub's REST API without a token, which answers 60 requests an hour from one address, so Hub asks at most once a minute, and asks again with the list's ETag, which GitHub answers with no list, and does not count, when nothing has changed. The last list is kept in Releases.yaml, so a Hub that cannot reach GitHub still shows it
class HubReleases
{
public:
    static constexpr std::uint32_t c_FormatVersion = 1;
    static constexpr std::string_view c_Path = "/saves/Releases.yaml";
    static constexpr std::string_view c_Repository = "ThatTanishqTak/Trinity";
    static constexpr std::chrono::seconds c_Interval{ 60 };

    // url is GitHub's list of the repository's releases, or a stand-in serving GitHub's replies, as --releases-url names one
    explicit HubReleases(std::string url);
    ~HubReleases();

    HubReleases(const HubReleases&) = delete;
    HubReleases& operator=(const HubReleases&) = delete;

    [[nodiscard]] static std::string GetDefaultUrl();
    // As GitHub requires of every request, naming Hub and its version
    [[nodiscard]] static std::string GetUserAgent();
    // The releases in GitHub's JSON with a zip for this platform and architecture, newest first, and how many have none
    [[nodiscard]] static Trinity::Expected<std::vector<HubRelease>, std::string> Parse(std::string_view json, std::string_view platform, std::string_view architecture, std::size_t* others = nullptr);

    // The list kept from the last time, when it was got from the same address
    void Load();
    // Asks GitHub on a job, unless it is asking or asked within the last minute
    void Refresh();
    // Once a frame, taking the answer
    void Update();

    [[nodiscard]] bool IsFetching() const { return m_Fetch != nullptr; }
    // How long until Refresh asks again, 0 when it would now
    [[nodiscard]] std::chrono::seconds GetWait() const;
    [[nodiscard]] const std::vector<HubRelease>& GetReleases() const { return m_Releases; }
    // Releases with no zip for this platform, which are not listed
    [[nodiscard]] std::size_t GetOtherReleases() const { return m_Others; }
    // When GitHub last gave the list, as 2026-11-07T05:41:29Z, or empty when it never has
    [[nodiscard]] const std::string& GetFetchedAt() const { return m_FetchedAt; }
    // Why the last ask did not get the list, in one line, or empty when it did
    [[nodiscard]] const std::string& GetNote() const { return m_Note; }

private:
    struct Fetch
    {
        enum class Outcome : std::uint8_t
        {
            Listed,
            Unchanged,
            Refused
        };

        std::string Url;
        std::string ETag;
        std::atomic<bool> Stop = false;
        std::atomic<bool> Done = false;

        // Written by the job before Done
        Outcome Result = Outcome::Refused;
        std::vector<HubRelease> Releases;
        std::size_t Others = 0;
        std::string NewETag;
        std::string Note;
    };

    static void Run(Fetch& fetch);
    void Save() const;

    std::string m_Url;
    std::vector<HubRelease> m_Releases;
    std::size_t m_Others = 0;
    std::string m_ETag;
    std::string m_FetchedAt;
    std::string m_Note;
    std::optional<std::chrono::system_clock::time_point> m_LastAsked;

    std::unique_ptr<Fetch> m_Fetch;
    Trinity::JobCounter m_Jobs;
};
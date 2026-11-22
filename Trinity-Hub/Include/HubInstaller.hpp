#pragma once

#include "HubReleases.hpp"

#include <Trinity.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// On a job, one at a time. Unpacked to a staging folder, renamed once whole, so failure leaves none
class HubInstaller
{
public:
    enum class Stage : std::uint8_t
    {
        Idle,
        Downloading,
        Checking,
        Reading,
        // A version is installed in the folder already, and the person is asked whether to replace it
        Asking,
        Unpacking,
        Deleting
    };

    struct Result
    {
        bool Succeeded = false;
        bool Cancelled = false;
        bool Deleted = false;
        // What happened, in one line, naming why when it failed
        std::string Message;
        // It failed as the zip's manifest is of a format newer than this Hub reads
        bool NeedsNewerHub = false;
        std::filesystem::path Directory;
        Trinity::EngineManifest Manifest;
    };

    HubInstaller() = default;
    ~HubInstaller();

    HubInstaller(const HubInstaller&) = delete;
    HubInstaller& operator=(const HubInstaller&) = delete;

    // installed names the folders of versions installed already, so one is replaced where it is
    void Install(std::filesystem::path zip, std::filesystem::path installsDirectory, std::vector<std::pair<std::string, std::filesystem::path>> installed);
    // Into a folder beside the versions, deleted once the install is done, then installed as a zip
    void Download(const HubRelease& release, std::filesystem::path installsDirectory, std::vector<std::pair<std::string, std::filesystem::path>> installed);
    // While Asking: unpacks over the version installed, or leaves it
    void Replace();
    void KeepInstalled();
    void Delete(std::filesystem::path directory, Trinity::EngineManifest manifest);
    // Lets the file being unpacked finish, then leaves nothing behind
    void Cancel();

    // Once a frame. The result of what finished, once
    [[nodiscard]] std::optional<Result> Update();

    [[nodiscard]] bool IsBusy() const { return m_Running != nullptr; }
    [[nodiscard]] Stage GetStage() const;
    // From 0 to 1, or below 0 while there is no measure of it
    [[nodiscard]] float GetProgress() const;
    // What is being done, in one line
    [[nodiscard]] std::string Describe() const;

    // What a cut-short install or delete left: a download or staging folder, or a version moved aside
    static void CleanUp(const std::filesystem::path& installsDirectory);

private:
    struct Running
    {
        std::filesystem::path Zip;
        std::optional<HubRelease> Release;
        std::filesystem::path DownloadFolder;
        // Checked against its hash already, as it downloaded
        bool Verified = false;
        std::filesystem::path InstallsDirectory;
        std::vector<std::pair<std::string, std::filesystem::path>> Installed;
        std::filesystem::path Destination;
        Trinity::ZipArchive Archive;
        // What the zip's paths begin with above the version's own files, its top folder and a slash
        std::string Root;
        Trinity::EngineManifest Manifest;
        bool Replacing = false;

        std::atomic<Stage> Current = Stage::Checking;
        std::atomic<std::uint64_t> Done = 0;
        std::atomic<std::uint64_t> Total = 0;
        std::atomic<bool> Stop = false;
        std::atomic<bool> Finished = false;
        // Written by the job before Finished
        Result Outcome;
    };

    static void Receive(Running& running);
    static void Prepare(Running& running);
    static void Unpack(Running& running);
    static void Remove(Running& running);
    void Submit(void (*work)(Running&));

    std::unique_ptr<Running> m_Running;
    Trinity::JobCounter m_Jobs;
};
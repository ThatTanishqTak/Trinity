#pragma once

#include <Trinity.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Installs a version from its zip, or deletes an installed one's files, on a job, one at a time. An install checks the zip against the .sha256 beside it, when there is one, reads its manifest and checks every file it holds before it unpacks anything, then unpacks it into a staging folder beside the version's, which is renamed to the version's only once every file is there, so a failed or cancelled install leaves nothing behind, and one over a version already installed keeps that version until the new one is whole
class HubInstaller
{
public:
    enum class Stage : std::uint8_t
    {
        Idle,
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
        std::filesystem::path Directory;
        Trinity::EngineManifest Manifest;
    };

    HubInstaller() = default;
    ~HubInstaller();

    HubInstaller(const HubInstaller&) = delete;
    HubInstaller& operator=(const HubInstaller&) = delete;

    // installed names, as Trinity-<version>-<platform>-<architecture>, the folders of the versions installed already, so a version installed elsewhere is replaced where it is
    void Install(std::filesystem::path zip, std::filesystem::path installsDirectory, std::vector<std::pair<std::string, std::filesystem::path>> installed);
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

    // What an install or delete cut short by Hub ending left in the folder: a staging folder, which is deleted, and a version moved aside to be replaced, which is put back when its replacement never took its place, and otherwise deleted
    static void CleanUp(const std::filesystem::path& installsDirectory);

private:
    struct Running
    {
        std::filesystem::path Zip;
        std::filesystem::path InstallsDirectory;
        std::vector<std::pair<std::string, std::filesystem::path>> Installed;
        std::filesystem::path Destination;
        Trinity::ZipArchive Archive;
        // What the zip's paths begin with above the version's own files, its top folder and a slash, or nothing
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

    static void Prepare(Running& running);
    static void Unpack(Running& running);
    static void Remove(Running& running);
    void Submit(void (*work)(Running&));

    std::unique_ptr<Running> m_Running;
    Trinity::JobCounter m_Jobs;
};
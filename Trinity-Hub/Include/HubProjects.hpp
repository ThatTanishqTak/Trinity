#pragma once

#include <Trinity.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// A project of the list, with what its file says now
struct HubProject
{
    Trinity::ProjectListEntry Entry;
    // Read from its file as the list was, without opening the project
    std::optional<Trinity::ProjectFileInfo> Info;
    // Its file is gone, moved or deleted
    bool Missing = false;
    // Why its file could not be read, though it is there
    std::string Problem;
    // Its name and folder in lowercase, which the search looks in
    std::string SearchText;

    [[nodiscard]] const std::string& GetName() const { return Info ? Info->Name : Entry.Name; }
    [[nodiscard]] const std::string& GetEngineVersion() const { return Info ? Info->EngineVersion : Entry.EngineVersion; }
};

// Hub's view of the projects list, Projects.yaml in its folder, read on a job with each project's file, which never mounts a project. While the Projects page is shown, the list's file is looked at once a second and read again once it changes, as when a Forge opens a project
class HubProjects
{
public:
    explicit HubProjects(std::filesystem::path file);
    ~HubProjects();

    HubProjects(const HubProjects&) = delete;
    HubProjects& operator=(const HubProjects&) = delete;

    // The list and each project's file read again, on a job
    void Refresh();
    // Once a frame, taking what the job read
    void Update();
    // Once a frame while the list is shown
    void Watch();

    [[nodiscard]] bool IsRefreshing() const { return m_Reading != nullptr; }
    [[nodiscard]] const std::vector<HubProject>& GetProjects() const { return m_Projects; }
    // Counted up each time the list is read again, so what is searched and sorted from it is known to be out of date
    [[nodiscard]] std::uint64_t GetGeneration() const { return m_Generation; }
    // Why the list could not be read, or empty when it was
    [[nodiscard]] const std::string& GetProblem() const { return m_Problem; }
    [[nodiscard]] const std::filesystem::path& GetFile() const { return m_File; }

private:
    struct Reading
    {
        std::filesystem::path File;
        std::atomic<bool> Stop = false;
        std::atomic<bool> Done = false;

        // Written by the job before Done
        std::optional<std::filesystem::file_time_type> Time;
        std::vector<HubProject> Projects;
        std::string Problem;
    };

    static void Read(Reading& reading);

    std::filesystem::path m_File;
    std::vector<HubProject> m_Projects;
    std::uint64_t m_Generation = 0;
    std::string m_Problem;

    // When the list's file was written, as the last read found it
    std::optional<std::filesystem::file_time_type> m_ReadTime;
    std::chrono::steady_clock::time_point m_NextLook;
    bool m_ReadAgain = false;

    std::unique_ptr<Reading> m_Reading;
    Trinity::JobCounter m_Jobs;
};
#pragma once

#include "Importers/EnvironmentImporter.hpp"

#include <Trinity.hpp>

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

// Environments are filtered on the job system one at a time, since the filter already uses every core, beside the texture and model imports
class EnvironmentImportBatch
{
public:
    EnvironmentImportBatch() = default;
    ~EnvironmentImportBatch();

    EnvironmentImportBatch(const EnvironmentImportBatch&) = delete;
    EnvironmentImportBatch& operator=(const EnvironmentImportBatch&) = delete;

    void Start(std::vector<Trinity::AssetRecord> records);
    [[nodiscard]] std::optional<EnvironmentImportReport> Update();
    [[nodiscard]] EnvironmentImportReport Wait();
    void Stop();

    [[nodiscard]] bool IsRunning() const { return m_Running != nullptr; }

private:
    struct Finished
    {
        Trinity::UUID ID;
        EnvironmentImporter::Result Result = EnvironmentImporter::Result::Failed;
    };

    // Written by the job under Mutex, apart from the flags
    struct Running
    {
        std::vector<Trinity::AssetRecord> Records;
        std::atomic<bool> Stop = false;
        std::atomic<bool> Done = false;

        std::mutex Mutex;
        std::vector<Finished> Completed;
        EnvironmentImportReport Report;
    };

    static void Run(Running& running);
    void TakeFinished();

    std::unique_ptr<Running> m_Running;
    std::vector<Trinity::UUID> m_Held;
    Trinity::JobCounter m_Jobs;
};
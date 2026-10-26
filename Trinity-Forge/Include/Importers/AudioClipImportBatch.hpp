#pragma once

#include "Importers/AudioClipImporter.hpp"

#include <Trinity.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

// Clips are checked and cooked on the job system one at a time, beside the texture, model and environment imports
class AudioClipImportBatch
{
public:
    AudioClipImportBatch() = default;
    ~AudioClipImportBatch();

    AudioClipImportBatch(const AudioClipImportBatch&) = delete;
    AudioClipImportBatch& operator=(const AudioClipImportBatch&) = delete;

    void Start(std::vector<Trinity::AssetRecord> records);
    [[nodiscard]] std::optional<AudioClipImportReport> Update();
    [[nodiscard]] AudioClipImportReport Wait();
    void Stop();

    [[nodiscard]] bool IsRunning() const { return m_Running != nullptr; }
    // Clips cooked again since Forge started, counted as each is loaded again, so what reads cooked headers knows to read them again
    [[nodiscard]] std::uint64_t GetImportedCount() const { return m_ImportedCount; }

private:
    struct Finished
    {
        Trinity::UUID ID;
        AudioClipImporter::Result Result = AudioClipImporter::Result::Failed;
    };

    // Written by the job under Mutex, apart from the flags
    struct Running
    {
        std::vector<Trinity::AssetRecord> Records;
        std::atomic<bool> Stop = false;
        std::atomic<bool> Done = false;

        std::mutex Mutex;
        std::vector<Finished> Completed;
        AudioClipImportReport Report;
    };

    static void Run(Running& running);
    void TakeFinished();

    std::unique_ptr<Running> m_Running;
    std::vector<Trinity::UUID> m_Held;
    std::uint64_t m_ImportedCount = 0;
    Trinity::JobCounter m_Jobs;
};
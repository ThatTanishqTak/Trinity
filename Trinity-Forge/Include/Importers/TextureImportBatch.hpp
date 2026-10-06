#pragma once

#include "Importers/TextureImporter.hpp"

#include <Trinity.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

class TextureImportBatch
{
public:
    // How far the batch has got. Texels count every mip level, so the progress follows the work and not the number of files
    struct Progress
    {
        std::size_t Textures = 0;
        bool Planned = false;
        std::size_t ToEncode = 0;
        std::size_t Encoding = 0;
        std::string Path;
        std::uint32_t Width = 0;
        std::uint32_t Height = 0;
        std::uint64_t DoneTexels = 0;
        std::uint64_t TotalTexels = 0;
        std::uint64_t CurrentTexels = 0;
        double CurrentSeconds = 0.0;
        double ElapsedSeconds = 0.0;
        std::optional<double> TexelsPerSecond;
        bool Stopping = false;
    };

    TextureImportBatch() = default;
    ~TextureImportBatch();

    TextureImportBatch(const TextureImportBatch&) = delete;
    TextureImportBatch& operator=(const TextureImportBatch&) = delete;

    void Start(std::vector<Trinity::AssetRecord> records);
    [[nodiscard]] std::optional<TextureImportReport> Update();
    [[nodiscard]] TextureImportReport Wait();
    void Stop();

    [[nodiscard]] bool IsRunning() const { return m_Running != nullptr; }
    [[nodiscard]] Progress GetProgress() const;

private:
    using Clock = std::chrono::steady_clock;

    struct Finished
    {
        Trinity::UUID ID;
        TextureImporter::Result Result = TextureImporter::Result::Failed;
    };

    // Written by the job under Mutex, apart from the flags
    struct Running
    {
        std::vector<Trinity::AssetRecord> Records;
        std::atomic<bool> Stop = false;
        std::atomic<bool> Done = false;

        mutable std::mutex Mutex;
        std::vector<Finished> Completed;
        TextureImportReport Report;
        bool Planned = false;
        std::size_t ToEncode = 0;
        std::size_t Encoding = 0;
        std::string Path;
        std::uint32_t Width = 0;
        std::uint32_t Height = 0;
        std::uint64_t DoneTexels = 0;
        std::uint64_t TotalTexels = 0;
        std::uint64_t CurrentTexels = 0;
        Clock::time_point CurrentStart;
        std::uint64_t EncodedTexels = 0;
        double EncodeSeconds = 0.0;
    };

    static void Run(Running& running);
    void TakeFinished();

    std::unique_ptr<Running> m_Running;
    std::vector<Trinity::UUID> m_Held;
    Clock::time_point m_Start;
    std::optional<double> m_TexelsPerSecond;
    Trinity::JobCounter m_Jobs;
};
#pragma once

#include "Importers/TextureImporter.hpp"

#include <Trinity.hpp>

#include <atomic>
#include <deque>
#include <memory>

class TextureReimporter
{
public:
    TextureReimporter() = default;
    ~TextureReimporter();

    TextureReimporter(const TextureReimporter&) = delete;
    TextureReimporter& operator=(const TextureReimporter&) = delete;

    void Queue(const Trinity::AssetRecord& record);
    void Update();
    void Finish();

    [[nodiscard]] bool IsBusy(Trinity::UUID id) const;

private:
    struct Running
    {
        Trinity::AssetRecord Record;
        std::atomic<bool> Finished = false;
        TextureImporter::Result Result = TextureImporter::Result::Failed;
    };

    void StartNext();

    std::unique_ptr<Running> m_Running;
    std::deque<Trinity::AssetRecord> m_Queued;
    Trinity::JobCounter m_Jobs;
};
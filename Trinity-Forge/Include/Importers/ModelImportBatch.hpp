#pragma once

#include "Importers/ModelImporter.hpp"

#include <Trinity.hpp>

#include <atomic>
#include <memory>
#include <optional>
#include <vector>

// Models are planned on a worker, given sub-asset UUIDs on the main thread, then cooked on a worker
class ModelImportBatch
{
public:
    ModelImportBatch() = default;
    ~ModelImportBatch();

    ModelImportBatch(const ModelImportBatch&) = delete;
    ModelImportBatch& operator=(const ModelImportBatch&) = delete;

    void Start(Trinity::AssetRegistry& registry, std::vector<Trinity::AssetRecord> records);
    [[nodiscard]] std::optional<ModelImportReport> Update();
    [[nodiscard]] ModelImportReport Wait();
    void Stop();

    [[nodiscard]] bool IsRunning() const { return m_Running != nullptr; }
    // Until the plans are taken up, after which every texture beside a model has its encode settings
    [[nodiscard]] bool IsPlanning() const { return m_Running != nullptr && !m_Running->Planned; }

private:
    struct ToCook
    {
        Trinity::AssetRecord Record;
        ModelImporter::Plan Plan;
        std::vector<Trinity::UUID> ExternalTextures;
        ModelImporter::Cooked Cooked;
    };

    // The job writes Plans, then Cooking's results, each before Done, and the main thread reads after
    struct Running
    {
        std::vector<Trinity::AssetRecord> Records;
        std::vector<ModelImporter::Plan> Plans;
        std::vector<ToCook> Cooking;
        std::atomic<bool> Stop = false;
        std::atomic<bool> Done = false;
        bool Planned = false;
        ModelImportReport Report;
    };

    static void RunPlans(Running& running);
    static void RunCooks(Running& running);
    void TakePlans();
    void TakeCooked();

    Trinity::AssetRegistry* m_Registry = nullptr;
    std::unique_ptr<Running> m_Running;
    std::vector<Trinity::UUID> m_Held;
    Trinity::JobCounter m_Jobs;
};
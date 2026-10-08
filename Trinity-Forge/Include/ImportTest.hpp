#pragma once

#include "Importers/ModelImporter.hpp"

#include <Trinity.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

class EditorSession;

class ImportTest
{
public:
    explicit ImportTest(EditorSession& session) : m_Session(session)
    {

    }

    ~ImportTest();

    ImportTest(const ImportTest&) = delete;
    ImportTest& operator=(const ImportTest&) = delete;

    void Start(const std::filesystem::path& directory);
    void Update();
    void OnBuildFrameGraph(Trinity::FrameGraph& graph);

private:
    enum class Phase : std::uint8_t
    {
        Idle,
        LoadingBC7,
        LoadingRGBA8,
        LoadingMaterials,
        ReadingMaterials,
        EditingMaterial,
        ReadingEdit,
        ReadingUndo,
        LoadingStandalone
    };

    [[nodiscard]] bool OpenProject(const std::filesystem::path& directory);
    [[nodiscard]] std::size_t CopyTestImages();
    [[nodiscard]] std::size_t CopyTestModels();
    [[nodiscard]] bool CheckModels();
    [[nodiscard]] bool ReimportModels();
    [[nodiscard]] bool CheckInstances();
    [[nodiscard]] bool CompareFormats();
    void CheckQuality();
    void BeginLoads(Phase phase);
    void FinishLoads();

    [[nodiscard]] bool CheckMaterialMapping();
    [[nodiscard]] bool CheckMaterialFiles();
    void BeginMaterials();
    void UpdateMaterials();
    void EditMaterial();
    void FinishEdit();
    void FinishUndo();
    void BeginStandalone();
    void FinishStandalone();
    void FinishMaterials(bool passed);
    // The materials' records as the table should hold them, compared with the table's own copy and, when just read back, the GPU's. Empty when all match
    [[nodiscard]] std::string CheckRecords(std::span<const Trinity::UUID> materials, bool readBack) const;
    [[nodiscard]] bool AreTexturesReady(const Trinity::MaterialData& material) const;

    EditorSession& m_Session;
    Phase m_Phase = Phase::Idle;
    std::vector<Trinity::AssetRef<Trinity::TextureAsset>> m_Textures;
    // Every texture the test models' materials use, and how
    std::unordered_map<Trinity::UUID, ModelImporter::TextureUsage> m_ModelTextures;
    std::uint64_t m_PhaseFrames = 0;
    std::uint64_t m_ReadyFrames = 0;

    // Every material of the glTF test models, and the one edited a thousand frames over
    std::vector<Trinity::AssetRef<Trinity::MaterialAsset>> m_Materials;
    std::vector<Trinity::UUID> m_MaterialIDs;
    Trinity::UUID m_Edited;
    Trinity::MaterialData m_EditBefore;
    Trinity::MaterialData m_EditLast;
    std::string m_EditBeforeCooked;
    std::string m_EditBeforeMeta;
    std::size_t m_EditHistory = 0;
    std::uint64_t m_EditFrames = 0;
    std::uint64_t m_EditMemory = 0;
    std::uint64_t m_EditLate = 0;
    Trinity::AssetRef<Trinity::MaterialAsset> m_Standalone;
    std::string m_StandalonePath;
    Trinity::MaterialData m_StandaloneData;
    bool m_MaterialsPassed = true;

    // The material table copied into a readback buffer by a pass of the frame after one is asked for, and read once that frame has finished
    Trinity::RHI::BufferHandle m_TableReadback;
    std::uint64_t m_TableReadbackSize = 0;
    bool m_ReadbackWanted = false;
    bool m_ReadbackAdded = false;
};
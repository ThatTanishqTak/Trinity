#pragma once

#include "Importers/ModelImporter.hpp"

#include <Trinity.hpp>

#include <cstdint>
#include <filesystem>
#include <unordered_map>
#include <vector>

class EditorSession;

class ImportTest
{
public:
    explicit ImportTest(EditorSession& session) : m_Session(session)
    {

    }

    void Start(const std::filesystem::path& directory);
    void Update();

private:
    enum class Phase : std::uint8_t
    {
        Idle,
        LoadingBC7,
        LoadingRGBA8
    };

    [[nodiscard]] bool OpenProject(const std::filesystem::path& directory);
    [[nodiscard]] std::size_t CopyTestImages();
    [[nodiscard]] std::size_t CopyTestModels();
    [[nodiscard]] bool CheckModels();
    [[nodiscard]] bool ReimportModels();
    [[nodiscard]] bool CheckInstances();
    void CheckQuality();
    void BeginLoads(Phase phase);
    void FinishLoads();

    EditorSession& m_Session;
    Phase m_Phase = Phase::Idle;
    std::vector<Trinity::AssetRef<Trinity::TextureAsset>> m_Textures;
    // Every texture the test models' materials use, and how
    std::unordered_map<Trinity::UUID, ModelImporter::TextureUsage> m_ModelTextures;
    std::uint64_t m_PhaseFrames = 0;
    std::uint64_t m_ReadyFrames = 0;
};
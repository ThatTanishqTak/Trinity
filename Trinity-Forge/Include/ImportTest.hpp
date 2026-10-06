#pragma once

#include <Trinity.hpp>

#include <cstdint>
#include <filesystem>
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
    void CheckQuality();
    void BeginLoads(Phase phase);
    void FinishLoads();

    EditorSession& m_Session;
    Phase m_Phase = Phase::Idle;
    std::vector<Trinity::AssetRef<Trinity::TextureAsset>> m_Textures;
    std::uint64_t m_PhaseFrames = 0;
    std::uint64_t m_ReadyFrames = 0;
};
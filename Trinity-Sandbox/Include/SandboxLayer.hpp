#pragma once

#include <Trinity.hpp>

#include <cstddef>
#include <cstdint>
#include <random>
#include <string_view>
#include <vector>

class SandboxLayer final : public Trinity::Layer
{
public:
    SandboxLayer();

    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate(Trinity::Timestep timestep) override;
    void OnEvent(Trinity::Event& event) override;
    void OnRender(Trinity::RHI::CommandList& commands) override;
    void OnBuildFrameGraph(Trinity::FrameGraph& graph, Trinity::FrameGraphTexture sceneColor) override;

private:
    enum class SpritePhase : std::uint8_t
    {
        Idle,
        Loading,
        Reading,
        Running
    };

    // The meshes load and read back first, then a thousand loads and releases run, and the sprites start once Assets is empty again
    enum class MeshPhase : std::uint8_t
    {
        Idle,
        Loading,
        Reading,
        Churning,
        Settling,
        Done
    };

    struct TestMesh
    {
        std::string_view Name;
        Trinity::UUID ID;
        Trinity::MeshData Data;
        std::vector<std::byte> Cooked;
    };

    struct ChurnRef
    {
        Trinity::AssetRef<Trinity::MeshAsset> Ref;
        std::uint64_t ReleaseFrame = 0;
    };

    bool OnKeyPressed(Trinity::KeyPressedEvent& event);
    void TestCompute();
    void TestLayeredTextures();
    void TestRasterState();
    void TestMultisampling();
    void TestTimestamps();
    void TestFrameGraph();
    void TestToneMapping();
    void CheckRendererGraph();
    void CheckGpuTimes();
    void UpdateResizes();
    void TestMeshRendererScene();
    void CreateTestAssets();
    [[nodiscard]] bool AddTestMeshes(Trinity::MemorySource& cache);
    void UpdateMeshes();
    void AddMeshReadbacks(Trinity::FrameGraph& graph);
    void CheckMeshReadbacks();
    void UpdateMeshChurn(std::uint64_t frame);
    void FinishMeshes();
    void UpdateSprites();
    void AddSpriteReadback(Trinity::FrameGraph& graph);
    void CheckSpriteReadback();
    void ReportSprites();
    void DestroyTestAssets();

    Trinity::Scope<Trinity::AssetRegistry> m_SpriteRegistry;
    Trinity::Scope<Trinity::Scene> m_SpriteScene;
    Trinity::Scope<Trinity::Scene> m_SpriteReadbackScene;
    std::vector<Trinity::UUID> m_SpriteTextures;
    Trinity::UUID m_GreyTexture;
    SpritePhase m_SpritePhase = SpritePhase::Idle;
    std::uint64_t m_SpritePhaseFrame = 0;
    Trinity::RHI::BufferHandle m_SpriteReadback;
    Trinity::Renderer2D::Statistics m_SpriteReadbackStatistics;
    bool m_SpriteReadbackAdded = false;
    bool m_SpriteReadbackDrawn = false;
    std::uint64_t m_SpriteFrames = 0;
    std::uint64_t m_SpriteBytesAtCheck = 0;
    std::uint64_t m_SpriteBytesLast = 0;
    std::uint64_t m_SpriteBadFrames = 0;
    bool m_SpriteReported = false;
    bool m_SpritesReady = false;

    std::vector<TestMesh> m_TestMeshes;
    std::vector<Trinity::AssetRef<Trinity::MeshAsset>> m_MeshRefs;
    std::vector<Trinity::RHI::BufferHandle> m_MeshReadbacks;
    std::vector<ChurnRef> m_MeshChurn;
    std::mt19937 m_MeshRandom;
    MeshPhase m_MeshPhase = MeshPhase::Idle;
    std::uint64_t m_MeshPhaseFrame = 0;
    std::uint32_t m_MeshChurnLoads = 0;
    std::uint32_t m_MeshChurnFinished = 0;
    bool m_MeshReadbackAdded = false;
    bool m_RHITested = false;
    bool m_GraphChecked = false;
    bool m_GpuTimesChecked = false;

    std::uint32_t m_Resizes = 0;
    std::uint32_t m_ResizeWidth = 0;
    std::uint32_t m_ResizeHeight = 0;
    std::uint64_t m_ResizeBytesMiddle = 0;

    float m_ClearHue = 0.0f;
    float m_SecondsSinceReport = 0.0f;
    std::uint32_t m_FramesSinceReport = 0;
};
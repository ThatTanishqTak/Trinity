#pragma once

#include <Trinity.hpp>

#include <atomic>
#include <random>
#include <unordered_map>
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

private:
    bool OnKeyPressed(Trinity::KeyPressedEvent& event);
    void TestUUIDs();
    void TestJobs();
    void TestParallelFor();
    void TestLogHistory();
    void TestFileSystem();
    void TestSaves();
    void TestScene();
    void TestHierarchy();
    void TestSceneFiles();
    void TestAssetRegistry();
    void UpdateAssetLoads();
    void TestModules();
    void TestShaders();
    void TestRHI();
    void TestResources();
    void TestCompressedTextures();
    void TestStaleHandle();
    void CreateTriangle();
    void DestroyTriangle();
    void CreateCheckerboard();
    void DestroyCheckerboard();
    void CreateQuadField();
    void DestroyQuadField();
    void DrawQuadField(Trinity::RHI::CommandList& commands);
    void StartAsyncReads();
    void CheckAsyncReads();
    void OpenSecondWindow();
    void UpdateSecondWindow();
    void CloseSecondWindow();
    void OnSecondWindowEvent(Trinity::Event& event);

    void* m_ScratchBuffer = nullptr;
    std::vector<std::uint32_t> m_Probe;

    std::vector<Trinity::FileRequest> m_AsyncReads;
    Trinity::FileRequest m_CancelledRead;
    std::uint64_t m_AsyncStartFrame = 0;
    std::uint32_t m_AsyncCompleted = 0;
    std::uint32_t m_AsyncMismatches = 0;
    std::uint32_t m_AsyncOffMainThread = 0;
    std::uint32_t m_CancelledCallbacks = 0;
    bool m_AsyncReported = false;

    struct HeldAsset
    {
        std::uint64_t Frame = 0;
        bool Cancel = false;
        Trinity::AssetRef<Trinity::BinaryAsset> Reference;
    };

    Trinity::Scope<Trinity::AssetRegistry> m_AssetRegistry;
    std::vector<HeldAsset> m_AssetRefs;
    std::vector<Trinity::UUID> m_AssetIDs;
    std::unordered_map<Trinity::UUID, std::uint32_t> m_AssetFileIndices;
    std::mt19937 m_AssetRandom;
    Trinity::MemoryTagStats m_AssetsBefore;
    std::uint64_t m_AssetPhaseFrame = 0;
    std::uint32_t m_AssetLoadsStarted = 0;
    std::uint32_t m_AssetVerified = 0;
    std::uint32_t m_AssetReleasedWhileLoading = 0;
    std::uint32_t m_AssetMismatches = 0;
    std::uint32_t m_AssetFailures = 0;
    bool m_AssetLoadsActive = false;

    std::atomic<std::uint32_t> m_FrameJobsRun{ 0 };
    std::atomic<std::uint32_t> m_FrameJobMismatches{ 0 };

    Trinity::RHI::PipelineHandle m_TrianglePipeline;
    Trinity::RHI::BufferHandle m_TriangleVertices;
    std::uint32_t m_TriangleVertexIndex = Trinity::RHI::c_NoBindlessIndex;

    Trinity::RHI::PipelineHandle m_QuadPipeline;
    Trinity::RHI::TextureHandle m_Checkerboard;
    Trinity::RHI::SamplerHandle m_CheckerboardSampler;
    std::uint32_t m_CheckerboardIndex = Trinity::RHI::c_NoBindlessIndex;
    std::uint32_t m_CheckerboardSamplerIndex = Trinity::RHI::c_NoBindlessIndex;

    Trinity::RHI::PipelineHandle m_FieldPipeline;
    float m_FieldSeconds = 0.0f;
    std::uint64_t m_FieldFrames = 0;
    std::uint64_t m_FieldFailedFrames = 0;
    std::uint64_t m_RendererBytesAtCheck = 0;
    std::uint64_t m_RendererBytesLast = 0;
    std::uint32_t m_UploadOverflowRequests = 0;
    bool m_OverflowUploadNextFrame = false;

    Trinity::Scope<Trinity::Window> m_SecondWindow;
    std::uint32_t m_SecondOutput = 0;
    std::uint32_t m_SecondWindowFrames = 0;
    std::uint32_t m_SecondWindowDraws = 0;
    std::uint32_t m_SecondWindowFailures = 0;
    std::uint32_t m_SecondDrawWidth = 0;
    std::uint32_t m_SecondDrawHeight = 0;
    Trinity::WindowPosition m_SecondTargetPosition;
    Trinity::WindowPosition m_SecondMovedTo;
    std::uint32_t m_SecondResizedWidth = 0;
    std::uint32_t m_SecondResizedHeight = 0;
    bool m_SecondWindowPending = false;
    bool m_SecondWindowClosing = false;

    float m_ClearHue = 0.0f;
    float m_SecondsSinceReport = 0.0f;
    std::uint32_t m_FramesSinceReport = 0;
};
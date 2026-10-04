#pragma once

#include <Trinity.hpp>

#include <atomic>
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
    void TestFileSystem();
    void TestSaves();
    void TestModules();
    void TestShaders();
    void TestRHI();
    void TestResources();
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

    float m_ClearHue = 0.0f;
    float m_SecondsSinceReport = 0.0f;
    std::uint32_t m_FramesSinceReport = 0;
};
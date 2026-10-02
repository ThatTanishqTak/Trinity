#pragma once

#include <Trinity.hpp>

#include <vector>

class SandboxLayer final : public Trinity::Layer
{
public:
    SandboxLayer();

    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate(Trinity::Timestep timestep) override;
    void OnEvent(Trinity::Event& event) override;

private:
    bool OnKeyPressed(Trinity::KeyPressedEvent& event);
    void TestUUIDs();
    void TestJobs();
    void TestParallelFor();
    void TestFileSystem();
    void TestSaves();
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

    float m_SecondsSinceReport = 0.0f;
    std::uint32_t m_FramesSinceReport = 0;
};
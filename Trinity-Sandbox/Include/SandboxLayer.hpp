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

    void* m_ScratchBuffer = nullptr;
    std::vector<std::uint32_t> m_Probe;

    float m_SecondsSinceReport = 0.0f;
    std::uint32_t m_FramesSinceReport = 0;
};
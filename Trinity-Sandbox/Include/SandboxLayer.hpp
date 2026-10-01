#pragma once

#include <Trinity.hpp>

class SandboxLayer final : public Trinity::Layer
{
public:
    SandboxLayer();

    void OnAttach() override;
    void OnUpdate(Trinity::Timestep timestep) override;
    void OnEvent(Trinity::Event& event) override;

private:
    bool OnKeyPressed(Trinity::KeyPressedEvent& event);

    float m_SecondsSinceReport = 0.0f;
    std::uint32_t m_FramesSinceReport = 0;
};

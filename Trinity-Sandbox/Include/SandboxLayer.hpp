#pragma once

#include <Trinity.hpp>

#include <cstdint>
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
    enum class SpritePhase : std::uint8_t
    {
        Idle,
        Loading,
        Running
    };

    bool OnKeyPressed(Trinity::KeyPressedEvent& event);
    void TestCompute();
    void CreateSprites();
    void UpdateSprites();
    void TestSpriteReadback();
    void ReportSprites();
    void DestroySprites();

    Trinity::Scope<Trinity::AssetRegistry> m_SpriteRegistry;
    Trinity::Scope<Trinity::Scene> m_SpriteScene;
    Trinity::Scope<Trinity::Scene> m_SpriteReadbackScene;
    std::vector<Trinity::UUID> m_SpriteTextures;
    SpritePhase m_SpritePhase = SpritePhase::Idle;
    std::uint64_t m_SpritePhaseFrame = 0;
    std::uint64_t m_SpriteLoadedFrame = 0;
    std::uint64_t m_SpriteFrames = 0;
    std::uint64_t m_SpriteBytesAtCheck = 0;
    std::uint64_t m_SpriteBytesLast = 0;
    std::uint64_t m_SpriteBadFrames = 0;
    bool m_SpriteReported = false;
    bool m_ComputeTested = false;

    float m_ClearHue = 0.0f;
    float m_SecondsSinceReport = 0.0f;
    std::uint32_t m_FramesSinceReport = 0;
};
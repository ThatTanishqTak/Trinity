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
    void OnBuildFrameGraph(Trinity::FrameGraph& graph, Trinity::FrameGraphTexture sceneColor) override;

private:
    enum class SpritePhase : std::uint8_t
    {
        Idle,
        Loading,
        Reading,
        Running
    };

    bool OnKeyPressed(Trinity::KeyPressedEvent& event);
    void TestCompute();
    void TestLayeredTextures();
    void TestRasterState();
    void TestMultisampling();
    void TestTimestamps();
    void TestFrameGraph();
    void CheckRendererGraph();
    void UpdateResizes();
    void CreateSprites();
    void UpdateSprites();
    void AddSpriteReadback(Trinity::FrameGraph& graph);
    void CheckSpriteReadback();
    void ReportSprites();
    void DestroySprites();

    Trinity::Scope<Trinity::AssetRegistry> m_SpriteRegistry;
    Trinity::Scope<Trinity::Scene> m_SpriteScene;
    Trinity::Scope<Trinity::Scene> m_SpriteReadbackScene;
    std::vector<Trinity::UUID> m_SpriteTextures;
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
    bool m_RHITested = false;
    bool m_GraphChecked = false;

    std::uint32_t m_Resizes = 0;
    std::uint32_t m_ResizeWidth = 0;
    std::uint32_t m_ResizeHeight = 0;
    std::uint64_t m_ResizeBytesMiddle = 0;

    float m_ClearHue = 0.0f;
    float m_SecondsSinceReport = 0.0f;
    std::uint32_t m_FramesSinceReport = 0;
};
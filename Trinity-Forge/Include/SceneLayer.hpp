#pragma once

#include <Trinity.hpp>

#include <cstdint>

// The placeholder scene until Forge has a real one: Sandbox's triangle, in a centred square so it keeps its shape, and a count of the input that reaches it
class SceneLayer final : public Trinity::Layer
{
public:
    SceneLayer();

    void OnAttach() override;
    void OnDetach() override;
    void OnEvent(Trinity::Event& event) override;
    void OnRender(Trinity::RHI::CommandList& commands) override;

    [[nodiscard]] std::uint64_t GetMouseEventCount() const { return m_MouseEvents; }
    [[nodiscard]] std::uint64_t GetKeyEventCount() const { return m_KeyEvents; }

private:
    Trinity::RHI::PipelineHandle m_Pipeline;
    Trinity::RHI::BufferHandle m_Vertices;
    std::uint32_t m_VertexIndex = Trinity::RHI::c_NoBindlessIndex;
    std::uint64_t m_MouseEvents = 0;
    std::uint64_t m_KeyEvents = 0;
};
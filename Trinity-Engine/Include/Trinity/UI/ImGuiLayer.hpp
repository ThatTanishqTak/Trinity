#pragma once

#include "Trinity/Core/Layer.hpp"

#include <cstdint>

struct ImGuiContext;

namespace Trinity
{
    // Runs a Dear ImGui frame around every layer's OnImGuiRender, an application opts in by pushing it as an overlay, so programs without it never link ImGui
    class TRINITY_API ImGuiLayer final : public Layer
    {
    public:
        ImGuiLayer();

        void OnAttach() override;
        void OnDetach() override;
        void OnUpdate(Timestep timestep) override;

    private:
        void SaveSettings();
        void AcknowledgeTextures(bool shutdown);

        ImGuiContext* m_Context = nullptr;
        std::uint64_t m_FrameCount = 0;
    };
}
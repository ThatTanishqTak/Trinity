#pragma once

#include "Trinity/Core/Base.hpp"
#include "Trinity/Core/Layer.hpp"

#include <cstdint>

struct ImGuiContext;

namespace Trinity
{
    class ImGuiRenderer;

    // Runs a Dear ImGui frame around every layer's OnImGuiRender, an application opts in by pushing it as an overlay, so programs without it never link ImGui
    class TRINITY_API ImGuiLayer final : public Layer
    {
    public:
        ImGuiLayer();
        ~ImGuiLayer() override;

        void OnAttach() override;
        void OnDetach() override;
        void OnUpdate(Timestep timestep) override;
        void OnPrepareRender(RHI::CommandList& commands) override;
        void OnRenderUI(RHI::CommandList& commands) override;

    private:
        void SaveSettings();

        ImGuiContext* m_Context = nullptr;
        Scope<ImGuiRenderer> m_Renderer;
        std::uint64_t m_FrameCount = 0;
    };
}
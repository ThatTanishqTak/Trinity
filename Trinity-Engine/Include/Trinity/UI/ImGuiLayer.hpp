#pragma once

#include "Trinity/Core/Base.hpp"
#include "Trinity/Core/Layer.hpp"

#include <cstdint>
#include <string>

struct ImFont;
struct ImGuiContext;

namespace Trinity
{
    class ImGuiRenderer;
    class Window;

    enum class UIFont : std::uint8_t
    {
        Regular,
        Bold
    };

    // Runs a Dear ImGui frame around every layer's OnImGuiRender, an application opts in by pushing it as an overlay, so programs without it never link ImGui
    class TRINITY_API ImGuiLayer final : public Layer
    {
    public:
        ImGuiLayer();
        ~ImGuiLayer() override;

        void OnAttach() override;
        void OnDetach() override;
        void OnUpdate(Timestep timestep) override;
        void OnEvent(Event& event) override;
        void OnPrepareRender(RHI::CommandList& commands) override;
        void OnRenderUI(RHI::CommandList& commands) override;

        [[nodiscard]] static ImFont* GetFont(UIFont font);

    private:
        void SaveSettings();
        void UpdateCursor(Window& window);
        void UpdateScale();

        ImGuiContext* m_Context = nullptr;
        Scope<ImGuiRenderer> m_Renderer;
        std::string m_ClipboardText;
        int m_Cursor = -2;
        float m_DpiScale = 1.0f;
        float m_AppliedDpiScale = 0.0f;
        float m_AppliedUserScale = 0.0f;
        std::uint64_t m_FrameCount = 0;
    };
}
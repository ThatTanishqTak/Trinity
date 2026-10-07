#pragma once

#include "Trinity/Core/Base.hpp"
#include "Trinity/Core/Layer.hpp"

#include <cstdint>
#include <string>
#include <vector>

struct ImFont;
struct ImGuiContext;
struct ImGuiViewport;

namespace Trinity
{
    class ImGuiRenderer;
    class TextureAsset;
    class Window;

    enum class UIFont : std::uint8_t
    {
        Regular,
        Bold
    };

    // The upper half of an ImGui texture ID carries flags for ImGui.slang. This one has it encode what an sRGB texture decodes to back to sRGB, since ImGui draws in gamma space
    constexpr std::uint64_t c_ImGuiEncodeSrgb = std::uint64_t{ 1 } << 32;

    // The ID to draw a texture asset with in ImGui: its bindless index, with c_ImGuiEncodeSrgb for an sRGB texture
    [[nodiscard]] TRINITY_API std::uint64_t GetImGuiTextureID(const TextureAsset& texture);

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

        void SetSceneInput(bool mouse, bool keyboard);

        [[nodiscard]] static ImFont* GetFont(UIFont font);

    private:
        struct ViewportWindow;

        void LoadSettings();
        void SaveSettings();
        void UpdateCursor(Window& window);
        void UpdateScale();
        void UpdateViewports();
        void UpdateMonitors();
        void OnViewportEvent(ImGuiViewport& viewport, Event& event);

        static void CreateViewportWindow(ImGuiViewport* viewport);
        static void DestroyViewportWindow(ImGuiViewport* viewport);
        static void CreateViewportOutput(ImGuiViewport* viewport);
        static void DestroyViewportOutput(ImGuiViewport* viewport);

        ImGuiContext* m_Context = nullptr;
        Scope<ImGuiRenderer> m_Renderer;
        std::string m_ClipboardText;
        std::vector<Scope<ViewportWindow>> m_ViewportWindows;
        Window* m_EventSource = nullptr;
        Window* m_MouseWindow = nullptr;
        bool m_ViewportsSupported = false;
        bool m_MonitorsChanged = false;
        bool m_SettingsLoaded = false;
        bool m_SceneInputSet = false;
        bool m_SceneMouse = false;
        bool m_SceneKeyboard = false;
        int m_Cursor = -2;
        float m_DpiScale = 1.0f;
        float m_AppliedDpiScale = 0.0f;
        float m_AppliedUserScale = 0.0f;
        std::uint64_t m_FrameCount = 0;
    };
}
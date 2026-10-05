#pragma once

#include "EditorSession.hpp"
#include "Panels/Panel.hpp"
#include "Panels/ViewportPanel.hpp"
#include "SceneLayer.hpp"

#include <Trinity.hpp>

#include <cstdint>
#include <string>
#include <vector>

struct ImGuiContext;
struct ImGuiSettingsHandler;
struct ImGuiTextBuffer;

class ForgeLayer final : public Trinity::Layer
{
public:
    ForgeLayer(Trinity::ImGuiLayer& imGui, const SceneLayer& scene);

    void OnAttach() override;
    void OnUpdate(Trinity::Timestep timestep) override;
    void OnEvent(Trinity::Event& event) override;
    void OnImGuiRender() override;
    [[nodiscard]] bool OnCloseRequested() override;

private:
    void ReadShortcuts();
    void DrawMenuBar();
    void DrawDockSpace();
    void BuildDefaultLayout(std::uint32_t dockSpace);
    void DrawAboutWindow();

    static void* OpenPanelSettings(ImGuiContext* context, ImGuiSettingsHandler* handler, const char* name);
    static void ReadPanelSetting(ImGuiContext* context, ImGuiSettingsHandler* handler, void* entry, const char* line);
    static void WritePanelSettings(ImGuiContext* context, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buffer);

    Trinity::ImGuiLayer& m_ImGui;
    const SceneLayer& m_Scene;
    EditorSession m_Session;
    std::vector<Trinity::Scope<Panel>> m_Panels;
    ViewportPanel* m_ViewportPanel = nullptr;
    std::string m_AboutTitle;
    bool m_ShowDemoWindow = false;
    bool m_ShowAbout = false;
    bool m_ResetLayout = false;
};
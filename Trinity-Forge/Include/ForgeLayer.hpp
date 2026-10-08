#pragma once

#include "EditorSession.hpp"
#include "ImportTest.hpp"
#include "Panels/ContentBrowserPanel.hpp"
#include "Panels/Panel.hpp"
#include "Panels/ViewportPanel.hpp"

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
    explicit ForgeLayer(Trinity::ImGuiLayer& imGui);

    void OnAttach() override;
    void OnUpdate(Trinity::Timestep timestep) override;
    void OnEvent(Trinity::Event& event) override;
    void OnPrepareRender(Trinity::RHI::CommandList& commands) override;
    void OnRender(Trinity::RHI::CommandList& commands) override;
    void OnBuildFrameGraph(Trinity::FrameGraph& graph, Trinity::FrameGraphTexture sceneColor) override;
    void OnImGuiRender() override;
    [[nodiscard]] bool OnCloseRequested() override;

private:
    void ReadShortcuts();
    void DrawMenuBar();
    void DrawEditMenu();
    void DrawHistoryMenu();
    void DrawDockSpace();
    void BuildDefaultLayout(std::uint32_t dockSpace);
    void DockNewPanels();
    void DrawAboutWindow();

    static void* OpenPanelSettings(ImGuiContext* context, ImGuiSettingsHandler* handler, const char* name);
    static void ReadPanelSetting(ImGuiContext* context, ImGuiSettingsHandler* handler, void* entry, const char* line);
    static void WritePanelSettings(ImGuiContext* context, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buffer);

    Trinity::ImGuiLayer& m_ImGui;
    EditorSession m_Session;
    ImportTest m_ImportTest{ m_Session };
    std::vector<Trinity::Scope<Panel>> m_Panels;
    ViewportPanel* m_ViewportPanel = nullptr;
    ContentBrowserPanel* m_ContentBrowser = nullptr;
    std::string m_AboutTitle;
    bool m_ShowDemoWindow = false;
    bool m_ShowAbout = false;
    bool m_ResetLayout = false;
    bool m_DockedNewPanels = false;
};
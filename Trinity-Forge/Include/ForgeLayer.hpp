#pragma once

#include "EditorSession.hpp"
#include "ImportTest.hpp"
#include "Panels/AudioMixerPanel.hpp"
#include "Panels/ConsolePanel.hpp"
#include "Panels/ContentBrowserPanel.hpp"
#include "Panels/GamePanel.hpp"
#include "Panels/HierarchyPanel.hpp"
#include "Panels/InputActionsPanel.hpp"
#include "Panels/Panel.hpp"
#include "Panels/ProjectSettingsPanel.hpp"
#include "Panels/ViewportPanel.hpp"
#include "PreferencesWindow.hpp"

#include <Trinity.hpp>

#include <cstdint>
#include <string>
#include <vector>

struct ImGuiContext;
struct ImGuiSettingsHandler;
struct ImGuiTextBuffer;

class PropertiesPanel;

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
    void DrawPlayControls();
    void DrawBuildStatus();
    void PushPlayTint();
    void DrawEditMenu();
    void DrawEntityMenu();
    void CreateEmpty(Trinity::UUID parent);
    void DrawRecentScenesMenu();
    void DrawScriptsMenu();
    void DrawHistoryMenu();
    void DrawDockSpace();
    void BuildDefaultLayout(std::uint32_t dockSpace);
    void DockNewPanels();
    void DrawAboutWindow();
    void DrawStartScreen();
    void DrawNotice();
    void DrawStatusBar();
    void ReadStatusLine();
    void PauseOnError();

    static void* OpenPanelSettings(ImGuiContext* context, ImGuiSettingsHandler* handler, const char* name);
    static void ReadPanelSetting(ImGuiContext* context, ImGuiSettingsHandler* handler, void* entry, const char* line);
    static void WritePanelSettings(ImGuiContext* context, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buffer);

    Trinity::ImGuiLayer& m_ImGui;
    EditorSession m_Session;
    ImportTest m_ImportTest{ m_Session };
    PreferencesWindow m_PreferencesWindow{ m_Session };
    std::vector<Trinity::Scope<Panel>> m_Panels;
    ViewportPanel* m_ViewportPanel = nullptr;
    HierarchyPanel* m_HierarchyPanel = nullptr;
    GamePanel* m_GamePanel = nullptr;
    PropertiesPanel* m_PropertiesPanel = nullptr;
    ProjectSettingsPanel* m_ProjectSettings = nullptr;
    ContentBrowserPanel* m_ContentBrowser = nullptr;
    InputActionsPanel* m_InputActionsPanel = nullptr;
    AudioMixerPanel* m_AudioMixerPanel = nullptr;
    ConsolePanel* m_Console = nullptr;
    std::string m_AboutTitle;
    // The status bar's line, the last logged at Info or above, copied from the history only when a newer one arrives
    std::string m_StatusText;
    Trinity::LogLevel m_StatusLevel = Trinity::LogLevel::Info;
    std::uint64_t m_StatusSequence = 0;
    // The sequence after the last line Error Pause looked at
    std::uint64_t m_ErrorScanSequence = 0;
    bool m_ShowDemoWindow = false;
    bool m_ShowAbout = false;
    bool m_ResetLayout = false;
    bool m_DockedNewPanels = false;
    // Whether a copy played last frame, so the Game panel comes to the front as one starts and the Scene panel as it stops
    bool m_WasPlaying = false;
    int m_TintedColors = 0;
};
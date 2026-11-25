#pragma once

#include "EditorSession.hpp"
#include "CommandPalette.hpp"
#include "Export/ExportCommand.hpp"
#include "ImportTest.hpp"
#include "Panels/AudioMixerPanel.hpp"
#include "Panels/BuildPanel.hpp"
#include "Panels/ConsolePanel.hpp"
#include "Panels/ContentBrowserPanel.hpp"
#include "Panels/GamePanel.hpp"
#include "Panels/HierarchyPanel.hpp"
#include "Panels/InputActionsPanel.hpp"
#include "Panels/Panel.hpp"
#include "Panels/ProjectSettingsPanel.hpp"
#include "Panels/SoundEventPanel.hpp"
#include "Panels/ViewportPanel.hpp"
#include "PreferencesWindow.hpp"
#include "ProjectThumbnail.hpp"
#include "SceneCapture.hpp"
#include "ShortcutsWindow.hpp"
#include "Toasts.hpp"

#include <Trinity.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
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
    void OnDetach() override;
    void OnUpdate(Trinity::Timestep timestep) override;
    void OnEvent(Trinity::Event& event) override;
    void OnPrepareRender(Trinity::RHI::CommandList& commands) override;
    void OnRender(Trinity::RHI::CommandList& commands) override;
    void OnBuildFrameGraph(Trinity::FrameGraph& graph, Trinity::FrameGraphTexture sceneColor) override;
    void OnImGuiRender() override;
    [[nodiscard]] bool OnCloseRequested() override;

private:
    void RegisterShortcuts();
    void ToggleMaximized();
    void SetMaximized(Panel* panel);
    // The command's key as its menu item and its button's tip show it
    [[nodiscard]] std::string GetKeyText(std::string_view id) const { return m_Session.GetShortcuts().GetKeyText(id); }
    void ReadShortcuts();
    void DrawMenuBar();
    void DrawToolbar();
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
    void DrawExportPopup();
    void DrawLayoutMenu();
    void DrawSaveLayoutPopup();
    void SaveLayout(std::string_view name);
    void ApplyLayout(const std::string& name);
    void BuildAndRun();
    void DrawStatusBar();
    void ReadStatusLine();
    void PauseOnError();

    static void* OpenPanelSettings(ImGuiContext* context, ImGuiSettingsHandler* handler, const char* name);
    static void ReadPanelSetting(ImGuiContext* context, ImGuiSettingsHandler* handler, void* entry, const char* line);
    static void WritePanelSettings(ImGuiContext* context, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buffer);

    Trinity::ImGuiLayer& m_ImGui;
    EditorSession m_Session;
    ImportTest m_ImportTest{ m_Session };
    ExportCommand m_Export{ m_Session };
    SceneCapture m_Capture{ m_Session };
    ProjectThumbnail m_Thumbnail{ m_Session };
    Toasts m_Toasts;
    PreferencesWindow m_PreferencesWindow{ m_Session };
    ShortcutsWindow m_ShortcutsWindow{ m_Session };
    CommandPalette m_CommandPalette{ m_Session };
    std::vector<Trinity::Scope<Panel>> m_Panels;
    // The panel that fills the dock space alone, the others not drawn until it is restored, or null
    Panel* m_Maximized = nullptr;
    // While a panel outside the centre is maximised: the dock node that is the centre, and the panel's own, which is lent its place
    std::uint32_t m_CentralNode = 0;
    std::uint32_t m_LentNode = 0;
    ViewportPanel* m_ViewportPanel = nullptr;
    HierarchyPanel* m_HierarchyPanel = nullptr;
    GamePanel* m_GamePanel = nullptr;
    PropertiesPanel* m_PropertiesPanel = nullptr;
    ProjectSettingsPanel* m_ProjectSettings = nullptr;
    BuildPanel* m_BuildPanel = nullptr;
    ContentBrowserPanel* m_ContentBrowser = nullptr;
    InputActionsPanel* m_InputActionsPanel = nullptr;
    AudioMixerPanel* m_AudioMixerPanel = nullptr;
    SoundEventPanel* m_SoundEventPanel = nullptr;
    ConsolePanel* m_Console = nullptr;
    std::string m_AboutTitle;
    // The last line logged at Info or above, copied from the history only when a newer one arrives
    std::string m_StatusText;
    Trinity::LogLevel m_StatusLevel = Trinity::LogLevel::Info;
    std::uint64_t m_StatusSequence = 0;
    // The sequence after the last line Error Pause looked at
    std::uint64_t m_ErrorScanSequence = 0;
    // Whether the export's popup was opened, so one another popup closed opens again
    bool m_ExportPopupOpen = false;
    bool m_ShowDemoWindow = false;
    bool m_ShowAbout = false;
    // The layout to change to before the next frame begins, since ImGui's settings are not loaded within one
    std::optional<std::string> m_PendingLayout;
    // The name typed so far for the layout Save Layout As makes
    std::string m_LayoutName;
    bool m_OpenSaveLayoutPopup = false;
    bool m_ResetLayout = false;
    bool m_DockedNewPanels = false;
    // Whether a copy played last frame, so the Game panel comes forward at Play and the Scene at Stop
    bool m_WasPlaying = false;
    int m_TintedColors = 0;
};
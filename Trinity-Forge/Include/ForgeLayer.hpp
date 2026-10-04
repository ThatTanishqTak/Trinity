#pragma once

#include "Panels/Panel.hpp"

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
    ForgeLayer();

    void OnAttach() override;
    void OnEvent(Trinity::Event& event) override;
    void OnImGuiRender() override;

private:
    bool OnKeyPressed(Trinity::KeyPressedEvent& event);
    void DrawMenuBar();
    void DrawDockSpace();
    void BuildDefaultLayout(std::uint32_t dockSpace);
    void DrawAboutWindow();

    static void* OpenPanelSettings(ImGuiContext* context, ImGuiSettingsHandler* handler, const char* name);
    static void ReadPanelSetting(ImGuiContext* context, ImGuiSettingsHandler* handler, void* entry, const char* line);
    static void WritePanelSettings(ImGuiContext* context, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buffer);

    std::vector<Trinity::Scope<Panel>> m_Panels;
    std::string m_AboutTitle;
    bool m_ShowDemoWindow = false;
    bool m_ShowAbout = false;
    bool m_ResetLayout = false;
};
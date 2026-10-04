#include "ForgeLayer.hpp"

#include "Panels/ConsolePanel.hpp"
#include "Panels/HierarchyPanel.hpp"
#include "Panels/PropertiesPanel.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <array>
#include <format>
#include <string_view>
#include <utility>

namespace
{
    // Hashed without the ID stack, so the dock space keeps its ID whatever ImGui window is current
    constexpr std::string_view c_DockSpaceName = "Forge dock space";
    constexpr const char* c_PanelSettingsName = "ForgePanels";

    std::string WithIcon(const char* icon, std::string_view text)
    {
        return std::format("{} {}", icon, text);
    }

    void TextLine(std::string_view text)
    {
        ImGui::TextUnformatted(text.data(), text.data() + text.size());
    }
}

ForgeLayer::ForgeLayer(Trinity::ImGuiLayer& imGui, const SceneLayer& scene) : Layer("Forge"), m_ImGui(imGui), m_Scene(scene), m_AboutTitle(std::format("{} About Trinity Forge###About", Trinity::Icons::c_InfoCircle))
{

}

void ForgeLayer::OnAttach()
{
    const Trinity::ApplicationCommandLineArgs& l_Args = Trinity::Application::Get().GetSpecification().CommandLineArgs;

    if (const auto it_Project = l_Args.GetOption("project"))
    {
        TR_INFO("Project: {}", *it_Project);
    }
    else
    {
        TR_INFO("No project given. Start Forge from Trinity-Hub or pass --project=<path>.");
    }

    // The scene is shown in the Viewport panel, so the window gets no copy of it under the UI
    Trinity::Application::Get().GetRenderer().SetSceneCopy(false);

    Trinity::Scope<ViewportPanel> l_Viewport = Trinity::CreateScope<ViewportPanel>(m_ImGui, m_Scene);
    m_ViewportPanel = l_Viewport.get();
    m_Panels.push_back(std::move(l_Viewport));
    m_Panels.push_back(Trinity::CreateScope<HierarchyPanel>());
    m_Panels.push_back(Trinity::CreateScope<PropertiesPanel>());
    m_Panels.push_back(Trinity::CreateScope<ConsolePanel>());

    // ImGui reads imgui.ini before its first frame, which is after this, so the panels get their saved state
    ImGuiSettingsHandler l_Handler;
    l_Handler.TypeName = c_PanelSettingsName;
    l_Handler.TypeHash = ImHashStr(c_PanelSettingsName);
    l_Handler.ReadOpenFn = &ForgeLayer::OpenPanelSettings;
    l_Handler.ReadLineFn = &ForgeLayer::ReadPanelSetting;
    l_Handler.WriteAllFn = &ForgeLayer::WritePanelSettings;
    l_Handler.UserData = this;
    ImGui::AddSettingsHandler(&l_Handler);
}

// The menu bar comes first, so the dock space fits in the space below it. Shortcuts are read from ImGui, since layer events are the scene's while the Viewport has them
void ForgeLayer::OnImGuiRender()
{
    // The Viewport panel gives the scene its input back while it is hovered or focused, and a closed panel leaves it with none
    m_ImGui.SetSceneInput(false, false);

    // F1 shows and hides the demo window, though not while a text field takes the keyboard
    if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F1, false))
    {
        m_ShowDemoWindow = !m_ShowDemoWindow;
        TR_INFO("Forge: demo window {}", m_ShowDemoWindow ? "shown" : "hidden");
    }

    DrawMenuBar();
    DrawDockSpace();

    for (const Trinity::Scope<Panel>& it_Panel : m_Panels)
    {
        it_Panel->Draw();
    }

    if (m_ShowAbout)
    {
        DrawAboutWindow();
    }

    if (m_ShowDemoWindow)
    {
        ImGui::ShowDemoWindow(&m_ShowDemoWindow);
    }
}

void ForgeLayer::DrawMenuBar()
{
    if (!ImGui::BeginMainMenuBar())
    {
        return;
    }

    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_PowerOff, "Exit").c_str(), "Alt+F4"))
        {
            Trinity::Application::Get().Close();
        }

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View"))
    {
        for (const Trinity::Scope<Panel>& it_Panel : m_Panels)
        {
            bool l_Open = it_Panel->IsOpen();
            if (ImGui::MenuItem(it_Panel->GetMenuLabel().c_str(), nullptr, &l_Open))
            {
                it_Panel->SetOpen(l_Open);
            }
        }

        ImGui::Separator();

        bool l_ShowStats = m_ViewportPanel->IsShowingStats();
        if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_Eye, "Viewport Stats").c_str(), nullptr, &l_ShowStats))
        {
            m_ViewportPanel->SetShowingStats(l_ShowStats);
        }

        if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_Refresh, "Reset Layout").c_str()))
        {
            m_ResetLayout = true;
        }

        // Greyed out where the platform has no windows for ImGui to float panels in
        const bool l_Supported = (ImGui::GetIO().BackendFlags & ImGuiBackendFlags_PlatformHasViewports) != 0;
        const Trinity::ConsoleVariableBase* l_Variable = Trinity::ConsoleVariables::Find("ui.viewports");
        bool l_Viewports = l_Supported && l_Variable != nullptr && l_Variable->ToString() == "true";
        if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_WindowRestore, "Multi-viewport").c_str(), nullptr, &l_Viewports, l_Supported))
        {
            Trinity::ConsoleVariables::Set("ui.viewports", l_Viewports ? "true" : "false");
        }

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help"))
    {
        ImGui::MenuItem(WithIcon(Trinity::Icons::c_Book, "ImGui Demo").c_str(), "F1", &m_ShowDemoWindow);
        ImGui::Separator();
        ImGui::MenuItem(WithIcon(Trinity::Icons::c_InfoCircle, "About").c_str(), nullptr, &m_ShowAbout);

        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

// imgui.ini keeps each user's layout, and the default is built when it has none, such as on the first run, or on View > Reset Layout
void ForgeLayer::DrawDockSpace()
{
    const ImGuiID l_DockSpace = ImHashStr(c_DockSpaceName.data(), c_DockSpaceName.size());
    if (m_ResetLayout || ImGui::DockBuilderGetNode(l_DockSpace) == nullptr)
    {
        BuildDefaultLayout(l_DockSpace);
        m_ResetLayout = false;
    }

    ImGui::DockSpaceOverViewport(l_DockSpace, ImGui::GetMainViewport());
}

// Hierarchy on the left, Properties on the right, Console below the Viewport. Every panel opens again, including any floating in windows of their own
void ForgeLayer::BuildDefaultLayout(std::uint32_t dockSpace)
{
    ImGui::DockBuilderRemoveNode(dockSpace);
    ImGui::DockBuilderAddNode(dockSpace, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockSpace, ImGui::GetMainViewport()->WorkSize);

    std::array<ImGuiID, 4> l_Nodes{};
    ImGuiID l_Centre = dockSpace;
    l_Nodes[std::to_underlying(DockSlot::Left)] = ImGui::DockBuilderSplitNode(l_Centre, ImGuiDir_Left, 0.2f, nullptr, &l_Centre);
    l_Nodes[std::to_underlying(DockSlot::Right)] = ImGui::DockBuilderSplitNode(l_Centre, ImGuiDir_Right, 0.25f, nullptr, &l_Centre);
    l_Nodes[std::to_underlying(DockSlot::Bottom)] = ImGui::DockBuilderSplitNode(l_Centre, ImGuiDir_Down, 0.3f, nullptr, &l_Centre);
    l_Nodes[std::to_underlying(DockSlot::Centre)] = l_Centre;

    for (const Trinity::Scope<Panel>& it_Panel : m_Panels)
    {
        it_Panel->SetOpen(true);
        ImGui::DockBuilderDockWindow(it_Panel->GetWindowName().c_str(), l_Nodes[std::to_underlying(it_Panel->GetSlot())]);
    }

    ImGui::DockBuilderFinish(dockSpace);
    TR_INFO("Forge: default layout built");
}

// The version, the graphics backend and adapter, and the fonts and scale the UI is drawn with
void ForgeLayer::DrawAboutWindow()
{
    if (!ImGui::Begin(m_AboutTitle.c_str(), &m_ShowAbout, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking))
    {
        ImGui::End();

        return;
    }

    const Trinity::RHI::DeviceInfo& l_Device = Trinity::Application::Get().GetDevice().GetInfo();
    const bool l_Viewports = (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0;

    ImGui::PushFont(Trinity::ImGuiLayer::GetFont(Trinity::UIFont::Bold), 0.0f);
    TextLine("Trinity Forge");
    ImGui::PopFont();

    TextLine(std::format("Version {}", Trinity::GetVersionString()));
    TextLine(std::format("Backend {}", Trinity::ToString(l_Device.API)));
    TextLine(std::format("Adapter {}{}", l_Device.AdapterName.empty() ? "none" : l_Device.AdapterName, l_Device.VideoMemoryBytes > 0 ? std::format(", {} video memory", Trinity::Memory::FormatBytes(l_Device.VideoMemoryBytes)) : ""));
    TextLine(std::format("Dear ImGui {}, multi-viewport {}", IMGUI_VERSION, l_Viewports ? "on" : "off"));

    ImGui::Separator();
    TextLine(std::format("JetBrains Mono Nerd Font {} {} {} {} {}", Trinity::Icons::c_File, Trinity::Icons::c_FolderOpen, Trinity::Icons::c_Gear, Trinity::Icons::c_Terminal, Trinity::Icons::c_Monitor));
    TextLine(std::format("DPI scale {:.2f}, ui.scale {:.2f}, text {:.0f} px", ImGui::GetStyle().FontScaleDpi, ImGui::GetStyle().FontScaleMain, ImGui::GetFontSize()));

    ImGui::End();
}

// imgui.ini holds one [ForgePanels][Open] section, with a Title=1 or Title=0 line for each panel, and ViewportStats for the Viewport's overlay
void* ForgeLayer::OpenPanelSettings([[maybe_unused]] ImGuiContext* context, ImGuiSettingsHandler* handler, [[maybe_unused]] const char* name)
{
    return handler->UserData;
}

void ForgeLayer::ReadPanelSetting([[maybe_unused]] ImGuiContext* context, [[maybe_unused]] ImGuiSettingsHandler* handler, void* entry, const char* line)
{
    const std::string_view l_Line(line);
    const std::size_t l_Equals = l_Line.find('=');
    if (l_Equals == std::string_view::npos)
    {
        return;
    }

    const std::string_view l_Title = l_Line.substr(0, l_Equals);
    const bool l_On = l_Line.substr(l_Equals + 1) != "0";
    ForgeLayer& l_Layer = *static_cast<ForgeLayer*>(entry);
    if (l_Title == "ViewportStats")
    {
        l_Layer.m_ViewportPanel->SetShowingStats(l_On);

        return;
    }

    for (const Trinity::Scope<Panel>& it_Panel : l_Layer.m_Panels)
    {
        if (it_Panel->GetTitle() == l_Title)
        {
            it_Panel->SetOpen(l_On);
        }
    }
}

void ForgeLayer::WritePanelSettings([[maybe_unused]] ImGuiContext* context, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buffer)
{
    const ForgeLayer& l_Layer = *static_cast<const ForgeLayer*>(handler->UserData);

    buffer->appendf("[%s][Open]\n", handler->TypeName);
    for (const Trinity::Scope<Panel>& it_Panel : l_Layer.m_Panels)
    {
        buffer->appendf("%s=%d\n", it_Panel->GetTitle().c_str(), it_Panel->IsOpen() ? 1 : 0);
    }

    buffer->appendf("ViewportStats=%d\n", l_Layer.m_ViewportPanel->IsShowingStats() ? 1 : 0);

    buffer->append("\n");
}
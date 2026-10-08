#include "ForgeLayer.hpp"

#include "EditorCommands.hpp"
#include "Panels/ConsolePanel.hpp"
#include "Panels/ContentBrowserPanel.hpp"
#include "Panels/HierarchyPanel.hpp"
#include "Panels/PropertiesPanel.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <format>
#include <string_view>
#include <utility>

namespace
{
    // Hashed without the ID stack, so the dock space keeps its ID whatever ImGui window is current
    constexpr std::string_view c_DockSpaceName = "Forge dock space";
    constexpr const char* c_PanelSettingsName = "ForgePanels";
    constexpr std::size_t c_HistoryMenuEntries = 30;

    std::string WithIcon(const char* icon, std::string_view text)
    {
        return std::format("{} {}", icon, text);
    }

    void TextLine(std::string_view text)
    {
        ImGui::TextUnformatted(text.data(), text.data() + text.size());
    }

    // From the first primary camera in hierarchy order, the one Renderer2D shows. A scene without one shows at exposure 1 with no curve
    Trinity::ToneMapping GetSceneToneMapping(Trinity::Scene& scene)
    {
        const Trinity::SceneRegistry& l_Registry = scene.GetRegistry();
        for (Trinity::Entity it_Entity = scene.GetFirstRoot(); it_Entity; it_Entity = scene.GetNextInHierarchyOrder(it_Entity))
        {
            if (const Trinity::CameraComponent* l_Camera = l_Registry.try_get<Trinity::CameraComponent>(it_Entity.GetHandle()); l_Camera != nullptr && l_Camera->Primary)
            {
                return l_Camera->GetToneMapping();
            }
        }

        return { Trinity::c_NeutralEV100, Trinity::Tonemapper::None };
    }
}

ForgeLayer::ForgeLayer(Trinity::ImGuiLayer& imGui) : Layer("Forge"), m_ImGui(imGui), m_AboutTitle(std::format("{} About Trinity Forge###About", Trinity::Icons::c_InfoCircle))
{

}

void ForgeLayer::OnAttach()
{
    const Trinity::ApplicationCommandLineArgs& l_Args = Trinity::Application::Get().GetSpecification().CommandLineArgs;
    if (const auto it_Directory = l_Args.GetOption("import-test"))
    {
        m_ImportTest.Start(it_Directory->empty() ? std::filesystem::temp_directory_path() / "Trinity-ImportTest" : std::filesystem::path(*it_Directory));
        m_Session.UpdateTitle();
    }
    else
    {
        m_Session.Start(l_Args);
    }

    // The scene is shown in the Viewport panel, so the window gets no copy of it under the UI
    Trinity::Application::Get().GetRenderer().SetSceneCopy(false);

    Trinity::Scope<ViewportPanel> l_Viewport = Trinity::CreateScope<ViewportPanel>(m_ImGui, m_Session);
    m_ViewportPanel = l_Viewport.get();
    m_Panels.push_back(std::move(l_Viewport));
    m_Panels.push_back(Trinity::CreateScope<HierarchyPanel>(m_Session));
    m_Panels.push_back(Trinity::CreateScope<PropertiesPanel>(m_Session));
    m_Panels.push_back(Trinity::CreateScope<ConsolePanel>());

    // Docked after the Console, so it is the tab the default layout shows
    Trinity::Scope<ContentBrowserPanel> l_ContentBrowser = Trinity::CreateScope<ContentBrowserPanel>(m_Session);
    m_ContentBrowser = l_ContentBrowser.get();
    m_Panels.push_back(std::move(l_ContentBrowser));

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

// Before ImGui's frame begins, so a native file dialog never blocks with a frame open. World transforms are brought up to date last, for the Viewport to draw and pick with
void ForgeLayer::OnUpdate([[maybe_unused]] Trinity::Timestep timestep)
{
    m_Session.Update();
    m_ImportTest.Update();
    m_Session.GetScene().UpdateWorldTransforms();
}

// Files may have changed while another program had focus
void ForgeLayer::OnEvent(Trinity::Event& event)
{
    if (event.GetEventType() == Trinity::EventType::WindowFocus)
    {
        m_Session.Request(EditorSession::Command::Refresh);
    }
}

// After the UI has had its say this frame and before the Renderer builds its graph, so an exposure dragged in the Properties panel shows in the same frame
void ForgeLayer::OnPrepareRender([[maybe_unused]] Trinity::RHI::CommandList& commands)
{
    Trinity::Application::Get().GetRenderer().SetToneMapping(GetSceneToneMapping(m_Session.GetScene()));
    m_ViewportPanel->PrepareScene();
}

// Drawn whether or not the Viewport panel is open, so the scene target holds the scene when the panel opens again. Transforms are brought up to date again, since the UI may have changed the scene since OnUpdate
void ForgeLayer::OnRender(Trinity::RHI::CommandList& commands)
{
    m_Session.GetScene().UpdateWorldTransforms();
    m_ViewportPanel->RenderScene(commands);
}

// Only the import test adds passes of its own, to read back what the renderer holds
void ForgeLayer::OnBuildFrameGraph(Trinity::FrameGraph& graph, [[maybe_unused]] Trinity::FrameGraphTexture sceneColor)
{
    m_ImportTest.OnBuildFrameGraph(graph);
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

    ReadShortcuts();
    DrawMenuBar();
    DrawDockSpace();

    for (const Trinity::Scope<Panel>& it_Panel : m_Panels)
    {
        it_Panel->Draw();
    }

    m_ContentBrowser->EndFrame();

    if (m_ShowAbout)
    {
        DrawAboutWindow();
    }

    if (m_ShowDemoWindow)
    {
        ImGui::ShowDemoWindow(&m_ShowDemoWindow);
    }

    m_Session.DrawPopups();
    m_Session.UpdateTitle();
}

// Unsaved changes keep Forge open until the person has answered
bool ForgeLayer::OnCloseRequested()
{
    return m_Session.RequestClose();
}

// Not while a field is typed into, nor while anything is being dragged, such as a gizmo, whose command is still open, nor while a popup is up, such as the texture import's
void ForgeLayer::ReadShortcuts()
{
    if (ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive() || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
    {
        return;
    }

    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_N))
    {
        m_Session.Request(EditorSession::Command::NewScene);
    }
    else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O))
    {
        m_Session.Request(EditorSession::Command::OpenScene);
    }
    else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S))
    {
        m_Session.Request(EditorSession::Command::SaveSceneAs);
    }
    else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
    {
        m_Session.Request(EditorSession::Command::SaveScene);
    }
    else if (ImGui::IsKeyChordPressed(ImGuiKey_F5))
    {
        m_Session.Request(EditorSession::Command::Refresh);
    }
    else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, ImGuiInputFlags_Repeat) || ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y, ImGuiInputFlags_Repeat))
    {
        m_Session.GetHistory().Redo();
    }
    else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z, ImGuiInputFlags_Repeat))
    {
        m_Session.GetHistory().Undo();
    }
    else if (m_ContentBrowser->IsFocused())
    {
        // Duplicate and Delete are the browser's own while it has focus
    }
    else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D) && m_Session.GetScene().FindEntityByUUID(m_Session.GetSelection()))
    {
        m_Session.GetHistory().Execute(Trinity::CreateScope<DuplicateEntityCommand>(m_Session.GetSelection()));
    }
    else if (ImGui::IsKeyChordPressed(ImGuiKey_Delete) && m_Session.GetScene().FindEntityByUUID(m_Session.GetSelection()))
    {
        m_Session.GetHistory().Execute(Trinity::CreateScope<DeleteEntityCommand>(m_Session.GetSelection()));
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
        const bool l_HasProject = m_Session.HasProject();
        const auto a_Item = [this](const char* icon, std::string_view label, const char* shortcut, bool enabled, EditorSession::Command command)
        {
            if (ImGui::MenuItem(WithIcon(icon, label).c_str(), shortcut, false, enabled))
            {
                m_Session.Request(command);
            }
        };

        a_Item(Trinity::Icons::c_File, "New Project...", nullptr, true, EditorSession::Command::NewProject);
        a_Item(Trinity::Icons::c_FolderOpen, "Open Project...", nullptr, true, EditorSession::Command::OpenProject);
        ImGui::Separator();
        a_Item(Trinity::Icons::c_File, "New Scene", "Ctrl+N", l_HasProject, EditorSession::Command::NewScene);
        a_Item(Trinity::Icons::c_FolderOpen, "Open Scene...", "Ctrl+O", l_HasProject, EditorSession::Command::OpenScene);
        a_Item(Trinity::Icons::c_Save, "Save", "Ctrl+S", l_HasProject, EditorSession::Command::SaveScene);
        a_Item(Trinity::Icons::c_Save, "Save As...", "Ctrl+Shift+S", l_HasProject, EditorSession::Command::SaveSceneAs);
        a_Item(Trinity::Icons::c_Gear, "Set as Start Scene", nullptr, l_HasProject && m_Session.HasScenePath(), EditorSession::Command::SetStartScene);
        a_Item(Trinity::Icons::c_Refresh, "Refresh", "F5", l_HasProject, EditorSession::Command::Refresh);
        ImGui::Separator();
        a_Item(Trinity::Icons::c_PowerOff, "Exit", "Alt+F4", true, EditorSession::Command::Exit);

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit"))
    {
        DrawEditMenu();

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

// Undo and Redo name the command they act on. Every edit goes through the history, so it can be undone
void ForgeLayer::DrawEditMenu()
{
    CommandStack& l_History = m_Session.GetHistory();
    const bool l_CanUndo = l_History.CanUndo();
    const bool l_CanRedo = l_History.CanRedo();

    const std::string l_Undo = l_CanUndo ? std::format("Undo {}###Undo", l_History.GetCommand(l_History.GetPosition() - 1).GetName()) : std::string("Undo###Undo");
    if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_Undo, l_Undo).c_str(), "Ctrl+Z", false, l_CanUndo))
    {
        l_History.Undo();
    }

    const std::string l_Redo = l_CanRedo ? std::format("Redo {}###Redo", l_History.GetCommand(l_History.GetPosition()).GetName()) : std::string("Redo###Redo");
    if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_Redo, l_Redo).c_str(), "Ctrl+Y", false, l_CanRedo))
    {
        l_History.Redo();
    }

    if (ImGui::BeginMenu(WithIcon(Trinity::Icons::c_History, "History").c_str(), l_History.GetCount() > 0))
    {
        DrawHistoryMenu();

        ImGui::EndMenu();
    }

    ImGui::Separator();

    if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_CubeOutline, "Create Empty Entity").c_str(), nullptr, false, m_Session.HasProject()))
    {
        l_History.Execute(Trinity::CreateScope<CreateEntityCommand>("Empty Entity", Trinity::UUID(), Trinity::UUID()));
    }

    const bool l_HasSelection = static_cast<bool>(m_Session.GetScene().FindEntityByUUID(m_Session.GetSelection()));
    if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_CubeOutline, "Duplicate").c_str(), "Ctrl+D", false, l_HasSelection))
    {
        l_History.Execute(Trinity::CreateScope<DuplicateEntityCommand>(m_Session.GetSelection()));
    }

    if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_Trash, "Delete").c_str(), "Delete", false, l_HasSelection))
    {
        l_History.Execute(Trinity::CreateScope<DeleteEntityCommand>(m_Session.GetSelection()));
    }
}

// The commands around the current position, oldest at the top, with those that would be redone greyed. Picking one undoes or redoes to just after it, and Start undoes everything the history holds
void ForgeLayer::DrawHistoryMenu()
{
    CommandStack& l_History = m_Session.GetHistory();
    const std::size_t l_Count = l_History.GetCount();
    const std::size_t l_Position = l_History.GetPosition();
    const std::size_t l_First = l_Count > c_HistoryMenuEntries ? std::min(l_Position - std::min(l_Position, c_HistoryMenuEntries / 2), l_Count - c_HistoryMenuEntries) : 0;
    const std::size_t l_Last = std::min(l_First + c_HistoryMenuEntries, l_Count);

    std::size_t l_Target = l_Position;
    if (ImGui::MenuItem("Start###HistoryStart", nullptr, l_Position == 0))
    {
        l_Target = 0;
    }

    if (l_First > 0)
    {
        ImGui::TextDisabled("%s", std::format("{} earlier", l_First).c_str());
    }

    for (std::size_t it_Index = l_First; it_Index < l_Last; ++it_Index)
    {
        const bool l_Undone = it_Index >= l_Position;
        if (l_Undone)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        }

        if (ImGui::MenuItem(std::format("{}###History{}", l_History.GetCommand(it_Index).GetName(), it_Index).c_str(), nullptr, it_Index + 1 == l_Position))
        {
            l_Target = it_Index + 1;
        }

        if (l_Undone)
        {
            ImGui::PopStyleColor();
        }
    }

    if (l_Last < l_Count)
    {
        ImGui::TextDisabled("%s", std::format("{} later", l_Count - l_Last).c_str());
    }

    if (l_Target != l_Position)
    {
        l_History.JumpTo(l_Target);
    }
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

    if (!std::exchange(m_DockedNewPanels, true))
    {
        DockNewPanels();
    }

    ImGui::DockSpaceOverViewport(l_DockSpace, ImGui::GetMainViewport());
}

// A panel added to Forge after the user's imgui.ini was saved has no place in it, so it joins the node of a panel with the same slot rather than floating
void ForgeLayer::DockNewPanels()
{
    for (const Trinity::Scope<Panel>& it_Panel : m_Panels)
    {
        if (ImGui::FindWindowSettingsByID(ImHashStr(it_Panel->GetWindowName().c_str())) != nullptr)
        {
            continue;
        }

        for (const Trinity::Scope<Panel>& it_Other : m_Panels)
        {
            const ImGuiWindowSettings* l_Settings = it_Other->GetSlot() == it_Panel->GetSlot() ? ImGui::FindWindowSettingsByID(ImHashStr(it_Other->GetWindowName().c_str())) : nullptr;
            if (l_Settings != nullptr && l_Settings->DockId != 0)
            {
                ImGui::DockBuilderDockWindow(it_Panel->GetWindowName().c_str(), l_Settings->DockId);
                TR_INFO("Forge: {} joins {} in the saved layout", it_Panel->GetTitle(), it_Other->GetTitle());

                break;
            }
        }
    }
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

// imgui.ini holds one [ForgePanels][Open] section, with a Title=1 or Title=0 line for each panel, and the Viewport's own settings: its overlay, gizmo and snap steps
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
    if (l_Layer.m_ViewportPanel->ReadSetting(l_Title, l_Line.substr(l_Equals + 1)))
    {
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

    l_Layer.m_ViewportPanel->WriteSettings(*buffer);

    buffer->append("\n");
}
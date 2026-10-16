#include "ForgeLayer.hpp"

#include "EditorCommands.hpp"
#include "Panels/ContentBrowserPanel.hpp"
#include "Panels/HierarchyPanel.hpp"
#include "Panels/PropertiesPanel.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <format>
#include <optional>
#include <string_view>
#include <utility>

namespace
{
    // Hashed without the ID stack, so the dock space keeps its ID whatever ImGui window is current
    constexpr std::string_view c_DockSpaceName = "Forge dock space";
    constexpr const char* c_PanelSettingsName = "ForgePanels";
    constexpr std::size_t c_HistoryMenuEntries = 30;
    // The backgrounds lean this far towards the tint while a copy plays, and further while it is paused
    constexpr ImVec4 c_PlayTint{ 0.2f, 0.35f, 0.65f, 1.0f };
    constexpr float c_PlayingTint = 0.18f;
    constexpr float c_PausedTint = 0.3f;
    // A notice stays this long, fading over the end of it, in text this much larger than the UI's
    constexpr float c_NoticeSeconds = 2.5f;
    constexpr float c_NoticeFadeSeconds = 0.5f;
    constexpr float c_NoticeScale = 1.4f;
    constexpr std::array<ImGuiCol, 7> c_TintedColors{ ImGuiCol_WindowBg, ImGuiCol_PopupBg, ImGuiCol_MenuBarBg, ImGuiCol_TitleBg, ImGuiCol_TitleBgActive, ImGuiCol_TitleBgCollapsed, ImGuiCol_DockingEmptyBg };

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

    // The scene is shown in the Scene and Game panels, so the window gets no copy of it under the UI
    Trinity::Application::Get().GetRenderer().SetSceneCopy(false);

    // The Scene and Game panels share the centre as tabs, the Scene panel in front
    Trinity::Scope<ViewportPanel> l_Viewport = Trinity::CreateScope<ViewportPanel>(m_ImGui, m_Session);
    m_ViewportPanel = l_Viewport.get();
    m_Panels.push_back(std::move(l_Viewport));
    Trinity::Scope<GamePanel> l_Game = Trinity::CreateScope<GamePanel>(m_Session);
    m_GamePanel = l_Game.get();
    m_Panels.push_back(std::move(l_Game));
    m_Panels.push_back(Trinity::CreateScope<HierarchyPanel>(m_Session));
    Trinity::Scope<PropertiesPanel> l_Properties = Trinity::CreateScope<PropertiesPanel>(m_Session);
    m_PropertiesPanel = l_Properties.get();
    m_Panels.push_back(std::move(l_Properties));

    // Docked as a tab behind Properties
    Trinity::Scope<ProjectSettingsPanel> l_ProjectSettings = Trinity::CreateScope<ProjectSettingsPanel>(m_Session);
    m_ProjectSettings = l_ProjectSettings.get();
    m_Panels.push_back(std::move(l_ProjectSettings));
    // A build's error opens where it is, in the script editor, on a double-click
    Trinity::Scope<ConsolePanel> l_Console = Trinity::CreateScope<ConsolePanel>();
    m_Console = l_Console.get();
    m_Console->SetOpenLocation([this](std::string_view path, std::uint32_t line) { static_cast<void>(m_Session.OpenScript(path, line)); });
    m_Panels.push_back(std::move(l_Console));

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

// Before ImGui's frame begins, so a native file dialog never blocks with a frame open. A playing copy steps after the session's work, and world transforms are brought up to date last, for the Viewport to draw and pick with
void ForgeLayer::OnUpdate(Trinity::Timestep timestep)
{
    m_Session.Update();
    m_ImportTest.Update();
    m_Session.UpdatePlay(timestep);
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

// After the UI has had its say this frame and before the Renderer builds its graph, so an exposure dragged in the Properties panel shows in the same frame. Transforms are brought up to date first, since the UI may have changed them. The Scene panel's view is the main one, collected first
void ForgeLayer::OnPrepareRender([[maybe_unused]] Trinity::RHI::CommandList& commands)
{
    m_Session.GetScene().UpdateWorldTransforms();
    Trinity::Application::Get().GetRenderer().SetToneMapping(GetSceneToneMapping(m_Session.GetScene()));
    m_ViewportPanel->PrepareScene();
    m_GamePanel->PrepareScene();
}

// Drawn whether or not the Scene panel is open, so the scene target holds the scene when the panel opens again. Transforms are brought up to date again, since the UI may have changed the scene since OnUpdate
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

// The menu bar comes first, so the dock space fits in the space below it. Shortcuts are read from ImGui, since layer events are the scene's while the Viewport has them. The Game panel comes to the front as a copy starts playing, and the Scene panel as it stops
void ForgeLayer::OnImGuiRender()
{
    const bool l_Playing = m_Session.IsPlaying();
    if (l_Playing != std::exchange(m_WasPlaying, l_Playing))
    {
        if (l_Playing)
        {
            m_GamePanel->RequestFocus();
        }
        else
        {
            m_ViewportPanel->RequestFocus();
        }
    }

    PushPlayTint();
    // The Scene panel gives the scene its input back while it is hovered or focused, and a closed panel leaves it with none
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
    DrawNotice();
    ImGui::PopStyleColor(std::exchange(m_TintedColors, 0));
}

// Unity's notification: the session's last notice over the middle of the main window for a moment, fading as it goes
void ForgeLayer::DrawNotice()
{
    const std::optional<EditorSession::Notice>& l_Notice = m_Session.GetNotice();
    if (!l_Notice)
    {
        return;
    }

    const float l_Age = std::chrono::duration<float>(std::chrono::steady_clock::now() - l_Notice->Shown).count();
    if (l_Age >= c_NoticeSeconds)
    {
        return;
    }

    const float l_Opacity = std::clamp((c_NoticeSeconds - l_Age) / c_NoticeFadeSeconds, 0.0f, 1.0f);
    ImFont* l_Font = Trinity::ImGuiLayer::GetFont(Trinity::UIFont::Bold);
    const float l_Size = ImGui::GetFontSize() * c_NoticeScale;
    const ImVec2 l_TextSize = l_Font->CalcTextSizeA(l_Size, FLT_MAX, 0.0f, l_Notice->Text.c_str());
    const ImVec2 l_Padding(l_Size, l_Size * 0.6f);
    ImGuiViewport* l_Viewport = ImGui::GetMainViewport();
    const ImVec2 l_Centre = l_Viewport->GetWorkCenter();
    const ImVec2 l_Min(std::floor(l_Centre.x - l_TextSize.x * 0.5f - l_Padding.x), std::floor(l_Centre.y - l_TextSize.y * 0.5f - l_Padding.y));
    const ImVec2 l_Max(l_Min.x + l_TextSize.x + l_Padding.x * 2.0f, l_Min.y + l_TextSize.y + l_Padding.y * 2.0f);

    ImDrawList& l_DrawList = *ImGui::GetForegroundDrawList(l_Viewport);
    const float l_Rounding = ImGui::GetStyle().PopupRounding + l_Padding.y * 0.5f;
    l_DrawList.AddRectFilled(l_Min, l_Max, ImGui::GetColorU32(ImGuiCol_PopupBg, l_Opacity), l_Rounding);
    l_DrawList.AddRect(l_Min, l_Max, ImGui::GetColorU32(ImGuiCol_Border, l_Opacity), l_Rounding);
    l_DrawList.AddText(l_Font, l_Size, ImVec2(l_Min.x + l_Padding.x, l_Min.y + l_Padding.y), ImGui::GetColorU32(ImGuiCol_Text, l_Opacity), l_Notice->Text.c_str());
}

// While a copy plays, the backgrounds lean towards blue, further while it is paused, so it is plain at a glance that nothing done now stays
void ForgeLayer::PushPlayTint()
{
    if (!m_Session.IsPlaying())
    {
        return;
    }

    const float l_Amount = m_Session.IsPaused() ? c_PausedTint : c_PlayingTint;
    for (const ImGuiCol it_Color : c_TintedColors)
    {
        const ImVec4 l_Base = ImGui::GetStyleColorVec4(it_Color);
        ImGui::PushStyleColor(it_Color, ImVec4(l_Base.x + (c_PlayTint.x - l_Base.x) * l_Amount, l_Base.y + (c_PlayTint.y - l_Base.y) * l_Amount, l_Base.z + (c_PlayTint.z - l_Base.z) * l_Amount, l_Base.w));
        ++m_TintedColors;
    }
}

// Unsaved changes keep Forge open until the person has answered
bool ForgeLayer::OnCloseRequested()
{
    return m_Session.RequestClose();
}

// Not while a field is typed into, nor while anything is being dragged, such as a gizmo, whose command is still open, nor while a popup is up, such as the texture import's. Ctrl+P plays and stops, and while a copy plays, nothing edits the scene
void ForgeLayer::ReadShortcuts()
{
    if (ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive() || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
    {
        return;
    }

    const bool l_Playing = m_Session.IsPlaying();
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiKey_P))
    {
        m_Session.RequestStep();
    }
    else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_P))
    {
        m_Session.RequestPause();
    }
    else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_P) && m_Session.HasProject())
    {
        if (l_Playing)
        {
            m_Session.RequestStop();
        }
        else
        {
            m_Session.RequestPlay();
        }
    }
    else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_N))
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
    else if (l_Playing)
    {
        // Undo, Redo, Duplicate and Delete would edit the scene, and the history is locked while a copy plays
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

    if (ImGui::BeginMenu("Scripts"))
    {
        DrawScriptsMenu();

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
        if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_Eye, "Scene Stats").c_str(), nullptr, &l_ShowStats))
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

    DrawPlayControls();
    DrawBuildStatus();

    ImGui::EndMainMenuBar();
}

// At the right of the menu bar, one for each kind of script the project has: a spinner while it builds or waits to, red with its error count when the last build failed, which shows those errors in the Console when clicked, and green when it is up to date
void ForgeLayer::DrawBuildStatus()
{
    const Trinity::Project* l_Project = m_Session.GetProject();
    if (l_Project == nullptr)
    {
        return;
    }

    struct Indicator
    {
        const char* Label;
        const char* Name;
        BuildStatus Status;
        const std::vector<std::string>* ErrorLines;
    };

    std::vector<Indicator> l_Indicators;
    if (l_Project->HasModule() && ModuleHost::IsSupported())
    {
        l_Indicators.push_back({ "C++", "C++ module", m_Session.GetModule().GetStatus(), &m_Session.GetModule().GetErrorLines() });
    }

    if (l_Project->HasAssembly() && AssemblyHost::IsSupported())
    {
        l_Indicators.push_back({ "C#", "C# scripts", m_Session.GetAssembly().GetStatus(), &m_Session.GetAssembly().GetErrorLines() });
    }

    const auto a_Text = [](const Indicator& indicator)
    {
        switch (indicator.Status.State)
        {
            case BuildState::Pending:
            case BuildState::Building:
            {
                return std::format("{} {}", Trinity::Icons::c_Spinner, indicator.Label);
            }
            case BuildState::Failed:
            {
                return std::format("{} {} {}", Trinity::Icons::c_TimesCircle, indicator.Label, indicator.Status.Errors);
            }
            case BuildState::UpToDate:
            {
                return std::format("{} {}", Trinity::Icons::c_CheckCircle, indicator.Label);
            }
            case BuildState::None:
            {
                break;
            }
        }

        return std::format("{} {}", Trinity::Icons::c_FileCode, indicator.Label);
    };

    const ImGuiStyle& l_Style = ImGui::GetStyle();
    float l_Width = 0.0f;
    for (const Indicator& it_Indicator : l_Indicators)
    {
        l_Width += ImGui::CalcTextSize(a_Text(it_Indicator).c_str()).x + l_Style.FramePadding.x * 2.0f + l_Style.ItemSpacing.x;
    }

    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - l_Width - l_Style.WindowPadding.x));
    for (const Indicator& it_Indicator : l_Indicators)
    {
        const BuildStatus& l_Status = it_Indicator.Status;
        ImVec4 l_Color = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
        std::string l_Tooltip = std::format("The {} has not been built yet", it_Indicator.Name);
        switch (l_Status.State)
        {
            case BuildState::Pending:
            {
                l_Color = ImVec4(1.0f, 0.8f, 0.3f, 1.0f);
                l_Tooltip = std::format("The {} builds once its changes settle", it_Indicator.Name);

                break;
            }
            case BuildState::Building:
            {
                l_Color = ImVec4(1.0f, 0.8f, 0.3f, 1.0f);
                l_Tooltip = std::format("The {} is building", it_Indicator.Name);

                break;
            }
            case BuildState::Failed:
            {
                l_Color = ImVec4(1.0f, 0.45f, 0.4f, 1.0f);
                l_Tooltip = std::format("The {} failed to build, with {} error(s) and {} warning(s), and what ran before it runs on. Click to see the errors in the Console", it_Indicator.Name, l_Status.Errors, l_Status.Warnings);

                break;
            }
            case BuildState::UpToDate:
            {
                l_Color = ImVec4(0.45f, 0.85f, 0.45f, 1.0f);
                l_Tooltip = std::format("The {} is up to date, built with {} warning(s)", it_Indicator.Name, l_Status.Warnings);

                break;
            }
            case BuildState::None:
            {
                break;
            }
        }

        ImGui::PushStyleColor(ImGuiCol_Text, l_Color);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        const bool l_Clicked = ImGui::Button(std::format("{}###Build{}", a_Text(it_Indicator), it_Indicator.Label).c_str());
        ImGui::PopStyleColor(2);
        ImGui::SetItemTooltip("%s", l_Tooltip.c_str());
        if (l_Clicked && l_Status.State == BuildState::Failed)
        {
            m_Console->ShowBuildErrors(it_Indicator.Label, l_Status.FirstLine, l_Status.EndLine, *it_Indicator.ErrorLines);
        }

        ImGui::SameLine();
    }
}

// Centred in the menu bar: Play, which resumes a paused copy, Pause, held down while paused, Step, which pauses and takes one fixed step, and Stop
void ForgeLayer::DrawPlayControls()
{
    const bool l_Playing = m_Session.IsPlaying();
    const bool l_Paused = m_Session.IsPaused();
    const float l_Button = ImGui::GetFrameHeight();
    const float l_Width = l_Button * 4.0f + ImGui::GetStyle().ItemSpacing.x * 3.0f;
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), (ImGui::GetWindowWidth() - l_Width) * 0.5f));

    const auto a_Button = [l_Button](const char* icon, const char* id, bool enabled, bool held, const char* tooltip)
    {
        if (held)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }

        ImGui::BeginDisabled(!enabled);
        const bool l_Pressed = ImGui::Button(std::format("{}###{}", icon, id).c_str(), ImVec2(l_Button, 0.0f));
        ImGui::EndDisabled();
        if (held)
        {
            ImGui::PopStyleColor();
        }

        ImGui::SetItemTooltip("%s", tooltip);
        ImGui::SameLine();

        return l_Pressed;
    };

    // Held down while it waits for the scripts to build, as Unity's is while they compile, and saying why it is refused while a build has failed
    const bool l_Waiting = m_Session.IsWaitingForBuild();
    const std::string l_Failed = l_Playing ? std::string() : m_Session.GetFailedBuilds();
    const std::string l_PlayTip = l_Paused ? "Resume (Ctrl+Shift+P)" : l_Waiting ? "Plays once the scripts have built. Click to stop waiting (Ctrl+P)" : !l_Failed.empty() ? std::format("Play (Ctrl+P). Build errors in {} stop it until they are fixed", l_Failed) : "Play (Ctrl+P)";
    if (a_Button(Trinity::Icons::c_Play, "Play", m_Session.HasProject() && (!l_Playing || l_Paused), (l_Playing && !l_Paused) || l_Waiting, l_PlayTip.c_str()))
    {
        m_Session.RequestPlay();
    }

    if (a_Button(Trinity::Icons::c_Pause, "Pause", l_Playing, l_Paused, "Pause (Ctrl+Shift+P)"))
    {
        m_Session.RequestPause();
    }

    if (a_Button(Trinity::Icons::c_StepForward, "Step", l_Playing, false, "Step (Ctrl+Alt+P)"))
    {
        m_Session.RequestStep();
    }

    if (a_Button(Trinity::Icons::c_Stop, "Stop", l_Playing, false, "Stop (Ctrl+P)"))
    {
        m_Session.RequestStop();
    }
}

// Undo and Redo name the command they act on. Every edit to the scene goes through the history, so it can be undone, and the project's settings are saved as they change. While a copy plays, nothing here edits the scene
void ForgeLayer::DrawEditMenu()
{
    CommandStack& l_History = m_Session.GetHistory();
    const bool l_Playing = m_Session.IsPlaying();
    const bool l_CanUndo = !l_Playing && l_History.CanUndo();
    const bool l_CanRedo = !l_Playing && l_History.CanRedo();

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

    if (ImGui::BeginMenu(WithIcon(Trinity::Icons::c_History, "History").c_str(), !l_Playing && l_History.GetCount() > 0))
    {
        DrawHistoryMenu();

        ImGui::EndMenu();
    }

    ImGui::Separator();

    if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_CubeOutline, "Create Empty Entity").c_str(), nullptr, false, m_Session.HasProject() && !l_Playing))
    {
        l_History.Execute(Trinity::CreateScope<CreateEntityCommand>("Empty Entity", Trinity::UUID(), Trinity::UUID()));
    }

    if (ImGui::BeginMenu(WithIcon(Trinity::Icons::c_Cube, "Create 3D Object").c_str(), m_Session.HasProject() && !l_Playing))
    {
        for (const Trinity::BuiltInMesh it_Mesh : Trinity::c_BuiltInMeshes)
        {
            if (ImGui::MenuItem(std::string(Trinity::ToString(it_Mesh)).c_str()))
            {
                static_cast<void>(m_Session.CreatePrimitive(it_Mesh, {}));
            }
        }

        ImGui::EndMenu();
    }

    const bool l_HasSelection = !l_Playing && static_cast<bool>(m_Session.GetScene().FindEntityByUUID(m_Session.GetSelection()));
    if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_CubeOutline, "Duplicate").c_str(), "Ctrl+D", false, l_HasSelection))
    {
        l_History.Execute(Trinity::CreateScope<DuplicateEntityCommand>(m_Session.GetSelection()));
    }

    if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_Trash, "Delete").c_str(), "Delete", false, l_HasSelection))
    {
        l_History.Execute(Trinity::CreateScope<DeleteEntityCommand>(m_Session.GetSelection()));
    }

    ImGui::Separator();

    if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_Gear, "Project Settings...").c_str(), nullptr, false, m_Session.HasProject()))
    {
        m_ProjectSettings->SetOpen(true);
        m_ProjectSettings->RequestFocus();
    }
}

// A Forge with a static engine cannot build or load a module, and one without .NET has no C#, and each says why when its items are hovered
void ForgeLayer::DrawScriptsMenu()
{
    const Trinity::Project* l_Project = m_Session.GetProject();
    const ModuleHost& l_Module = m_Session.GetModule();
    const bool l_Supported = ModuleHost::IsSupported();
    const bool l_HasModule = l_Project != nullptr && l_Project->HasModule();
    const auto a_Explain = [l_Supported]
    {
        if (!l_Supported)
        {
            ImGui::SetItemTooltip("This Forge's engine is linked statically, so it cannot build or load C++ modules. Use a Debug or Release Forge");
        }
    };

    if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_File, "Create C++ Module").c_str(), nullptr, false, l_Supported && l_Project != nullptr && !l_HasModule))
    {
        m_Session.Request(EditorSession::Command::CreateModule);
    }

    a_Explain();
    if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_Refresh, l_Module.IsBuilding() ? "Building C++ Module..." : "Build C++ Module").c_str(), nullptr, false, l_Supported && l_HasModule && !l_Module.IsBuilding()))
    {
        m_Session.Request(EditorSession::Command::BuildModule);
    }

    a_Explain();
    if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_FileCode, "Open C++ Module in IDE").c_str(), nullptr, false, l_HasModule))
    {
        static_cast<void>(m_Session.OpenModuleFolder());
    }

    if (l_HasModule)
    {
        ImGui::TextDisabled("%s: %s", l_Project->GetModuleName().c_str(), l_Module.IsBuilding() ? "building" : l_Module.IsLoaded() ? "loaded" : "not loaded");
    }

    // C# works in every configuration, unless .NET did not start
    ImGui::Separator();
    const AssemblyHost& l_Assembly = m_Session.GetAssembly();
    const bool l_DotNet = AssemblyHost::IsSupported();
    const bool l_HasAssembly = l_Project != nullptr && l_Project->HasAssembly();
    const auto a_ExplainDotNet = [l_DotNet]
    {
        if (!l_DotNet)
        {
            ImGui::SetItemTooltip("C# is off, since .NET did not start. The log says why");
        }
    };

    if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_File, "Create C# Project").c_str(), nullptr, false, l_DotNet && l_Project != nullptr && !l_HasAssembly))
    {
        m_Session.Request(EditorSession::Command::CreateAssembly);
    }

    a_ExplainDotNet();
    if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_Refresh, l_Assembly.IsBuilding() ? "Building C# Project..." : "Build C# Project").c_str(), nullptr, false, l_DotNet && l_HasAssembly && !l_Assembly.IsBuilding()))
    {
        m_Session.Request(EditorSession::Command::BuildAssembly);
    }

    a_ExplainDotNet();
    if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_FileCode, "Open C# Solution in IDE").c_str(), nullptr, false, l_HasAssembly))
    {
        static_cast<void>(m_Session.OpenSolution());
    }

    if (l_HasAssembly)
    {
        ImGui::TextDisabled("%s: %s", l_Project->GetAssemblyName().c_str(), l_Assembly.IsBuilding() ? "building" : l_Assembly.IsLoaded() ? "loaded" : "not loaded");
    }

    // An IDE not found on this machine is greyed, and saying so is all its item does
    ImGui::Separator();
    if (ImGui::BeginMenu(WithIcon(Trinity::Icons::c_Gear, "Script Editor").c_str()))
    {
        for (const ScriptEditor it_Editor : c_ScriptEditors)
        {
            const bool l_Available = IdeLauncher::IsAvailable(it_Editor);
            if (ImGui::MenuItem(std::string(IdeLauncher::ToString(it_Editor)).c_str(), nullptr, m_Session.GetScriptEditor() == it_Editor, l_Available))
            {
                m_Session.SetScriptEditor(it_Editor);
                ImGui::MarkIniSettingsDirty();
            }

            if (!l_Available)
            {
                ImGui::SetItemTooltip("%s was not found on this machine", std::string(IdeLauncher::ToString(it_Editor)).c_str());
            }
        }

        ImGui::EndMenu();
    }

    // The script editor's debugger, attached to this Forge, whose process ID is shown for attaching one by hand
    ImGui::Separator();
    const std::string l_ProcessID = std::to_string(Trinity::Process::GetCurrentID());
    const bool l_CanAttach = l_Project != nullptr && m_Session.GetScriptEditor() != ScriptEditor::System;
    const std::string l_Attach = m_Session.GetScriptEditor() == ScriptEditor::System ? std::string("Attach Debugger") : std::format("Attach {} Debugger", IdeLauncher::ToString(m_Session.GetScriptEditor()));
    if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_Bug, l_Attach).c_str(), nullptr, false, l_CanAttach))
    {
        static_cast<void>(m_Session.AttachDebugger());
    }

    if (!l_CanAttach && l_Project != nullptr)
    {
        ImGui::SetItemTooltip("Pick Visual Studio, Rider or VS Code under Script Editor to attach its debugger, or attach one to process %s by hand", l_ProcessID.c_str());
    }

    bool l_Wait = m_Session.GetWaitForDebugger();
    if (ImGui::MenuItem("Wait for Debugger at Play", nullptr, &l_Wait))
    {
        m_Session.SetWaitForDebugger(l_Wait);
        ImGui::MarkIniSettingsDirty();
    }

    ImGui::SetItemTooltip("Play waits until a native or a managed debugger is attached, so breakpoints in OnCreate are hit");
    if (ImGui::MenuItem(std::format("Copy Process ID ({})", l_ProcessID).c_str()))
    {
        ImGui::SetClipboardText(l_ProcessID.c_str());
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

// A panel added to Forge after the user's imgui.ini was saved has no place in it, so it joins the node of a panel with the same slot rather than floating. A new tab would be selected as it appears, so the Scene and Properties panels are brought back in front of any that joins them
void ForgeLayer::DockNewPanels()
{
    for (const Trinity::Scope<Panel>& it_Panel : m_Panels)
    {
        if (ImGui::FindWindowSettingsByID(ImHashStr(it_Panel->GetWindowName().c_str())) != nullptr)
        {
            continue;
        }

        m_ViewportPanel->RequestFocus();
        m_PropertiesPanel->RequestFocus();

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

// Hierarchy on the left, Properties on the right in front of Project Settings, the Scene and Game panels as tabs in the centre with the Scene panel in front, and the Console below them. Every panel opens again, including any floating in windows of their own
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
    m_ViewportPanel->RequestFocus();
    m_PropertiesPanel->RequestFocus();
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

// imgui.ini holds one [ForgePanels][Open] section, with an ID=1 or ID=0 line for each panel, the Scene panel's own settings: its overlay, gizmo and snap steps, the Game panel's aspect choice and whether it shows Stats, the Content Browser's thumbnail size or list, the script editor, and whether Play waits for a debugger
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

    const std::string_view l_Key = l_Line.substr(0, l_Equals);
    const std::string_view l_Value = l_Line.substr(l_Equals + 1);
    ForgeLayer& l_Layer = *static_cast<ForgeLayer*>(entry);
    if (l_Layer.m_ViewportPanel->ReadSetting(l_Key, l_Value) || l_Layer.m_GamePanel->ReadSetting(l_Key, l_Value) || l_Layer.m_ContentBrowser->ReadSetting(l_Key, l_Value))
    {
        return;
    }

    if (l_Key == "ScriptEditor")
    {
        l_Layer.m_Session.SetScriptEditor(IdeLauncher::FromKey(l_Value).value_or(ScriptEditor::System));

        return;
    }

    if (l_Key == "WaitForDebugger")
    {
        l_Layer.m_Session.SetWaitForDebugger(l_Value == "1");

        return;
    }

    const bool l_On = l_Value != "0";
    for (const Trinity::Scope<Panel>& it_Panel : l_Layer.m_Panels)
    {
        if (it_Panel->GetSettingsID() == l_Key)
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
        buffer->appendf("%s=%d\n", it_Panel->GetSettingsID().c_str(), it_Panel->IsOpen() ? 1 : 0);
    }

    l_Layer.m_ViewportPanel->WriteSettings(*buffer);
    l_Layer.m_GamePanel->WriteSettings(*buffer);
    l_Layer.m_ContentBrowser->WriteSettings(*buffer);
    const std::string_view l_Editor = IdeLauncher::ToKey(l_Layer.m_Session.GetScriptEditor());
    buffer->appendf("ScriptEditor=%.*s\n", static_cast<int>(l_Editor.size()), l_Editor.data());
    buffer->appendf("WaitForDebugger=%d\n", l_Layer.m_Session.GetWaitForDebugger() ? 1 : 0);

    buffer->append("\n");
}
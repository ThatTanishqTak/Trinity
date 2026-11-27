#include "ForgeLayer.hpp"

#include "EditorCommands.hpp"
#include "HelpLinks.hpp"
#include "Layouts.hpp"
#include "Panels/ContentBrowserPanel.hpp"
#include "Panels/HierarchyPanel.hpp"
#include "Panels/PropertiesPanel.hpp"

#include <Trinity/UI/Widgets.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace
{
    // Hashed without the ID stack, so the dock space keeps its ID whatever ImGui window is current
    constexpr std::string_view c_DockSpaceName = "Forge dock space";
    constexpr const char* c_PanelSettingsName = "ForgePanels";
    constexpr std::size_t c_HistoryMenuEntries = 30;
    // The backgrounds lean this far towards the Play Mode Tint while a copy plays, further if paused
    constexpr float c_PlayingTint = 0.18f;
    constexpr float c_PausedTint = 0.3f;
    // A notice stays this long, fading over the end of it, in text this much larger than the UI's
    constexpr float c_NoticeSeconds = 2.5f;
    constexpr float c_NoticeFadeSeconds = 0.5f;
    constexpr float c_NoticeScale = 1.4f;
    // The status bar's progress bar, and how wide its tooltips wrap
    constexpr float c_StatusProgressWidthInFonts = 8.0f;
    constexpr float c_StatusTooltipWidthInFonts = 40.0f;
    constexpr const char* c_ExportPopup = "Exporting###ExportProgress";
    constexpr const char* c_SaveLayoutPopup = "Save Layout###ForgeSaveLayout";
    constexpr float c_ExportProgressWidthInFonts = 26.0f;
    constexpr std::array<ImGuiCol, 7> c_TintedColors{ ImGuiCol_WindowBg, ImGuiCol_PopupBg, ImGuiCol_MenuBarBg, ImGuiCol_TitleBg, ImGuiCol_TitleBgActive, ImGuiCol_TitleBgCollapsed, ImGuiCol_DockingEmptyBg };

    std::string WithIcon(const char* icon, std::string_view text)
    {
        return std::format("{} {}", icon, text);
    }

    // Forge's own window, or one a panel floats in
    bool IsForgeFocused()
    {
        if (Trinity::Application::Get().GetWindow().IsFocused())
        {
            return true;
        }

        const ImGuiPlatformIO& l_PlatformIO = ImGui::GetPlatformIO();
        if (l_PlatformIO.Platform_GetWindowFocus == nullptr)
        {
            return false;
        }

        return std::any_of(l_PlatformIO.Viewports.begin(), l_PlatformIO.Viewports.end(), [&l_PlatformIO](ImGuiViewport* viewport)
        {
            return viewport != ImGui::GetMainViewport() && viewport->PlatformWindowCreated && l_PlatformIO.Platform_GetWindowFocus(viewport);
        });
    }

    void TextLine(std::string_view text)
    {
        ImGui::TextUnformatted(text.data(), text.data() + text.size());
    }

    // A scene's name and its folder, as Main and Scenes for /assets/Scenes/Main.trscene
    std::pair<std::string_view, std::string_view> SplitScenePath(std::string_view path)
    {
        const std::size_t l_Slash = path.rfind('/');
        std::string_view l_Name = l_Slash == std::string_view::npos ? path : path.substr(l_Slash + 1);
        l_Name = l_Name.substr(0, l_Name.rfind('.'));
        const std::string_view l_Folder = l_Slash == std::string_view::npos ? std::string_view() : path.substr(0, l_Slash);
        const std::size_t l_Root = l_Folder.find('/', 1);

        return { l_Name, l_Root == std::string_view::npos ? std::string_view() : l_Folder.substr(l_Root + 1) };
    }

    // From the first primary camera in hierarchy order. Without one, exposure 1 and no curve
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

// Before the layout is saved for the last time, so it is saved with the centre where it belongs
void ForgeLayer::OnDetach()
{
    if (ImGui::GetCurrentContext() != nullptr)
    {
        SetMaximized(nullptr);
    }
}

void ForgeLayer::OnAttach()
{
    // Files dragged in from the system's file manager are imported, which OnEvent hears of
    Trinity::Application::Get().GetWindow().SetAcceptsFiles(true);

    const Trinity::ApplicationCommandLineArgs& l_Args = Trinity::Application::Get().GetSpecification().CommandLineArgs;
    if (const auto it_Directory = l_Args.GetOption("import-test"))
    {
        m_ImportTest.Start(it_Directory->empty() ? std::filesystem::temp_directory_path() / "Trinity-ImportTest" : std::filesystem::path(*it_Directory));
        m_Session.UpdateTitle();
    }
    else
    {
        m_Session.Start(l_Args);
        if (const auto it_Folder = l_Args.GetOption("export"); it_Folder && !it_Folder->empty())
        {
            m_Export.Start(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(it_Folder->data()), it_Folder->size())), true);
        }

        if (const auto it_File = l_Args.GetOption("capture"); it_File && !it_File->empty())
        {
            m_Capture.Start(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(it_File->data()), it_File->size())));
        }
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
    // A double-click or F in the Hierarchy frames in the Scene panel, which outlines search matches
    Trinity::Scope<HierarchyPanel> l_Hierarchy = Trinity::CreateScope<HierarchyPanel>(m_Session);
    m_HierarchyPanel = l_Hierarchy.get();
    m_HierarchyPanel->SetFrameSelection([this](bool follow) { m_ViewportPanel->RequestFrame(follow); });
    m_ViewportPanel->SetHighlighted([this] { return m_HierarchyPanel->IsOpen() ? m_HierarchyPanel->GetSearchMatches() : nullptr; });
    m_Panels.push_back(std::move(l_Hierarchy));
    Trinity::Scope<PropertiesPanel> l_Properties = Trinity::CreateScope<PropertiesPanel>(m_Session);
    m_PropertiesPanel = l_Properties.get();
    m_Panels.push_back(std::move(l_Properties));

    // A window of its own until it is docked. The Build panel is a tab behind Properties, and exports as --export does
    Trinity::Scope<ProjectSettingsPanel> l_ProjectSettings = Trinity::CreateScope<ProjectSettingsPanel>(m_Session);
    m_ProjectSettings = l_ProjectSettings.get();
    m_Panels.push_back(std::move(l_ProjectSettings));
    Trinity::Scope<BuildPanel> l_Build = Trinity::CreateScope<BuildPanel>(m_Session, m_Export);
    m_BuildPanel = l_Build.get();
    m_BuildPanel->SetShowPlayerSettings([this] { m_ProjectSettings->ShowSection(ProjectSettingsPanel::Section::Player); });
    m_Panels.push_back(std::move(l_Build));
    // A build's error opens where it is, in the script editor, on a double-click
    Trinity::Scope<ConsolePanel> l_Console = Trinity::CreateScope<ConsolePanel>();
    m_Console = l_Console.get();
    m_Console->SetOpenLocation([this](std::string_view path, std::uint32_t line) { static_cast<void>(m_Session.OpenScript(path, line)); });
    // A stopped script's badge in Properties brings the Console forward, to its exception
    m_PropertiesPanel->SetShowConsole([this]
    {
        m_Console->SetOpen(true);
        m_Console->RequestFocus();
    });
    // As a click on a warning's or an error's toast does, to all of what was logged
    m_Toasts.SetShowConsole([this]
    {
        m_Console->SetOpen(true);
        m_Console->RequestFocus();
    });
    m_Panels.push_back(std::move(l_Console));
    // The Console's Clear on Play and Clear on Build. A build beside one still running clears nothing
    m_Session.SetBeforePlay([this]
    {
        if (m_Console->IsClearingOnPlay())
        {
            Trinity::LogHistory::Clear();
        }
    });
    m_Session.SetBeforeBuild([this]
    {
        if (m_Console->IsClearingOnBuild() && !m_Session.GetModule().IsBuilding() && !m_Session.GetAssembly().IsBuilding())
        {
            Trinity::LogHistory::Clear();
        }
    });
    m_Session.SetAfterSceneSave([this] { m_Thumbnail.Request(); });

    // Beside the Console, wide enough for its three columns
    Trinity::Scope<InputActionsPanel> l_InputActions = Trinity::CreateScope<InputActionsPanel>(m_Session);
    m_InputActionsPanel = l_InputActions.get();
    m_Panels.push_back(std::move(l_InputActions));

    // Beside Input Actions, as wide, for its strips
    Trinity::Scope<AudioMixerPanel> l_AudioMixer = Trinity::CreateScope<AudioMixerPanel>(m_Session);
    m_AudioMixerPanel = l_AudioMixer.get();
    m_Panels.push_back(std::move(l_AudioMixer));

    // Beside the Audio Mixer, as wide, for its tree, its details and its curves
    Trinity::Scope<SoundEventPanel> l_SoundEvent = Trinity::CreateScope<SoundEventPanel>(m_Session);
    m_SoundEventPanel = l_SoundEvent.get();
    m_Panels.push_back(std::move(l_SoundEvent));

    // Docked after the Console, so it is the tab the default layout shows
    Trinity::Scope<ContentBrowserPanel> l_ContentBrowser = Trinity::CreateScope<ContentBrowserPanel>(m_Session);
    m_ContentBrowser = l_ContentBrowser.get();
    // Locate in Content Browser, on an asset slot of Properties, shows the asset there, bringing it forward
    m_PropertiesPanel->SetLocateAsset([this](Trinity::UUID asset)
    {
        if (m_ContentBrowser->Reveal(asset))
        {
            m_ContentBrowser->SetOpen(true);
            m_ContentBrowser->RequestFocus();
        }
    });
    // Find References In Scene shows its finds in the Hierarchy's search, bringing it forward
    m_ContentBrowser->SetFindReferences([this](std::string search)
    {
        m_HierarchyPanel->SetSearch(std::move(search));
        m_HierarchyPanel->SetOpen(true);
        m_HierarchyPanel->RequestFocus();
    });
    // An input actions asset opens in its panel, brought forward
    m_ContentBrowser->SetOpenInputActions([this](Trinity::UUID asset)
    {
        m_InputActionsPanel->SetAsset(asset);
        m_InputActionsPanel->SetOpen(true);
        m_InputActionsPanel->RequestFocus();
    });
    // As is an audio mixer
    m_ContentBrowser->SetOpenAudioMixer([this](Trinity::UUID asset)
    {
        m_AudioMixerPanel->SetAsset(asset);
        m_AudioMixerPanel->SetOpen(true);
        m_AudioMixerPanel->RequestFocus();
    });
    // As is a sound event
    m_ContentBrowser->SetOpenSoundEvent([this](Trinity::UUID asset)
    {
        m_SoundEventPanel->SetAsset(asset);
        m_SoundEventPanel->SetOpen(true);
        m_SoundEventPanel->RequestFocus();
    });
    m_Panels.push_back(std::move(l_ContentBrowser));

    // ImGui reads imgui.ini before its first frame, which is after this, so panels get their state
    ImGuiSettingsHandler l_Handler;
    l_Handler.TypeName = c_PanelSettingsName;
    l_Handler.TypeHash = ImHashStr(c_PanelSettingsName);
    l_Handler.ReadOpenFn = &ForgeLayer::OpenPanelSettings;
    l_Handler.ReadLineFn = &ForgeLayer::ReadPanelSetting;
    l_Handler.WriteAllFn = &ForgeLayer::WritePanelSettings;
    l_Handler.UserData = this;
    ImGui::AddSettingsHandler(&l_Handler);
    RegisterShortcuts();
}

// Before ImGui's frame begins, so a native file dialog never blocks with a frame open
void ForgeLayer::OnUpdate(Trinity::Timestep timestep)
{
    // Here, before ImGui's frame begins, which is where its settings may be loaded again
    if (const std::optional<std::string> l_Layout = std::exchange(m_PendingLayout, std::nullopt))
    {
        ApplyLayout(*l_Layout);
    }

    m_Session.Update();
    m_ImportTest.Update();
    m_Export.Update();
    m_Capture.Update();
    m_Thumbnail.Update();
    m_StartScreen.Update();
    m_TitleBar.Update(Trinity::Application::Get().GetWindow(), m_Session.GetPreferences().CustomTitleBar);
    m_Session.UpdatePlay(timestep);
    m_Session.GetScene().UpdateWorldTransforms();
    m_GamePanel->ShowUI();
    m_ViewportPanel->ShowUI();
    if (Trinity::AudioEngine* l_Audio = Trinity::Application::Get().GetAudioEngine(); l_Audio != nullptr)
    {
        l_Audio->SetMuted((m_Session.IsPlaying() && m_GamePanel->IsMutingAudio()) || (m_Session.GetPreferences().MuteWhenUnfocused && !IsForgeFocused()));
    }
}

// Files may have changed while another program had focus
void ForgeLayer::OnEvent(Trinity::Event& event)
{
    if (event.GetEventType() == Trinity::EventType::WindowFocus)
    {
        m_Session.Request(EditorSession::Command::Refresh);
    }
    else if (event.GetEventType() == Trinity::EventType::FilesDropped && m_Session.HasProject())
    {
        // Let go anywhere over Forge, they go into the folder the Content Browser has open, which then shows them
        m_Session.RequestImportFiles(static_cast<const Trinity::FilesDroppedEvent&>(event).GetPaths(), m_ContentBrowser->GetFolder(), [this](const std::vector<std::string>& imported)
        {
            m_ContentBrowser->RevealImported(imported);
            m_ContentBrowser->SetOpen(true);
            m_ContentBrowser->RequestFocus();
        });
    }
    else if (event.GetEventType() == Trinity::EventType::FilesDropped)
    {
        TR_WARN("Forge: files can be dropped in once a project is open");
    }
}

// After the UI and before the Renderer builds its graph, so a dragged exposure shows this frame
void ForgeLayer::OnPrepareRender([[maybe_unused]] Trinity::RHI::CommandList& commands)
{
    m_Session.GetScene().UpdateWorldTransforms();
    // In prefab mode the Game panel goes on showing the scene
    if (Trinity::Scene& l_Main = m_Session.GetMainScene(); &l_Main != &m_Session.GetScene())
    {
        l_Main.UpdateWorldTransforms();
    }

    Trinity::Application::Get().GetRenderer().SetToneMapping(GetSceneToneMapping(m_Session.GetScene()));
    m_ViewportPanel->PrepareScene();
    m_GamePanel->PrepareScene();
    m_Capture.Prepare();
    if (m_Thumbnail.IsDrawing())
    {
        m_Thumbnail.Prepare(m_ViewportPanel->GetCamera(glm::vec2(static_cast<float>(SceneCapture::c_Width), static_cast<float>(SceneCapture::c_Height))));
    }

    m_ContentBrowser->PrepareThumbnails();
}

// Drawn whether or not the Scene panel is open, so the scene target is current when it reopens
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

// The menu bar and the toolbar come first, so the dock space fits below them. Shortcuts are read from ImGui
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

    PauseOnError();
    PushPlayTint();
    // The Scene panel gives the scene its input back while hovered or focused. Closed, it gives none
    m_ImGui.SetSceneInput(false, false);

    ReadShortcuts();
    DrawMenuBar();
    DrawToolbar();
    DrawStatusBar();
    DrawDockSpace();

    // A maximised panel is restored as it is closed, and as another is asked to come forward, which it could not do unseen
    if (m_Maximized != nullptr && (!m_Maximized->IsOpen() || std::ranges::any_of(m_Panels, [this](const Trinity::Scope<Panel>& panel) { return panel.get() != m_Maximized && panel->IsFocusRequested(); })))
    {
        SetMaximized(nullptr);
    }

    // The layout is not saved while a panel is maximised, so a crash then leaves the file as it was before
    if (m_Maximized != nullptr)
    {
        ImGui::GetIO().WantSaveIniSettings = false;
    }

    // While one is maximised the others are left out, so its dock node has the whole dock space, and each is back in its own once drawn again
    for (const Trinity::Scope<Panel>& it_Panel : m_Panels)
    {
        if (m_Maximized == nullptr || it_Panel.get() == m_Maximized)
        {
            it_Panel->Draw();
        }
        else
        {
            it_Panel->Skip();
        }
    }

    m_Toasts.Draw();

    // The scene reads the keyboard and mouse as the Game panel lets it, and gamepads with focus
    Trinity::SceneInputFilter l_SceneInput = m_GamePanel->TakeSceneFilter();
    l_SceneInput.Gamepads = !m_InputActionsPanel->IsListening();
    Trinity::Input::SetSceneFilter(l_SceneInput);

    m_ContentBrowser->EndFrame();
    m_PropertiesPanel->DrawPopups();
    m_StartScreen.Draw();
    m_PreferencesWindow.Draw();
    m_ShortcutsWindow.Draw();
    m_CommandPalette.Draw();

    if (m_ShowAbout)
    {
        DrawAboutWindow();
    }

    if (m_ShowDemoWindow)
    {
        ImGui::ShowDemoWindow(&m_ShowDemoWindow);
    }

    m_Session.DrawPopups();
    DrawExportPopup();
    DrawSaveLayoutPopup();
    m_Session.UpdateTitle();
    DrawNotice();
    ImGui::PopStyleColor(std::exchange(m_TintedColors, 0));
}

// Over everything while an export runs, as Unity's build progress. Cancel keeps the last export
void ForgeLayer::DrawExportPopup()
{
    const bool l_Running = m_Export.IsRunning();
    if (!l_Running || (m_ExportPopupOpen && !ImGui::IsPopupOpen(c_ExportPopup)))
    {
        m_ExportPopupOpen = false;
    }

    if (l_Running && !m_ExportPopupOpen && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
    {
        ImGui::OpenPopup(c_ExportPopup);
        m_ExportPopupOpen = true;
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(c_ExportPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        return;
    }

    if (!l_Running)
    {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();

        return;
    }

    ImGui::TextUnformatted(m_Export.GetStatus().c_str());
    const std::optional<float> l_Progress = m_Export.GetProgress();
    const std::string l_Percent = l_Progress ? std::format("{:.0f}%", *l_Progress * 100.0f) : std::string();
    ImGui::ProgressBar(l_Progress ? *l_Progress : -static_cast<float>(ImGui::GetTime()), ImVec2(ImGui::GetFontSize() * c_ExportProgressWidthInFonts, 0.0f), l_Percent.c_str());
    ImGui::TextDisabled("%.0f s elapsed. The Console tells how it goes", m_Export.GetSeconds());
    ImGui::Spacing();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        m_Export.Cancel();
    }

    ImGui::EndPopup();
}

// From the File menu or Ctrl+B. The Build panel comes forward when the settings stop it
void ForgeLayer::BuildAndRun()
{
    if (!m_BuildPanel->Build(true))
    {
        m_BuildPanel->SetOpen(true);
        m_BuildPanel->RequestFocus();
    }
}

// Unity's status bar: the last line logged at Info or above, and at the right background work
void ForgeLayer::DrawStatusBar()
{
    ReadStatusLine();
    const std::vector<EditorSession::Activity> l_Activities = m_Session.GetActivities();

    const ImGuiWindowFlags l_Flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
    if (ImGui::BeginViewportSideBar("##ForgeStatusBar", ImGui::GetMainViewport(), ImGuiDir_Down, ImGui::GetFrameHeight(), l_Flags) && ImGui::BeginMenuBar())
    {
        const ImGuiStyle& l_Style = ImGui::GetStyle();
        const float l_LineHeight = ImGui::GetTextLineHeight();

        // The first activity, with how many more there are, measured first so the line takes what is left
        std::string l_Activity;
        if (!l_Activities.empty())
        {
            l_Activity = l_Activities.size() > 1 ? std::format("{} (+{} more)", l_Activities.front().Text, l_Activities.size() - 1) : l_Activities.front().Text;
        }

        const float l_BarWidth = ImGui::GetFontSize() * c_StatusProgressWidthInFonts;
        const float l_ActivityWidth = l_Activity.empty() ? 0.0f : ImGui::CalcTextSize(l_Activity.c_str()).x + l_BarWidth + l_Style.ItemSpacing.x * 2.0f;
        const ImVec2 l_Start = ImGui::GetCursorScreenPos();
        const float l_LineWidth = std::max(ImGui::GetContentRegionAvail().x - l_ActivityWidth, 1.0f);

        ImGui::InvisibleButton("##StatusLine", ImVec2(l_LineWidth, l_LineHeight));
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        {
            m_Console->SetOpen(true);
            m_Console->RequestFocus();
        }

        if (!m_StatusText.empty())
        {
            const char* l_Icon = m_StatusLevel >= Trinity::LogLevel::Error ? Trinity::Icons::c_TimesCircle : m_StatusLevel == Trinity::LogLevel::Warn ? Trinity::Icons::c_Warning : Trinity::Icons::c_InfoCircle;
            const std::string l_Line = std::format("{} {}", l_Icon, std::string_view(m_StatusText).substr(0, m_StatusText.find('\n')));
            const float l_MaxX = l_Start.x + l_LineWidth;
            ImGui::PushStyleColor(ImGuiCol_Text, ConsolePanel::GetLevelColor(m_StatusLevel));
            ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), l_Start, ImVec2(l_MaxX, l_Start.y + l_LineHeight), l_MaxX, l_Line.c_str(), nullptr, nullptr);
            ImGui::PopStyleColor();

            if (ImGui::BeginItemTooltip())
            {
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * c_StatusTooltipWidthInFonts);
                ImGui::TextUnformatted(m_StatusText.c_str());
                ImGui::PopTextWrapPos();
                ImGui::TextDisabled("Click to show the Console");
                ImGui::EndTooltip();
            }
        }

        if (!l_Activity.empty())
        {
            const std::optional<float> l_Progress = l_Activities.front().Progress;
            ImGui::SetCursorScreenPos(ImVec2(l_Start.x + l_LineWidth + l_Style.ItemSpacing.x, l_Start.y));
            ImGui::BeginGroup();
            ImGui::TextUnformatted(l_Activity.c_str());
            ImGui::SameLine();
            const std::string l_Percent = l_Progress ? std::format("{:.0f}%", *l_Progress * 100.0f) : std::string();
            ImGui::ProgressBar(l_Progress ? *l_Progress : -static_cast<float>(ImGui::GetTime()), ImVec2(l_BarWidth, l_LineHeight), l_Percent.c_str());
            ImGui::EndGroup();

            if (ImGui::BeginItemTooltip())
            {
                for (const EditorSession::Activity& it_Activity : l_Activities)
                {
                    ImGui::TextUnformatted(it_Activity.Progress ? std::format("{}, {:.0f}%", it_Activity.Text, *it_Activity.Progress * 100.0f).c_str() : it_Activity.Text.c_str());
                }

                ImGui::EndTooltip();
            }
        }

        ImGui::EndMenuBar();
    }

    ImGui::End();
}

// The history is let go of before drawing, since drawing may log. Nothing found empties the bar
void ForgeLayer::ReadStatusLine()
{
    const Trinity::LogHistory::Reader l_History = Trinity::LogHistory::Read();
    for (std::size_t it_Index = l_History.GetCount(); it_Index > 0; --it_Index)
    {
        const Trinity::LogEntry& l_Entry = l_History[it_Index - 1];
        if (l_Entry.Level == Trinity::LogLevel::Trace)
        {
            continue;
        }

        if (l_Entry.Sequence != m_StatusSequence || m_StatusText.empty())
        {
            m_StatusSequence = l_Entry.Sequence;
            m_StatusLevel = l_Entry.Level;
            m_StatusText.assign(l_Entry.Text);
        }

        return;
    }

    m_StatusText.clear();
    m_StatusSequence = 0;
}

// As Unity's Error Pause. Lines logged while nothing plays are skipped, so old errors never pause
void ForgeLayer::PauseOnError()
{
    if (!m_Console->IsPausingOnError() || !m_Session.IsPlaying() || m_Session.IsPaused())
    {
        m_ErrorScanSequence = Trinity::LogHistory::GetNextSequence();

        return;
    }

    if (Trinity::LogHistory::GetNextSequence() == m_ErrorScanSequence)
    {
        return;
    }

    bool l_Error = false;
    {
        const Trinity::LogHistory::Reader l_History = Trinity::LogHistory::Read();
        for (std::size_t it_Index = l_History.GetCount(); it_Index > 0 && l_History[it_Index - 1].Sequence >= m_ErrorScanSequence && !l_Error; --it_Index)
        {
            l_Error = l_History[it_Index - 1].Level >= Trinity::LogLevel::Error;
        }

        // Read again under the lock every line and clear takes, so nothing logged meanwhile is missed
        m_ErrorScanSequence = Trinity::LogHistory::GetNextSequence();
    }

    if (l_Error)
    {
        m_Session.RequestPause();
    }
}

// Unity's notification: the session's last notice over the middle of the main window, fading
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

// While a copy plays, the backgrounds lean towards the Play Mode Tint, further while paused
void ForgeLayer::PushPlayTint()
{
    if (!m_Session.IsPlaying())
    {
        return;
    }

    const float l_Amount = m_Session.IsPaused() ? c_PausedTint : c_PlayingTint;
    const glm::vec3 l_Tint = m_Session.GetPreferences().PlayModeTint;
    for (const ImGuiCol it_Color : c_TintedColors)
    {
        const ImVec4 l_Base = ImGui::GetStyleColorVec4(it_Color);
        ImGui::PushStyleColor(it_Color, ImVec4(l_Base.x + (l_Tint.x - l_Base.x) * l_Amount, l_Base.y + (l_Tint.y - l_Base.y) * l_Amount, l_Base.z + (l_Tint.z - l_Base.z) * l_Amount, l_Base.w));
        ++m_TintedColors;
    }
}

// Unsaved changes keep Forge open until the person has answered
bool ForgeLayer::OnCloseRequested()
{
    return m_Session.RequestClose();
}

// Every command a key runs, with the key it starts on. Each is ready by what its menu item is greyed by, and Duplicate, Delete and Paste are left to a panel that lists things of its own while it has the focus
void ForgeLayer::RegisterShortcuts()
{
    Shortcuts& l_Shortcuts = m_Session.GetShortcuts();
    const auto a_Request = [this](EditorSession::Command command) { return [this, command] { m_Session.Request(command); }; };
    const auto a_HasProject = [this] { return m_Session.HasProject(); };
    const auto a_HasSelection = [this] { return static_cast<bool>(m_Session.GetScene().FindEntityByUUID(m_Session.GetSelection())); };
    const auto a_OwnsEditKeys = [this] { return m_ContentBrowser->IsFocused() || m_InputActionsPanel->IsFocused() || m_AudioMixerPanel->IsFocused() || m_SoundEventPanel->IsFocused(); };
    // Not while a copy plays, which nothing may change, nor from a panel with its own Duplicate, Delete and Paste
    const auto a_EditsScene = [this, a_OwnsEditKeys] { return !m_Session.IsPlaying() && !a_OwnsEditKeys(); };

    l_Shortcuts.Add({ .ID = "file.newScene", .Category = "File", .Label = "New Scene", .Chord = ImGuiMod_Ctrl | ImGuiKey_N, .Run = a_Request(EditorSession::Command::NewScene) });
    l_Shortcuts.Add({ .ID = "file.openScene", .Category = "File", .Label = "Open Scene", .Chord = ImGuiMod_Ctrl | ImGuiKey_O, .Run = a_Request(EditorSession::Command::OpenScene) });
    l_Shortcuts.Add({ .ID = "file.save", .Category = "File", .Label = "Save", .Chord = ImGuiMod_Ctrl | ImGuiKey_S, .Run = a_Request(EditorSession::Command::SaveScene) });
    l_Shortcuts.Add({ .ID = "file.saveAs", .Category = "File", .Label = "Save As", .Chord = ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, .Run = a_Request(EditorSession::Command::SaveSceneAs) });
    l_Shortcuts.Add({ .ID = "file.refresh", .Category = "File", .Label = "Refresh", .Chord = ImGuiKey_F5, .Run = a_Request(EditorSession::Command::Refresh) });
    l_Shortcuts.Add({ .ID = "file.buildSettings", .Category = "File", .Label = "Build Settings", .Chord = ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_B, .IsEnabled = a_HasProject, .Run = [this]
    {
        m_BuildPanel->SetOpen(true);
        m_BuildPanel->RequestFocus();
    } });
    l_Shortcuts.Add({ .ID = "file.buildAndRun", .Category = "File", .Label = "Build And Run", .Chord = ImGuiMod_Ctrl | ImGuiKey_B, .IsEnabled = a_HasProject, .Run = [this] { BuildAndRun(); } });

    l_Shortcuts.Add({ .ID = "edit.undo", .Category = "Edit", .Label = "Undo", .Chord = ImGuiMod_Ctrl | ImGuiKey_Z, .Repeats = true, .Run = [this] { m_Session.GetHistory().Undo(); } });
    l_Shortcuts.Add({ .ID = "edit.redo", .Category = "Edit", .Label = "Redo", .Chord = ImGuiMod_Ctrl | ImGuiKey_Y, .Alternate = ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, .Repeats = true, .Run = [this] { m_Session.GetHistory().Redo(); } });
    l_Shortcuts.Add({ .ID = "edit.copy", .Category = "Edit", .Label = "Copy", .Chord = ImGuiMod_Ctrl | ImGuiKey_C, .IsEnabled = [this, a_HasSelection] { return !m_ContentBrowser->IsFocused() && a_HasSelection(); }, .Run = [this] { m_Session.CopySelection(); } });
    l_Shortcuts.Add({ .ID = "edit.paste", .Category = "Edit", .Label = "Paste", .Chord = ImGuiMod_Ctrl | ImGuiKey_V, .IsEnabled = [this, a_EditsScene] { return a_EditsScene() && m_Session.HasCopiedEntity(); }, .Run = [this] { static_cast<void>(m_Session.PasteEntity()); } });
    l_Shortcuts.Add({ .ID = "edit.pasteAsChild", .Category = "Edit", .Label = "Paste As Child", .Chord = ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_V, .IsEnabled = [this, a_EditsScene, a_HasSelection] { return a_EditsScene() && m_Session.HasCopiedEntity() && a_HasSelection(); }, .Run = [this] { static_cast<void>(m_Session.PasteEntityAsChild(m_Session.GetSelection())); } });
    l_Shortcuts.Add({ .ID = "edit.duplicate", .Category = "Edit", .Label = "Duplicate", .Chord = ImGuiMod_Ctrl | ImGuiKey_D, .IsEnabled = [a_EditsScene, a_HasSelection] { return a_EditsScene() && a_HasSelection(); }, .Run = [this] { static_cast<void>(m_Session.DuplicateSelection()); } });
    l_Shortcuts.Add({ .ID = "edit.delete", .Category = "Edit", .Label = "Delete", .Chord = ImGuiKey_Delete, .IsEnabled = [a_EditsScene, a_HasSelection] { return a_EditsScene() && a_HasSelection(); }, .Run = [this] { static_cast<void>(m_Session.DeleteSelection()); } });
    l_Shortcuts.Add({ .ID = "edit.selectAll", .Category = "Edit", .Label = "Select All", .Chord = ImGuiMod_Ctrl | ImGuiKey_A, .IsEnabled = [a_HasProject, a_OwnsEditKeys] { return a_HasProject() && !a_OwnsEditKeys(); }, .Run = [this] { m_HierarchyPanel->SelectAll(); } });
    l_Shortcuts.Add({ .ID = "edit.preferences", .Category = "Edit", .Label = "Preferences", .Chord = ImGuiMod_Ctrl | ImGuiKey_Comma, .Run = [this] { m_PreferencesWindow.Open(); } });

    l_Shortcuts.Add({ .ID = "entity.createEmpty", .Category = "Entity", .Label = "Create Empty", .Chord = ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_N, .IsEnabled = [this] { return !m_Session.IsPlaying() && m_Session.HasProject(); }, .Run = [this] { CreateEmpty({}); } });
    l_Shortcuts.Add({ .ID = "entity.createEmptyChild", .Category = "Entity", .Label = "Create Empty Child", .Chord = ImGuiMod_Alt | ImGuiMod_Shift | ImGuiKey_N, .IsEnabled = [this, a_HasSelection] { return !m_Session.IsPlaying() && a_HasSelection(); }, .Run = [this] { CreateEmpty(m_Session.GetSelection()); } });

    l_Shortcuts.Add({ .ID = "play.toggle", .Category = "Play", .Label = "Play or Stop", .Chord = ImGuiMod_Ctrl | ImGuiKey_P, .IsEnabled = a_HasProject, .Run = [this]
    {
        if (m_Session.IsPlaying())
        {
            m_Session.RequestStop();
        }
        else
        {
            m_Session.RequestPlay();
        }
    } });
    l_Shortcuts.Add({ .ID = "play.pause", .Category = "Play", .Label = "Pause or Resume", .Chord = ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_P, .Run = [this] { m_Session.RequestPause(); } });
    l_Shortcuts.Add({ .ID = "play.step", .Category = "Play", .Label = "Step", .Chord = ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiKey_P, .Run = [this] { m_Session.RequestStep(); } });

    l_Shortcuts.Add({ .ID = "help.palette", .Category = "Help", .Label = "Command Palette", .Chord = ImGuiMod_Ctrl | ImGuiKey_K, .Run = [this] { m_CommandPalette.Open(); } });
    l_Shortcuts.Add({ .ID = "help.shortcuts", .Category = "Help", .Label = "Keyboard Shortcuts", .Chord = ImGuiMod_Ctrl | ImGuiKey_Slash, .Run = [this] { m_ShortcutsWindow.Open(); } });
    l_Shortcuts.Add({ .ID = "help.demo", .Category = "Help", .Label = "ImGui Demo", .Chord = ImGuiKey_F1, .Run = [this]
    {
        m_ShowDemoWindow = !m_ShowDemoWindow;
        TR_INFO("Forge: demo window {}", m_ShowDemoWindow ? "shown" : "hidden");
    } });
    l_Shortcuts.Add({ .ID = "help.documentation", .Category = "Help", .Label = "Documentation", .Run = [] { HelpLinks::Open(HelpLinks::c_Documentation); } });
    l_Shortcuts.Add({ .ID = "help.releaseNotes", .Category = "Help", .Label = "Release Notes", .Run = [] { HelpLinks::Open(HelpLinks::c_ReleaseNotes); } });
    l_Shortcuts.Add({ .ID = "help.reportBug", .Category = "Help", .Label = "Report a Bug", .Run = [] { HelpLinks::Open(HelpLinks::c_ReportBug); } });

    // What a menu does and no key has had, which the Command Palette runs by name, and which can be given a key as the rest can
    l_Shortcuts.Add({ .ID = "file.newProject", .Category = "File", .Label = "New Project", .Run = a_Request(EditorSession::Command::NewProject) });
    l_Shortcuts.Add({ .ID = "file.openProject", .Category = "File", .Label = "Open Project", .Run = a_Request(EditorSession::Command::OpenProject) });
    l_Shortcuts.Add({ .ID = "file.setStartScene", .Category = "File", .Label = "Set as Start Scene", .IsEnabled = [this] { return m_Session.HasProject() && m_Session.HasScenePath(); }, .Run = a_Request(EditorSession::Command::SetStartScene) });
    l_Shortcuts.Add({ .ID = "edit.projectSettings", .Category = "Edit", .Label = "Project Settings", .IsEnabled = a_HasProject, .Run = [this]
    {
        m_ProjectSettings->SetOpen(true);
        m_ProjectSettings->RequestFocus();
    } });

    const auto a_CreatesEntity = [this] { return m_Session.HasProject() && !m_Session.IsPlaying(); };
    // A saved key names a command by its ID, which for these is the label without its spaces
    const auto a_MakeID = [](std::string_view prefix, std::string_view label)
    {
        std::string l_ID(prefix);
        std::ranges::copy_if(label, std::back_inserter(l_ID), [](char character) { return character != ' '; });

        return l_ID;
    };
    for (const Trinity::BuiltInMesh it_Mesh : Trinity::c_BuiltInMeshes)
    {
        l_Shortcuts.Add({ .ID = a_MakeID("entity.create", Trinity::ToString(it_Mesh)), .Category = "Entity", .Label = std::format("Create {}", Trinity::ToString(it_Mesh)), .IsEnabled = a_CreatesEntity, .Run = [this, it_Mesh] { static_cast<void>(m_Session.CreatePrimitive(it_Mesh, {})); } });
    }

    for (const Trinity::LightType it_Type : c_LightTypes)
    {
        l_Shortcuts.Add({ .ID = a_MakeID("entity.create", GetLightLabel(it_Type)), .Category = "Entity", .Label = std::format("Create {}", GetLightLabel(it_Type)), .IsEnabled = a_CreatesEntity, .Run = [this, it_Type] { static_cast<void>(m_Session.CreateLight(it_Type, {})); } });
    }

    l_Shortcuts.Add({ .ID = "entity.createCamera", .Category = "Entity", .Label = "Create Camera", .IsEnabled = a_CreatesEntity, .Run = [this] { static_cast<void>(m_Session.CreateCamera({})); } });

    // Each panel by the name its tab has, shown and brought forward
    for (const Trinity::Scope<Panel>& it_Panel : m_Panels)
    {
        Panel* l_Panel = it_Panel.get();
        l_Shortcuts.Add({ .ID = a_MakeID("window.", l_Panel->GetSettingsID()), .Category = "Window", .Label = std::format("Show {}", l_Panel->GetTitle()), .Run = [l_Panel]
        {
            l_Panel->SetOpen(true);
            l_Panel->RequestFocus();
        } });
    }

    // Unity's key for it. Not while the Game panel has the focus of a scene that plays, whose own keys these may be
    l_Shortcuts.Add({ .ID = "window.maximize", .Category = "Window", .Label = "Maximize or Restore Panel", .Chord = ImGuiMod_Shift | ImGuiKey_Space, .IsEnabled = [this] { return !(m_Session.IsPlaying() && m_GamePanel->IsWindowFocused()); }, .Run = [this] { ToggleMaximized(); } });
    l_Shortcuts.Add({ .ID = "window.resetLayout", .Category = "Window", .Label = "Reset Layout", .Run = [this]
    {
        SetMaximized(nullptr);
        m_ResetLayout = true;
    } });
    l_Shortcuts.Add({ .ID = "window.saveLayout", .Category = "Window", .Label = "Save Layout As", .Run = [this]
    {
        m_LayoutName.clear();
        m_OpenSaveLayoutPopup = true;
    } });
    l_Shortcuts.Add({ .ID = "window.theme", .Category = "Window", .Label = "Toggle Theme", .Run = [this] { m_Session.SetPreferenceVariable("ui.theme", Trinity::Theme::IsLight() ? Trinity::Theme::c_Dark : Trinity::Theme::c_Light); } });

    // Unreal's keys for them: a number goes to the bookmark and Ctrl with it saves one, while the mouse is over the Scene panel or it has the focus. Not while the Game panel has the focus of a scene that plays, whose own keys the numbers may be
    const auto a_OverScene = [this] { return !(m_Session.IsPlaying() && m_GamePanel->IsWindowFocused()) && (m_ViewportPanel->IsWindowHovered() || m_ViewportPanel->IsWindowFocused()); };
    for (std::size_t it_Index = 0; it_Index < ViewportPanel::c_BookmarkCount; ++it_Index)
    {
        const ImGuiKey l_Key = static_cast<ImGuiKey>(ImGuiKey_1 + static_cast<int>(it_Index));
        l_Shortcuts.Add({ .ID = std::format("bookmark.go{}", it_Index + 1), .Category = "Bookmarks", .Label = std::format("Go to Bookmark {}", it_Index + 1), .Chord = l_Key, .IsEnabled = a_HasProject, .IsKeyInScope = a_OverScene, .Run = [this, it_Index] { m_ViewportPanel->GoToBookmark(it_Index); } });
    }

    for (std::size_t it_Index = 0; it_Index < ViewportPanel::c_BookmarkCount; ++it_Index)
    {
        const ImGuiKey l_Key = static_cast<ImGuiKey>(ImGuiKey_1 + static_cast<int>(it_Index));
        l_Shortcuts.Add({ .ID = std::format("bookmark.save{}", it_Index + 1), .Category = "Bookmarks", .Label = std::format("Save Bookmark {}", it_Index + 1), .Chord = ImGuiMod_Ctrl | l_Key, .IsEnabled = a_HasProject, .IsKeyInScope = a_OverScene, .Run = [this, it_Index] { m_ViewportPanel->SaveBookmark(it_Index); } });
    }

    // Last, so each key someone changed finds the command it was changed for
    l_Shortcuts.ReadKeys();
}

// Each saved layout by name, which a click changes to, then saving the arrangement as it is and taking a saved one away
void ForgeLayer::DrawLayoutMenu()
{
    const std::vector<std::string> l_Names = Layouts::List();
    for (const std::string& it_Name : l_Names)
    {
        if (ImGui::MenuItem(it_Name.c_str()))
        {
            m_PendingLayout = it_Name;
        }
    }

    if (l_Names.empty())
    {
        ImGui::TextDisabled("No layout saved yet");
    }

    ImGui::Separator();
    if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_Save, "Save Layout As...").c_str()))
    {
        m_LayoutName.clear();
        m_OpenSaveLayoutPopup = true;
    }

    if (ImGui::BeginMenu(WithIcon(Trinity::Icons::c_Trash, "Delete Layout").c_str(), !l_Names.empty()))
    {
        for (const std::string& it_Name : l_Names)
        {
            if (ImGui::MenuItem(it_Name.c_str()))
            {
                static_cast<void>(Layouts::Remove(it_Name));
            }
        }

        ImGui::EndMenu();
    }
}

// Asks the layout's name, and tells where one of that name is already saved, which it then takes the place of
void ForgeLayer::DrawSaveLayoutPopup()
{
    if (std::exchange(m_OpenSaveLayoutPopup, false))
    {
        ImGui::OpenPopup(c_SaveLayoutPopup);
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(c_SaveLayoutPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        return;
    }

    ImGui::TextUnformatted("Which panels are open and where each one is, saved under a name");
    if (ImGui::IsWindowAppearing())
    {
        ImGui::SetKeyboardFocusHere();
    }

    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 24.0f);
    const bool l_Entered = ImGui::InputTextWithHint("##LayoutName", "Name", &m_LayoutName, ImGuiInputTextFlags_EnterReturnsTrue);
    const std::string l_Name = Layouts::MakeName(m_LayoutName);
    const std::vector<std::string> l_Names = Layouts::List();
    const bool l_Replaces = std::ranges::find(l_Names, l_Name) != l_Names.end();
    if (l_Replaces)
    {
        ImGui::TextColored(Trinity::Theme::GetColor(Trinity::ThemeColor::Warning), "%s", std::format("{} {} is saved already, and this takes its place", Trinity::Icons::c_Warning, l_Name).c_str());
    }

    ImGui::Spacing();
    ImGui::BeginDisabled(l_Name.empty());
    if (ImGui::Button(l_Replaces ? "Replace" : "Save") || (l_Entered && !l_Name.empty()))
    {
        SaveLayout(l_Name);
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

// A maximised panel is restored first, so the arrangement saved is the whole of it
void ForgeLayer::SaveLayout(std::string_view name)
{
    SetMaximized(nullptr);
    std::vector<std::string> l_Open;
    for (const Trinity::Scope<Panel>& it_Panel : m_Panels)
    {
        if (it_Panel->IsOpen())
        {
            l_Open.push_back(it_Panel->GetSettingsID());
        }
    }

    if (Layouts::Save(name, l_Open, ImGui::SaveIniSettingsToMemory()))
    {
        Toasts::Notify(std::format("Saved layout {}", name));
    }
}

// ImGui's settings as they are now, with the layout's windows and docking in place of their own, loaded again, and each panel open or closed as the layout has it. A panel newer than the layout joins a panel of its slot, as one newer than imgui.ini does
void ForgeLayer::ApplyLayout(const std::string& name)
{
    const std::optional<Layouts::Layout> l_Layout = Layouts::Read(name);
    if (!l_Layout)
    {
        TR_WARN("Forge: the layout {} could not be read, so the panels stay as they are", name);

        return;
    }

    SetMaximized(nullptr);
    const std::string l_Settings = Layouts::Merge(ImGui::SaveIniSettingsToMemory(), l_Layout->Settings);
    ImGui::LoadIniSettingsFromMemory(l_Settings.data(), l_Settings.size());
    for (const Trinity::Scope<Panel>& it_Panel : m_Panels)
    {
        it_Panel->SetOpen(std::ranges::find(l_Layout->OpenPanels, it_Panel->GetSettingsID()) != l_Layout->OpenPanels.end());
    }

    m_DockedNewPanels = false;
    ImGui::MarkIniSettingsDirty();
}

// The panel under the mouse, or else the one with the focus, fills the dock space alone until this is asked again
void ForgeLayer::ToggleMaximized()
{
    if (m_Maximized != nullptr)
    {
        SetMaximized(nullptr);

        return;
    }

    const auto a_Hovered = std::ranges::find_if(m_Panels, [](const Trinity::Scope<Panel>& panel) { return panel->IsWindowHovered(); });
    const auto a_Focused = std::ranges::find_if(m_Panels, [](const Trinity::Scope<Panel>& panel) { return panel->IsWindowFocused(); });
    if (const auto a_Panel = a_Hovered != m_Panels.end() ? a_Hovered : a_Focused; a_Panel != m_Panels.end())
    {
        SetMaximized(a_Panel->get());
    }
}

// Null restores. The others are left undrawn, which hides their dock nodes, and the room they leave goes to the centre alone. So a panel outside the centre has its own node made the centre for as long as it is maximised, and the node that was is made it again after. Nothing is undocked, so every panel is back where it was
void ForgeLayer::SetMaximized(Panel* panel)
{
    if (ImGuiDockNode* l_Lent = m_LentNode != 0 ? ImGui::DockBuilderGetNode(m_LentNode) : nullptr)
    {
        l_Lent->SetLocalFlags(l_Lent->LocalFlags & ~ImGuiDockNodeFlags_CentralNode);
    }

    if (ImGuiDockNode* l_Central = m_CentralNode != 0 ? ImGui::DockBuilderGetNode(m_CentralNode) : nullptr)
    {
        l_Central->SetLocalFlags(l_Central->LocalFlags | ImGuiDockNodeFlags_CentralNode);
    }

    // What changed meanwhile is saved now that the layout is whole again
    if (m_Maximized != nullptr)
    {
        ImGui::MarkIniSettingsDirty();
    }

    m_CentralNode = 0;
    m_LentNode = 0;
    m_Maximized = nullptr;
    if (panel == nullptr)
    {
        return;
    }

    // Only one docked in Forge's own dock space has the others' room to take
    const ImGuiID l_DockSpace = ImHashStr(c_DockSpaceName.data(), c_DockSpaceName.size());
    const ImGuiWindow* l_Window = ImGui::FindWindowByName(panel->GetWindowName().c_str());
    ImGuiDockNode* l_Node = l_Window != nullptr ? l_Window->DockNode : nullptr;
    if (l_Node == nullptr || ImGui::DockNodeGetRootNode(l_Node)->ID != l_DockSpace)
    {
        TR_INFO("Forge: {} is not docked in Forge's window, so it is not maximised", panel->GetTitle());

        return;
    }

    // A centre with no panel in it would be let go of once it is no longer the centre, so it is left as it is
    ImGuiDockNode* l_Central = ImGui::DockBuilderGetCentralNode(l_DockSpace);
    if (l_Central != nullptr && l_Central != l_Node && l_Central->Windows.Size > 0)
    {
        l_Central->SetLocalFlags(l_Central->LocalFlags & ~ImGuiDockNodeFlags_CentralNode);
        l_Node->SetLocalFlags(l_Node->LocalFlags | ImGuiDockNodeFlags_CentralNode);
        m_CentralNode = l_Central->ID;
        m_LentNode = l_Node->ID;
    }

    m_Maximized = panel;
    m_Maximized->RequestFocus();
}

// Not while typing, dragging, in a popup, or while Input Actions listens for a key to bind
// Not while typing, dragging, in a popup, or while Input Actions listens. Ctrl+P plays and stops
void ForgeLayer::ReadShortcuts()
{
    if (ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive() || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) || m_InputActionsPanel->IsListening())
    {
        return;
    }

    static_cast<void>(m_Session.GetShortcuts().Dispatch());
}

// On a window with Forge's own title bar, the menu bar is that bar: taller, and ended with what is open and the window's buttons
void ForgeLayer::DrawMenuBar()
{
    if (!m_TitleBar.Begin())
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
        a_Item(Trinity::Icons::c_File, "New Scene", GetKeyText("file.newScene").c_str(), l_HasProject, EditorSession::Command::NewScene);
        a_Item(Trinity::Icons::c_FolderOpen, "Open Scene...", GetKeyText("file.openScene").c_str(), l_HasProject, EditorSession::Command::OpenScene);
        if (ImGui::BeginMenu(WithIcon(Trinity::Icons::c_History, "Open Recent Scene").c_str(), l_HasProject))
        {
            DrawRecentScenesMenu();

            ImGui::EndMenu();
        }

        // In prefab mode Save is the prefab's, as in Unity, and Save As waits for the scene
        const bool l_PrefabMode = static_cast<bool>(m_Session.GetOpenPrefab());
        a_Item(Trinity::Icons::c_Save, l_PrefabMode ? "Save Prefab" : "Save", GetKeyText("file.save").c_str(), l_HasProject, EditorSession::Command::SaveScene);
        a_Item(Trinity::Icons::c_Save, "Save As...", GetKeyText("file.saveAs").c_str(), l_HasProject && !l_PrefabMode, EditorSession::Command::SaveSceneAs);
        a_Item(Trinity::Icons::c_Gear, "Set as Start Scene", nullptr, l_HasProject && m_Session.HasScenePath(), EditorSession::Command::SetStartScene);
        a_Item(Trinity::Icons::c_Refresh, "Refresh", GetKeyText("file.refresh").c_str(), l_HasProject, EditorSession::Command::Refresh);
        ImGui::Separator();
        if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_PackageVariantClosed, "Build Settings...").c_str(), GetKeyText("file.buildSettings").c_str(), false, l_HasProject))
        {
            m_BuildPanel->SetOpen(true);
            m_BuildPanel->RequestFocus();
        }

        if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_Play, "Build And Run").c_str(), GetKeyText("file.buildAndRun").c_str(), false, l_HasProject && !m_Export.IsRunning()))
        {
            BuildAndRun();
        }

        ImGui::Separator();
        a_Item(Trinity::Icons::c_PowerOff, "Exit", "Alt+F4", true, EditorSession::Command::Exit);

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit"))
    {
        DrawEditMenu();

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Entity"))
    {
        DrawEntityMenu();

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
        if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_Expand, m_Maximized != nullptr ? "Restore Panels" : "Maximize Panel").c_str(), GetKeyText("window.maximize").c_str()))
        {
            ToggleMaximized();
        }

        ImGui::SetItemTooltip("%s", m_Maximized != nullptr ? "Brings the other panels back" : "The panel under the mouse, or the one with the focus, fills the window until this is asked again");

        bool l_ShowStats = m_ViewportPanel->IsShowingStats();
        if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_Eye, "Scene Stats").c_str(), nullptr, &l_ShowStats))
        {
            m_ViewportPanel->SetShowingStats(l_ShowStats);
        }

        if (ImGui::BeginMenu(WithIcon(Trinity::Icons::c_WindowRestore, "Layout").c_str()))
        {
            DrawLayoutMenu();

            ImGui::EndMenu();
        }

        if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_Refresh, "Reset Layout").c_str()))
        {
            SetMaximized(nullptr);
            m_ResetLayout = true;
        }

        // Greyed out where the platform has no windows for ImGui to float panels in
        const bool l_Supported = (ImGui::GetIO().BackendFlags & ImGuiBackendFlags_PlatformHasViewports) != 0;
        const Trinity::ConsoleVariableBase* l_Variable = Trinity::ConsoleVariables::Find("ui.viewports");
        bool l_Viewports = l_Supported && l_Variable != nullptr && l_Variable->ToString() == "true";
        if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_WindowRestore, "Multi-viewport").c_str(), nullptr, &l_Viewports, l_Supported))
        {
            m_Session.SetPreferenceVariable("ui.viewports", l_Viewports ? "true" : "false");
        }

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help"))
    {
        // What a newcomer looks for first, then what opens in the browser, then what is about Forge itself
        if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_Book, "Documentation").c_str(), GetKeyText("help.documentation").c_str()))
        {
            HelpLinks::Open(HelpLinks::c_Documentation);
        }

        if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_Search, "Command Palette...").c_str(), GetKeyText("help.palette").c_str()))
        {
            m_CommandPalette.Open();
        }

        if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_Keyboard, "Keyboard Shortcuts...").c_str(), GetKeyText("help.shortcuts").c_str()))
        {
            m_ShortcutsWindow.Open();
        }

        ImGui::Separator();
        if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_Globe, "Release Notes").c_str(), GetKeyText("help.releaseNotes").c_str()))
        {
            HelpLinks::Open(HelpLinks::c_ReleaseNotes);
        }

        if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_Bug, "Report a Bug").c_str(), GetKeyText("help.reportBug").c_str()))
        {
            HelpLinks::Open(HelpLinks::c_ReportBug);
        }

        ImGui::Separator();
        ImGui::MenuItem(WithIcon(Trinity::Icons::c_Terminal, "ImGui Demo").c_str(), GetKeyText("help.demo").c_str(), &m_ShowDemoWindow);
        ImGui::MenuItem(WithIcon(Trinity::Icons::c_InfoCircle, "About").c_str(), nullptr, &m_ShowAbout);

        ImGui::EndMenu();
    }

    // The project and its scene, with an asterisk while it has unsaved changes, as the system's title bar named them
    const std::string l_Title = m_Session.HasProject() ? std::format("{} - {}{}", m_Session.GetProject()->GetName(), m_Session.GetSceneName(), m_Session.IsDirty() ? "*" : "") : std::string("Trinity Forge");
    m_TitleBar.End(Trinity::Application::Get().GetWindow(), l_Title, [this] { m_Session.Request(EditorSession::Command::Exit); });
}

// Unity's and Unreal's strip under the menu bar: Save, Undo and Redo at the left, the play controls in the middle and the build status at the right
void ForgeLayer::DrawToolbar()
{
    const ImGuiStyle& l_Style = ImGui::GetStyle();
    const float l_Button = ImGui::GetFrameHeight();
    const ImGuiWindowFlags l_Flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse;

    // A frame's padding above and below the buttons, and no border, which would box the strip in
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(l_Style.WindowPadding.x, l_Style.FramePadding.y));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const bool l_Open = ImGui::BeginViewportSideBar("##ForgeToolbar", ImGui::GetMainViewport(), ImGuiDir_Up, l_Button + l_Style.FramePadding.y * 2.0f, l_Flags);
    ImGui::PopStyleVar(2);
    if (l_Open)
    {
        const auto a_Button = [l_Button](const char* icon, const char* id, bool enabled, const std::string& tooltip)
        {
            ImGui::BeginDisabled(!enabled);
            const bool l_Pressed = ImGui::Button(std::format("{}###{}", icon, id).c_str(), ImVec2(l_Button, 0.0f));
            ImGui::EndDisabled();

            ImGui::SetItemTooltip("%s", tooltip.c_str());
            ImGui::SameLine();

            return l_Pressed;
        };

        // In prefab mode Save is the prefab's, as it is in the File menu
        const bool l_PrefabMode = static_cast<bool>(m_Session.GetOpenPrefab());
        if (a_Button(Trinity::Icons::c_Save, "Save", m_Session.HasProject(), std::format("{} ({})", l_PrefabMode ? "Save Prefab" : "Save", GetKeyText("file.save"))))
        {
            m_Session.Request(EditorSession::Command::SaveScene);
        }

        CommandStack& l_History = m_Session.GetHistory();
        const bool l_CanUndo = l_History.CanUndo();
        const bool l_CanRedo = l_History.CanRedo();
        if (a_Button(Trinity::Icons::c_Undo, "Undo", l_CanUndo, l_CanUndo ? std::format("Undo {} ({})", l_History.GetCommand(l_History.GetPosition() - 1).GetName(), GetKeyText("edit.undo")) : std::format("Undo ({})", GetKeyText("edit.undo"))))
        {
            l_History.Undo();
        }

        if (a_Button(Trinity::Icons::c_Redo, "Redo", l_CanRedo, l_CanRedo ? std::format("Redo {} ({})", l_History.GetCommand(l_History.GetPosition()).GetName(), GetKeyText("edit.redo")) : std::format("Redo ({})", GetKeyText("edit.redo"))))
        {
            l_History.Redo();
        }

        DrawPlayControls();
        DrawBuildStatus();
    }

    ImGui::End();
}

// One for each kind of script: a spinner while building, red with its error count, else green
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

    // A project without scripts has none. Moving the cursor for nothing would stretch the toolbar
    if (l_Indicators.empty())
    {
        return;
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
                l_Color = Trinity::Theme::GetColor(Trinity::ThemeColor::Warning);
                l_Tooltip = std::format("The {} builds once its changes settle", it_Indicator.Name);

                break;
            }
            case BuildState::Building:
            {
                l_Color = Trinity::Theme::GetColor(Trinity::ThemeColor::Warning);
                l_Tooltip = std::format("The {} is building", it_Indicator.Name);

                break;
            }
            case BuildState::Failed:
            {
                l_Color = Trinity::Theme::GetColor(Trinity::ThemeColor::Error);
                l_Tooltip = std::format("The {} failed to build, with {} error(s) and {} warning(s), and what ran before it runs on. Click to see the errors in the Console", it_Indicator.Name, l_Status.Errors, l_Status.Warnings);

                break;
            }
            case BuildState::UpToDate:
            {
                l_Color = Trinity::Theme::GetColor(Trinity::ThemeColor::Success);
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
        // By its language rather than a ### suffix, which after C# would read as ## and cut the label
        ImGui::PushID(it_Indicator.Label);
        const bool l_Clicked = ImGui::Button(a_Text(it_Indicator).c_str());
        ImGui::PopID();
        ImGui::PopStyleColor(2);
        ImGui::SetItemTooltip("%s", l_Tooltip.c_str());
        if (l_Clicked && l_Status.State == BuildState::Failed)
        {
            m_Console->ShowBuildErrors(it_Indicator.Label, l_Status.FirstLine, l_Status.EndLine, *it_Indicator.ErrorLines);
        }

        ImGui::SameLine();
    }
}

// Centred in the toolbar: Play, which is Stop for as long as the scene plays, then Pause, which resumes a paused copy, and Step
void ForgeLayer::DrawPlayControls()
{
    const bool l_Playing = m_Session.IsPlaying();
    const bool l_Paused = m_Session.IsPaused();
    const float l_Button = ImGui::GetFrameHeight();
    const float l_Width = l_Button * 3.0f + ImGui::GetStyle().ItemSpacing.x * 2.0f;
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), (ImGui::GetWindowWidth() - l_Width) * 0.5f));

    const auto a_Button = [l_Button](const char* icon, const char* id, bool enabled, bool held, const char* tooltip)
    {
        ImGui::BeginDisabled(!enabled);
        const bool l_Pressed = Trinity::Widgets::ToggleButton(std::format("{}###{}", icon, id).c_str(), held, ImVec2(l_Button, 0.0f));
        ImGui::EndDisabled();

        ImGui::SetItemTooltip("%s", tooltip);
        ImGui::SameLine();

        return l_Pressed;
    };

    // One button for both, as Unreal has it: Play in edit mode, and Stop, held down, while the scene plays. Held down too while it waits for the scripts to build, and saying why while a build has failed
    const bool l_Waiting = m_Session.IsWaitingForBuild();
    const std::string l_Failed = l_Playing ? std::string() : m_Session.GetFailedBuilds();
    // Greyed in prefab mode, which has no scene to play, though a scene that plays can always be stopped
    const bool l_PrefabMode = static_cast<bool>(m_Session.GetOpenPrefab());
    const std::string l_PlayKey = GetKeyText("play.toggle");
    const std::string l_PauseKey = GetKeyText("play.pause");
    const std::string l_PlayTip = l_Playing ? std::format("Stop ({})", l_PlayKey) : l_PrefabMode ? "Play is disabled in prefab mode. Go back to the scene to play it" : l_Waiting ? std::format("Plays once the scripts have built. Click to stop waiting ({})", l_PlayKey) : !l_Failed.empty() ? std::format("Play ({}). Build errors in {} stop it until they are fixed", l_PlayKey, l_Failed) : std::format("Play ({})", l_PlayKey);
    if (a_Button(l_Playing ? Trinity::Icons::c_Stop : Trinity::Icons::c_Play, "Play", m_Session.HasProject() && (l_Playing || !l_PrefabMode), l_Playing || l_Waiting, l_PlayTip.c_str()))
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

    // Held down while paused, when it is what resumes
    if (a_Button(Trinity::Icons::c_Pause, "Pause", l_Playing, l_Paused, std::format("{} ({})", l_Paused ? "Resume" : "Pause", l_PauseKey).c_str()))
    {
        m_Session.RequestPause();
    }

    if (a_Button(Trinity::Icons::c_StepForward, "Step", l_Playing, false, std::format("Step ({})", GetKeyText("play.step")).c_str()))
    {
        m_Session.RequestStep();
    }
}

// Undo and Redo name their command. While a copy plays, only an asset's edits are undone
void ForgeLayer::DrawEditMenu()
{
    CommandStack& l_History = m_Session.GetHistory();
    const bool l_Playing = m_Session.IsPlaying();
    const bool l_CanUndo = l_History.CanUndo();
    const bool l_CanRedo = l_History.CanRedo();

    const std::string l_Undo = l_CanUndo ? std::format("Undo {}###Undo", l_History.GetCommand(l_History.GetPosition() - 1).GetName()) : std::string("Undo###Undo");
    if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_Undo, l_Undo).c_str(), GetKeyText("edit.undo").c_str(), false, l_CanUndo))
    {
        l_History.Undo();
    }

    const std::string l_Redo = l_CanRedo ? std::format("Redo {}###Redo", l_History.GetCommand(l_History.GetPosition()).GetName()) : std::string("Redo###Redo");
    if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_Redo, l_Redo).c_str(), GetKeyText("edit.redo").c_str(), false, l_CanRedo))
    {
        l_History.Redo();
    }

    if (ImGui::BeginMenu(WithIcon(Trinity::Icons::c_History, "History").c_str(), !l_Playing && l_History.GetCount() > 0))
    {
        DrawHistoryMenu();

        ImGui::EndMenu();
    }

    ImGui::Separator();

    const bool l_Selected = static_cast<bool>(m_Session.GetScene().FindEntityByUUID(m_Session.GetSelection()));
    const bool l_HasSelection = !l_Playing && l_Selected;
    if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_Copy, "Copy").c_str(), GetKeyText("edit.copy").c_str(), false, l_Selected))
    {
        m_Session.CopySelection();
    }

    const bool l_CanPaste = !l_Playing && m_Session.HasCopiedEntity();
    if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_Paste, "Paste").c_str(), GetKeyText("edit.paste").c_str(), false, l_CanPaste))
    {
        static_cast<void>(m_Session.PasteEntity());
    }

    if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_Paste, "Paste As Child").c_str(), GetKeyText("edit.pasteAsChild").c_str(), false, l_CanPaste && l_HasSelection))
    {
        static_cast<void>(m_Session.PasteEntityAsChild(m_Session.GetSelection()));
    }

    if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_CubeOutline, "Duplicate").c_str(), GetKeyText("edit.duplicate").c_str(), false, l_HasSelection))
    {
        static_cast<void>(m_Session.DuplicateSelection());
    }

    if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_Trash, "Delete").c_str(), GetKeyText("edit.delete").c_str(), false, l_HasSelection))
    {
        static_cast<void>(m_Session.DeleteSelection());
    }

    if (ImGui::MenuItem("Select All", GetKeyText("edit.selectAll").c_str(), false, m_Session.HasProject()))
    {
        m_HierarchyPanel->SelectAll();
    }

    ImGui::Separator();

    if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_Gear, "Project Settings...").c_str(), nullptr, false, m_Session.HasProject()))
    {
        m_ProjectSettings->SetOpen(true);
        m_ProjectSettings->RequestFocus();
    }

    if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_Sliders, "Preferences...").c_str(), GetKeyText("edit.preferences").c_str()))
    {
        m_PreferencesWindow.Open();
    }
}

// Unity's GameObject menu. New entities go among the roots at the spawn point. Not while playing
void ForgeLayer::DrawEntityMenu()
{
    const bool l_Editable = m_Session.HasProject() && !m_Session.IsPlaying();
    const bool l_HasSelection = l_Editable && static_cast<bool>(m_Session.GetScene().FindEntityByUUID(m_Session.GetSelection()));
    if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_CubeOutline, "Create Empty").c_str(), GetKeyText("entity.createEmpty").c_str(), false, l_Editable))
    {
        CreateEmpty({});
    }

    if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_CubeOutline, "Create Empty Child").c_str(), GetKeyText("entity.createEmptyChild").c_str(), false, l_HasSelection))
    {
        CreateEmpty(m_Session.GetSelection());
    }

    ImGui::Separator();

    if (ImGui::BeginMenu(WithIcon(Trinity::Icons::c_Cube, "3D Object").c_str(), l_Editable))
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

    if (ImGui::BeginMenu(WithIcon(Trinity::Icons::c_Globe, "Light").c_str(), l_Editable))
    {
        for (const Trinity::LightType it_Type : c_LightTypes)
        {
            if (ImGui::MenuItem(std::string(GetLightLabel(it_Type)).c_str()))
            {
                static_cast<void>(m_Session.CreateLight(it_Type, {}));
            }
        }

        ImGui::EndMenu();
    }

    if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_Monitor, "Camera").c_str(), nullptr, false, l_Editable))
    {
        static_cast<void>(m_Session.CreateCamera({}));
    }

    ImGui::Separator();

    // Unity's Unpack, for the instance the selection is of, its root or one of its entities
    const Trinity::Entity l_Instance = l_HasSelection ? Trinity::Prefab::FindInstanceRoot(m_Session.GetScene().FindEntityByUUID(m_Session.GetSelection())) : Trinity::Entity();
    if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_PackageVariantClosed, "Unpack Prefab").c_str(), nullptr, false, l_Instance && !IsMissingPrefab(l_Instance)))
    {
        static_cast<void>(m_Session.UnpackPrefab(l_Instance.GetUUID()));
    }

    if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_PackageVariantClosed, "Unpack Prefab Completely").c_str(), nullptr, false, l_Instance && !IsMissingPrefab(l_Instance)))
    {
        static_cast<void>(m_Session.UnpackPrefab(l_Instance.GetUUID(), true));
    }
}

// As Unity puts a new entity's name up for typing, in the Hierarchy brought forward
void ForgeLayer::CreateEmpty(Trinity::UUID parent)
{
    if (m_Session.CreateEmpty(parent) && m_HierarchyPanel->IsOpen())
    {
        m_HierarchyPanel->RequestFocus();
        m_HierarchyPanel->Rename(m_Session.GetSelection());
    }
}

// As Unity's Open Recent Scene: newest first, the open one ticked. A missing one is greyed
void ForgeLayer::DrawRecentScenesMenu()
{
    const std::vector<EditorSession::RecentScene>& l_Scenes = m_Session.GetRecentScenes();
    const Trinity::AssetRegistry* l_Registry = m_Session.GetRegistry();
    const Trinity::UUID l_Open = m_Session.GetSceneID();
    if (l_Scenes.empty())
    {
        ImGui::TextDisabled("No scene opened or saved yet");
    }

    std::optional<Trinity::UUID> l_Removed;
    for (const EditorSession::RecentScene& it_Scene : l_Scenes)
    {
        const Trinity::AssetRecord* l_Record = l_Registry != nullptr ? l_Registry->Find(it_Scene.ID) : nullptr;
        const auto [l_Name, l_Folder] = SplitScenePath(l_Record != nullptr ? std::string_view(l_Record->Path) : std::string_view(it_Scene.Path));
        ImGui::PushID(it_Scene.ID.ToString().c_str());
        if (l_Record == nullptr)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            const bool l_MenuOpen = ImGui::BeginMenu(std::format("{} {} (missing)", Trinity::Icons::c_Warning, l_Name).c_str());
            ImGui::PopStyleColor();
            if (l_MenuOpen)
            {
                if (ImGui::MenuItem(std::format("{} Remove from List", Trinity::Icons::c_TimesCircle).c_str()))
                {
                    l_Removed = it_Scene.ID;
                }

                ImGui::EndMenu();
            }
        }
        else if (ImGui::MenuItem(std::string(l_Name).c_str(), std::string(l_Folder).c_str(), it_Scene.ID == l_Open, it_Scene.ID != l_Open))
        {
            m_Session.RequestOpenScene(l_Record->Path);
        }

        ImGui::PopID();
    }

    ImGui::Separator();
    if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_Trash, "Clear Recent").c_str(), nullptr, false, !l_Scenes.empty()))
    {
        m_Session.ClearRecentScenes();
    }

    if (l_Removed)
    {
        m_Session.RemoveRecentScene(*l_Removed);
    }
}

// Only the project's language. A Forge that cannot build it says why when its items are hovered
void ForgeLayer::DrawScriptsMenu()
{
    const Trinity::Project* l_Project = m_Session.GetProject();
    if (l_Project == nullptr)
    {
        ImGui::TextDisabled("A project's scripts are here once it is open");
    }
    else if (l_Project->GetLanguage() == Trinity::ScriptLanguage::Cpp)
    {
        const ModuleHost& l_Module = m_Session.GetModule();
        const bool l_Supported = ModuleHost::IsSupported();
        const bool l_HasModule = l_Project->HasModule();
        const auto a_Explain = [l_Supported]
        {
            if (!l_Supported)
            {
                ImGui::SetItemTooltip("This Forge's engine is linked statically, so it cannot build or load C++ modules. Use a Debug or Release Forge");
            }
        };

        if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_File, "Create C++ Module").c_str(), nullptr, false, l_Supported && !l_HasModule))
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
    }
    else
    {
        // C# works in every configuration, unless .NET did not start
        const AssemblyHost& l_Assembly = m_Session.GetAssembly();
        const bool l_DotNet = AssemblyHost::IsSupported();
        const bool l_HasAssembly = l_Project->HasAssembly();
        const auto a_ExplainDotNet = [l_DotNet]
        {
            if (!l_DotNet)
            {
                ImGui::SetItemTooltip("C# is off, since .NET did not start. The log says why");
            }
        };

        if (ImGui::MenuItem(WithIcon(Trinity::Icons::c_File, "Create C# Project").c_str(), nullptr, false, l_DotNet && !l_HasAssembly))
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
            }

            if (!l_Available)
            {
                ImGui::SetItemTooltip("%s was not found on this machine", std::string(IdeLauncher::ToString(it_Editor)).c_str());
            }
        }

        ImGui::EndMenu();
    }

    // The script editor's debugger, attached to this Forge, whose process ID is shown too
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
    }

    ImGui::SetItemTooltip("Play waits until a native or a managed debugger is attached, so breakpoints in OnCreate are hit");
    if (ImGui::MenuItem(std::format("Copy Process ID ({})", l_ProcessID).c_str()))
    {
        ImGui::SetClipboardText(l_ProcessID.c_str());
    }
}

// Oldest at the top, redoable ones greyed. Picking one undoes or redoes to just after it
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

// imgui.ini keeps each user's layout. The default is built when it has none, or on Reset Layout
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

// A panel newer than the user's imgui.ini joins the node of a panel with the same slot
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

// Hierarchy left, Properties right, Scene and Game as centre tabs, Console below. All reopen but the floating ones
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
        // A floating panel has no node here, and stays put away until it is asked for
        if (it_Panel->GetSlot() == DockSlot::Floating)
        {
            it_Panel->SetOpen(false);

            continue;
        }

        it_Panel->SetOpen(true);
        ImGui::DockBuilderDockWindow(it_Panel->GetWindowName().c_str(), l_Nodes[std::to_underlying(it_Panel->GetSlot())]);
    }

    ImGui::DockBuilderFinish(dockSpace);
    m_ViewportPanel->RequestFocus();
    m_PropertiesPanel->RequestFocus();
    TR_INFO("Forge: default layout built");
}

// The version and commit, the graphics backend and adapter, and the UI's fonts and scale
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
    const std::string_view l_Commit = Trinity::GetBuildCommit();
    TextLine(l_Commit.empty() ? std::string("Commit none, as it was built outside a git checkout") : std::format("Commit {}{}", l_Commit, Trinity::IsBuildModified() ? ", with changes not committed" : ""));
    if (const std::string_view l_Date = Trinity::GetBuildDate(); !l_Date.empty())
    {
        TextLine(std::format("Committed {}", l_Date));
    }

    TextLine(std::format("Backend {}", Trinity::ToString(l_Device.API)));
    TextLine(std::format("Adapter {}{}", l_Device.AdapterName.empty() ? "none" : l_Device.AdapterName, l_Device.VideoMemoryBytes > 0 ? std::format(", {} video memory", Trinity::Memory::FormatBytes(l_Device.VideoMemoryBytes)) : ""));
    TextLine(std::format("Dear ImGui {}, multi-viewport {}", IMGUI_VERSION, l_Viewports ? "on" : "off"));

    ImGui::Separator();
    TextLine(std::format("JetBrains Mono Nerd Font {} {} {} {} {}", Trinity::Icons::c_File, Trinity::Icons::c_FolderOpen, Trinity::Icons::c_Gear, Trinity::Icons::c_Terminal, Trinity::Icons::c_Monitor));
    TextLine(std::format("DPI scale {:.2f}, ui.scale {:.2f}, text {:.0f} px", ImGui::GetStyle().FontScaleDpi, ImGui::GetStyle().FontScaleMain, ImGui::GetFontSize()));

    ImGui::End();
}

// imgui.ini's [ForgePanels][Open] section: an ID=1 or ID=0 line a panel, then panels' settings
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
    if (l_Layer.m_ViewportPanel->ReadSetting(l_Key, l_Value) || l_Layer.m_GamePanel->ReadSetting(l_Key, l_Value) || l_Layer.m_ContentBrowser->ReadSetting(l_Key, l_Value) || l_Layer.m_Console->ReadSetting(l_Key, l_Value))
    {
        return;
    }

    if (l_Key == "ScriptEditor")
    {
        if (!l_Layer.m_Session.HasPreferencesFile())
        {
            l_Layer.m_Session.SetScriptEditor(IdeLauncher::FromKey(l_Value).value_or(ScriptEditor::System));
        }

        return;
    }

    if (l_Key == "WaitForDebugger")
    {
        if (!l_Layer.m_Session.HasPreferencesFile())
        {
            l_Layer.m_Session.SetWaitForDebugger(l_Value == "1");
        }

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
    l_Layer.m_Console->WriteSettings(*buffer);

    buffer->append("\n");
}
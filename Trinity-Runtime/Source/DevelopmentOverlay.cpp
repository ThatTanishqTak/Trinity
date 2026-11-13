#if !defined(TR_DISTRIBUTION)
#include "DevelopmentOverlay.hpp"

#include <imgui.h>

#include <algorithm>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    // How far the panels keep from the window's edges, how much of the game shows through them, and how much of the window's width the log takes at most, so it stays clear of the mark
    constexpr float c_Margin = 8.0f;
    constexpr float c_Opacity = 0.6f;
    constexpr float c_LogWidth = 0.62f;
    // The menu RmlUi's debugger lays across the top of the view in its dp, which the statistics go under while it is open
    constexpr float c_DebuggerMenuHeight = 32.0f;

    constexpr ImVec4 c_ErrorColor{ 1.0f, 0.4f, 0.4f, 1.0f };
    constexpr ImVec4 c_WarningColor{ 1.0f, 0.82f, 0.3f, 1.0f };
    constexpr ImVec4 c_DimColor{ 0.65f, 0.65f, 0.65f, 1.0f };

    // A panel ImGui never gives the pointer, the keyboard or the focus to, and keeps nothing of
    constexpr ImGuiWindowFlags c_PanelFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize;

    // In the corner of the window the pivot names, 0 for the left or top and 1 for the right or bottom, a top one lowered by what lies across the top. Always to be ended with ImGui::End
    bool BeginPanel(const char* name, const ImGuiViewport& viewport, ImVec2 pivot, float top = 0.0f)
    {
        const ImVec2 l_Position(viewport.WorkPos.x + (pivot.x == 0.0f ? c_Margin : viewport.WorkSize.x - c_Margin), viewport.WorkPos.y + (pivot.y == 0.0f ? top + c_Margin : viewport.WorkSize.y - c_Margin));
        ImGui::SetNextWindowPos(l_Position, ImGuiCond_Always, pivot);
        ImGui::SetNextWindowBgAlpha(c_Opacity);

        return ImGui::Begin(name, nullptr, c_PanelFlags);
    }

    void ColoredLine(const ImVec4& color, std::string_view text)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted(text.data(), text.data() + text.size());
        ImGui::PopStyleColor();
    }

    void Line(std::string_view text)
    {
        ImGui::TextUnformatted(text.data(), text.data() + text.size());
    }

    std::string CountOf(std::uint32_t count, std::string_view thing)
    {
        return std::format("{} {}{}", count, thing, count == 1 ? "" : "s");
    }

    // As Forge's Game panel shows the sound: the voices mixed and those kept unheard, the sound events playing and the streams read from files, then how long each period of sound took to mix, on average and at most, over about the last half second
    void AudioLines(const Trinity::AudioEngine* engine, Trinity::AudioScene* scene)
    {
        if (engine == nullptr)
        {
            Line("Audio: none");

            return;
        }

        const std::string_view l_Muted = engine->IsMuted() ? ", muted" : "";
        if (scene == nullptr)
        {
            Line(std::format("Audio: no scene plays{}", l_Muted));
        }
        else
        {
            const Trinity::AudioScene::Statistics l_Statistics = scene->GetStatistics();
            Line(std::format("Audio: {} real, {} virtual voices, {} events, {} streams{}", l_Statistics.Real, l_Statistics.Virtual, l_Statistics.Events, l_Statistics.Streams, l_Muted));
        }

        const Trinity::AudioMixTiming l_Timing = engine->GetMixTiming();
        Line(l_Timing.PeriodFrames == 0 ? std::string("Mix: not timed yet") : std::format("Mix: {:.2f} ms of {:.2f} ms periods, {:.1f}%, at most {:.2f} ms", l_Timing.AverageMilliseconds, l_Timing.PeriodMilliseconds, l_Timing.PeriodMilliseconds > 0.0 ? 100.0 * l_Timing.AverageMilliseconds / l_Timing.PeriodMilliseconds : 0.0, l_Timing.PeakMilliseconds));
    }

    const ImVec4& GetLevelColor(Trinity::LogLevel level)
    {
        static const ImVec4 s_Text{ 1.0f, 1.0f, 1.0f, 1.0f };
        switch (level)
        {
            case Trinity::LogLevel::Critical:
            case Trinity::LogLevel::Error:
            {
                return c_ErrorColor;
            }
            case Trinity::LogLevel::Warn:
            {
                return c_WarningColor;
            }
            case Trinity::LogLevel::Trace:
            {
                return c_DimColor;
            }
            case Trinity::LogLevel::Info:
            {
                break;
            }
        }

        return s_Text;
    }
}

DevelopmentOverlay::DevelopmentOverlay() : Layer("Development")
{

}

// F3 shows and hides the statistics and the log, and F4 moves the debugger on. Neither reaches the layers below, nor the game's reads of the keyboard, a held key's repeats included
void DevelopmentOverlay::OnEvent(Trinity::Event& event)
{
    Trinity::EventDispatcher l_Dispatcher(event);
    l_Dispatcher.Dispatch<Trinity::KeyPressedEvent>([this](Trinity::KeyPressedEvent& pressed)
    {
        const Trinity::KeyCode l_Key = pressed.GetKeyCode();
        if (l_Key != c_StatsKey && l_Key != c_DebuggerKey)
        {
            return false;
        }

        Trinity::Input::TakeKey(l_Key);
        if (pressed.IsRepeat())
        {
            return true;
        }

        if (l_Key == c_StatsKey)
        {
            m_ShowStats = !m_ShowStats;

            return true;
        }

        Trinity::GameUI* l_UI = Trinity::Application::Get().GetGameUI();
        if (l_UI == nullptr)
        {
            TR_INFO("Development: this player has no game UI for RmlUi's debugger to inspect");

            return true;
        }

        l_UI->CycleDebugger();
        const Trinity::GameUIDebugger l_Debugger = l_UI->GetDebugger();
        if (l_Debugger.Document != Trinity::GameUI::c_NoDocument)
        {
            TR_INFO("Development: RmlUi's debugger inspects {}, {} of {}", l_Debugger.Path, l_Debugger.Position, CountOf(l_Debugger.Count, "document"));
        }
        else
        {
            TR_INFO("Development: RmlUi's debugger {}", l_Debugger.Count == 0 ? "has no document to inspect" : "closed");
        }

        return true;
    });
}

void DevelopmentOverlay::OnImGuiRender()
{
    CountLog();

    const ImGuiViewport& l_Viewport = *ImGui::GetMainViewport();
    DrawMark(l_Viewport);
    if (m_ShowStats)
    {
        DrawStats(l_Viewport);
        DrawLog(l_Viewport);
    }
}

// From the newest line back to the first not yet counted, then forwards, so a frame reads only what was logged since the last. Lines logged before the overlay started count too, as the history keeps them from the player's start
void DevelopmentOverlay::CountLog()
{
    const Trinity::LogHistory::Reader l_Reader = Trinity::LogHistory::Read();
    std::size_t l_First = l_Reader.GetCount();
    while (l_First > 0 && l_Reader[l_First - 1].Sequence >= m_NextLine)
    {
        --l_First;
    }

    for (std::size_t it_Line = l_First; it_Line < l_Reader.GetCount(); ++it_Line)
    {
        const Trinity::LogEntry& l_Entry = l_Reader[it_Line];
        m_Errors += l_Entry.Level == Trinity::LogLevel::Error || l_Entry.Level == Trinity::LogLevel::Critical ? 1 : 0;
        m_Warnings += l_Entry.Level == Trinity::LogLevel::Warn ? 1 : 0;
        m_NextLine = l_Entry.Sequence + 1;
    }
}

// In the bottom right corner, as Unity's is, with the counts beside it, the debugger's document under it while it is open, and the keys while nothing is shown
void DevelopmentOverlay::DrawMark(const ImGuiViewport& viewport) const
{
    if (BeginPanel("##DevelopmentMark", viewport, ImVec2(1.0f, 1.0f)))
    {
        Line("Development Build");
        if (m_Errors != 0)
        {
            ImGui::SameLine();
            ColoredLine(c_ErrorColor, CountOf(m_Errors, "error"));
        }

        if (m_Warnings != 0)
        {
            ImGui::SameLine();
            ColoredLine(c_WarningColor, CountOf(m_Warnings, "warning"));
        }

        const Trinity::GameUI* l_UI = Trinity::Application::Get().GetGameUI();
        const Trinity::GameUIDebugger l_Debugger = l_UI != nullptr ? l_UI->GetDebugger() : Trinity::GameUIDebugger{};
        if (l_Debugger.Document != Trinity::GameUI::c_NoDocument)
        {
            ColoredLine(c_DimColor, std::format("RmlUi debugger: {}, {} of {}", l_Debugger.Path, l_Debugger.Position, l_Debugger.Count));
        }
        else if (!m_ShowStats)
        {
            ColoredLine(c_DimColor, "F3 stats and log, F4 UI debugger");
        }
    }

    ImGui::End();
}

// In the top left corner, under the debugger's menu while it is open: the frame rate and time ImGui averages, the device and the view's size, each GPU memory heap's budget and use, what the last frame drew, and the sound
void DevelopmentOverlay::DrawStats(const ImGuiViewport& viewport) const
{
    Trinity::Application& l_Application = Trinity::Application::Get();
    const Trinity::GameUI* l_UI = l_Application.GetGameUI();
    const float l_Top = l_UI != nullptr && l_UI->GetDebugger().Document != Trinity::GameUI::c_NoDocument ? c_DebuggerMenuHeight * l_Application.GetWindow().GetDpiScale() : 0.0f;
    if (BeginPanel("##DevelopmentStats", viewport, ImVec2(0.0f, 0.0f), l_Top))
    {
        Trinity::Renderer& l_Renderer = l_Application.GetRenderer();
        const ImGuiIO& l_IO = ImGui::GetIO();
        const Trinity::RHI::DeviceInfo& l_Device = l_Application.GetDevice().GetInfo();
        Line(std::format("{:.0f} fps, {:.2f} ms a frame", l_IO.Framerate, l_IO.Framerate > 0.0f ? 1000.0f / l_IO.Framerate : 0.0f));
        Line(std::format("{} on {}, {}x{}", Trinity::ToString(l_Device.API), l_Device.AdapterName.empty() ? std::string_view("no adapter") : std::string_view(l_Device.AdapterName), l_Renderer.GetSceneWidth(), l_Renderer.GetSceneHeight()));
        for (const Trinity::RHI::MemoryHeapBudget& it_Heap : l_Application.GetDevice().GetMemoryBudget())
        {
            Line(Trinity::RHI::DescribeMemoryHeap(it_Heap));
        }

        const Trinity::Renderer3D::Statistics& l_3D = l_Renderer.GetRenderer3D().GetStatistics();
        Line(std::format("3D: {} of {} submeshes, {} draws, {} in {} shadow maps, {}", l_3D.Visible, l_3D.Submeshes, l_3D.PrePassDraws + l_3D.OpaqueDraws + l_3D.TransparentDraws + l_3D.WorldUIDraws, l_3D.ShadowDraws, l_3D.ShadowMaps, CountOf(l_3D.Lights, "light")));
        const Trinity::Renderer2D::Statistics& l_2D = l_Renderer.GetRenderer2D().GetStatistics();
        Line(std::format("2D: {} in {} draws", CountOf(l_2D.Sprites, "sprite"), l_2D.DrawCalls));
        if (l_UI != nullptr)
        {
            const Trinity::GameUIStatistics l_Statistics = l_UI->GetStatistics();
            Line(std::format("UI: {}, {} commands", CountOf(l_Statistics.Documents, "document"), l_Statistics.Commands));
        }

        const Trinity::SceneManager* l_Scenes = Trinity::SceneManager::GetActive();
        Trinity::SceneRuntime* l_Runtime = l_Scenes != nullptr ? l_Scenes->GetRuntime() : nullptr;
        AudioLines(l_Application.GetAudioEngine(), l_Runtime != nullptr ? &l_Runtime->GetAudio() : nullptr);
    }

    ImGui::End();
}

// Along the bottom, from the left, the newest last. The lines are copied out of the history first, so nothing logged while ImGui draws waits on its lock
void DevelopmentOverlay::DrawLog(const ImGuiViewport& viewport) const
{
    std::vector<std::pair<Trinity::LogLevel, std::string>> l_Lines;
    {
        const Trinity::LogHistory::Reader l_Reader = Trinity::LogHistory::Read();
        const std::size_t l_Count = l_Reader.GetCount();
        l_Lines.reserve(std::min(l_Count, c_LogLines));
        for (std::size_t it_Line = l_Count > c_LogLines ? l_Count - c_LogLines : 0; it_Line < l_Count; ++it_Line)
        {
            const Trinity::LogEntry& l_Entry = l_Reader[it_Line];
            l_Lines.emplace_back(l_Entry.Level, std::format("{} {}", Trinity::Log::FormatTime(l_Entry.Time), l_Entry.Text));
        }
    }

    const float l_Width = viewport.WorkSize.x * c_LogWidth;
    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(l_Width, viewport.WorkSize.y * 0.5f));
    if (BeginPanel("##DevelopmentLog", viewport, ImVec2(0.0f, 1.0f)))
    {
        ImGui::PushTextWrapPos(l_Width - ImGui::GetStyle().WindowPadding.x * 2.0f);
        if (l_Lines.empty())
        {
            ColoredLine(c_DimColor, "Nothing has been logged");
        }

        for (const auto& [it_Level, it_Text] : l_Lines)
        {
            ColoredLine(GetLevelColor(it_Level), it_Text);
        }

        ImGui::PopTextWrapPos();
    }

    ImGui::End();
}
#endif
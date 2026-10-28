#include "Panels/GamePanel.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    constexpr double c_SettleSeconds = 0.1;
    constexpr const char* c_NoCameraMessage = "The scene has no primary camera to show it through";
    constexpr float c_MaximumScale = 5.0f;
    // Each notch of the wheel zooms by this much
    constexpr float c_WheelZoom = 1.1f;
    constexpr float c_StatsMargin = 8.0f;
    // The Actions overlay's value bars, and a Vector2's square, in font sizes
    constexpr float c_ValueWidth = 6.0f;
    constexpr float c_SquareSize = 2.5f;

    // How a choice is named in imgui.ini and in the menu, as Unity names it there, and the shape it keeps: none when free, a ratio, or a fixed resolution
    struct AspectChoice
    {
        std::string_view Name;
        std::string_view Label;
        std::uint32_t Width = 0;
        std::uint32_t Height = 0;
        bool Fixed = false;
    };

    constexpr std::array<AspectChoice, 10> c_Aspects{ {
        { "Free", "Free Aspect", 0, 0, false },
        { "5:4", "5:4 Aspect", 5, 4, false },
        { "4:3", "4:3 Aspect", 4, 3, false },
        { "3:2", "3:2 Aspect", 3, 2, false },
        { "16:10", "16:10 Aspect", 16, 10, false },
        { "16:9", "16:9 Aspect", 16, 9, false },
        { "1280x720", "HD (1280x720)", 1280, 720, true },
        { "1920x1080", "Full HD (1920x1080)", 1920, 1080, true },
        { "2560x1440", "QHD (2560x1440)", 2560, 1440, true },
        { "3840x2160", "4K UHD (3840x2160)", 3840, 2160, true }
    } };

    // The first primary camera in hierarchy order, the one the game shows the scene through
    Trinity::Entity FindPrimaryCamera(Trinity::Scene& scene)
    {
        const Trinity::SceneRegistry& l_Registry = scene.GetRegistry();
        for (Trinity::Entity it_Entity = scene.GetFirstRoot(); it_Entity; it_Entity = scene.GetNextInHierarchyOrder(it_Entity))
        {
            if (const Trinity::CameraComponent* l_Camera = l_Registry.try_get<Trinity::CameraComponent>(it_Entity.GetHandle()); l_Camera != nullptr && l_Camera->Primary)
            {
                return it_Entity;
            }
        }

        return {};
    }

    // A button lit while it is on, as Unity's Stats is, which imgui.ini keeps
    void DrawToggle(const char* label, bool& on)
    {
        const bool l_On = on;
        if (l_On)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }

        if (ImGui::Button(label))
        {
            on = !on;
            ImGui::MarkIniSettingsDirty();
        }

        if (l_On)
        {
            ImGui::PopStyleColor();
        }
    }

    // The game's sound as the Stats show it, read from the engine and the playing scene each frame: the voices mixed and those kept unheard, the sound events playing and the streams read from files, then how long each period of sound took to mix, on average and at most, over about the last half second
    void AppendAudioStats(std::vector<std::string>& lines, const Trinity::AudioEngine* engine, const Trinity::AudioScene* scene)
    {
        if (engine == nullptr)
        {
            lines.emplace_back("Audio: none");

            return;
        }

        const std::string_view l_Muted = engine->IsMuted() ? ", muted" : "";
        if (scene == nullptr)
        {
            lines.push_back(std::format("Audio: no scene plays{}", l_Muted));
        }
        else
        {
            const Trinity::AudioScene::Statistics l_Statistics = scene->GetStatistics();
            lines.push_back(std::format("Audio: {} real, {} virtual voices{}", l_Statistics.Real, l_Statistics.Virtual, l_Muted));
            lines.push_back(std::format("{} events, {} streams", l_Statistics.Events, l_Statistics.Streams));
        }

        const Trinity::AudioMixTiming l_Timing = engine->GetMixTiming();
        lines.push_back(l_Timing.PeriodFrames == 0 ? std::string("Mix: not timed yet") : std::format("Mix {:.2f} ms of {:.2f} ms periods, {:.1f}%, at most {:.2f} ms", l_Timing.AverageMilliseconds, l_Timing.PeriodMilliseconds, l_Timing.PeriodMilliseconds > 0.0 ? 100.0 * l_Timing.AverageMilliseconds / l_Timing.PeriodMilliseconds : 0.0, l_Timing.PeakMilliseconds));
    }

    // The keyboard and mouse and each connected pad the player reads
    std::string DescribeDevices(std::uint32_t player)
    {
        std::string l_Devices = Trinity::InputActions::ReadsDevice(player, Trinity::InputDevice::KeyboardMouse) ? "Keyboard & Mouse" : "";
        for (std::uint32_t it_Gamepad = 0; it_Gamepad < Trinity::Input::c_MaxGamepads; ++it_Gamepad)
        {
            if (Trinity::Input::IsGamepadConnected(it_Gamepad) && Trinity::InputActions::ReadsDevice(player, Trinity::GetGamepadDevice(it_Gamepad)))
            {
                l_Devices += std::format("{}Gamepad {}", l_Devices.empty() ? "" : ", ", it_Gamepad);
            }
        }

        return l_Devices.empty() ? "no device" : l_Devices;
    }

    std::string FormatValue(Trinity::InputActionType type, glm::vec2 value)
    {
        switch (type)
        {
            case Trinity::InputActionType::Button:
            {
                return std::format("{:.2f}", value.x);
            }
            case Trinity::InputActionType::Axis:
            {
                return std::format("{:+.2f}", value.x);
            }
            case Trinity::InputActionType::Vector2:
            {
                return std::format("{:+.2f}, {:+.2f}", value.x, value.y);
            }
        }

        return {};
    }

    // A bar filled from the left for a Button and from the middle for an Axis, marked at the press point, or a dot in a square for a Vector2, with Y up and a ring at the press point. A Hold's or a MultiTap's progress runs along the foot
    void DrawActionValue(ImDrawList& drawList, ImVec2 min, ImVec2 max, const Trinity::InputActionDefinition& action, const Trinity::InputActions::ActionReading& reading)
    {
        const ImU32 l_Fill = ImGui::GetColorU32(reading.Held ? ImGuiCol_PlotHistogramHovered : ImGuiCol_PlotHistogram);
        const ImU32 l_Mark = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        const float l_Width = max.x - min.x;
        const float l_Press = Trinity::InputActions::c_PressPoint;
        drawList.AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_FrameBg));
        switch (action.Type)
        {
            case Trinity::InputActionType::Button:
            {
                drawList.AddRectFilled(min, ImVec2(min.x + l_Width * std::clamp(reading.Value.x, 0.0f, 1.0f), max.y), l_Fill);
                drawList.AddLine(ImVec2(min.x + l_Width * l_Press, min.y), ImVec2(min.x + l_Width * l_Press, max.y), l_Mark);

                break;
            }
            case Trinity::InputActionType::Axis:
            {
                const float l_Centre = min.x + l_Width * 0.5f;
                const float l_End = l_Centre + l_Width * 0.5f * std::clamp(reading.Value.x, -1.0f, 1.0f);
                drawList.AddRectFilled(ImVec2(std::min(l_Centre, l_End), min.y), ImVec2(std::max(l_Centre, l_End), max.y), l_Fill);
                drawList.AddLine(ImVec2(l_Centre - l_Width * 0.5f * l_Press, min.y), ImVec2(l_Centre - l_Width * 0.5f * l_Press, max.y), l_Mark);
                drawList.AddLine(ImVec2(l_Centre + l_Width * 0.5f * l_Press, min.y), ImVec2(l_Centre + l_Width * 0.5f * l_Press, max.y), l_Mark);

                break;
            }
            case Trinity::InputActionType::Vector2:
            {
                const ImVec2 l_Centre((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
                const float l_Radius = l_Width * 0.5f;
                const glm::vec2 l_Value = glm::clamp(reading.Value, glm::vec2(-1.0f), glm::vec2(1.0f));
                drawList.AddCircle(l_Centre, l_Radius * l_Press, l_Mark);
                drawList.AddCircleFilled(ImVec2(l_Centre.x + l_Value.x * l_Radius, l_Centre.y - l_Value.y * l_Radius), std::max(l_Radius * 0.15f, 2.0f), l_Fill);

                break;
            }
        }

        const Trinity::InputInteractionKind l_Kind = action.Interaction.Kind;
        if (l_Kind == Trinity::InputInteractionKind::Hold || l_Kind == Trinity::InputInteractionKind::MultiTap)
        {
            const float l_Thickness = std::max(std::floor((max.y - min.y) * 0.2f), 2.0f);
            drawList.AddRectFilled(ImVec2(min.x, max.y - l_Thickness), ImVec2(min.x + l_Width * std::clamp(reading.Progress, 0.0f, 1.0f), max.y), ImGui::GetColorU32(ImGuiCol_PlotLines));
        }
    }
}

// The view starts at a single texel, so a Game panel that is never shown costs nothing
GamePanel::GamePanel(EditorSession& session) : Panel("Game", Trinity::Icons::c_Gamepad, DockSlot::Centre), m_Session(session)
{
    static_assert(c_Aspects.size() == static_cast<std::size_t>(Aspect::Count));

    SetBorderless(true);
    m_View = Trinity::Application::Get().GetRenderer().CreateView(1, 1);
}

// Layers are destroyed before the Renderer, so the view is still there to destroy
GamePanel::~GamePanel()
{
    Trinity::Application::Get().GetRenderer().DestroyView(m_View);
}

// The panel's lines in imgui.ini: the aspect choice, by its name, and whether Stats and Actions are on
bool GamePanel::ReadSetting(std::string_view key, std::string_view value)
{
    if (key == "GameStats")
    {
        m_ShowStats = value != "0";

        return true;
    }

    if (key == "GameActions")
    {
        m_ShowActions = value != "0";

        return true;
    }

    if (key == "GameMuteAudio")
    {
        m_MuteAudio = value != "0";

        return true;
    }

    if (key != "GameAspect")
    {
        return false;
    }

    const auto a_Found = std::ranges::find(c_Aspects, value, &AspectChoice::Name);
    if (a_Found != c_Aspects.end())
    {
        m_Aspect = static_cast<Aspect>(a_Found - c_Aspects.begin());
    }

    return true;
}

void GamePanel::WriteSettings(ImGuiTextBuffer& buffer) const
{
    const std::string l_Lines = std::format("GameAspect={}\nGameStats={}\nGameActions={}\nGameMuteAudio={}\n", c_Aspects[static_cast<std::size_t>(m_Aspect)].Name, m_ShowStats ? 1 : 0, m_ShowActions ? 1 : 0, m_MuteAudio ? 1 : 0);
    buffer.append(l_Lines.c_str(), l_Lines.c_str() + l_Lines.size());
}

// Through the scene's primary camera, for the view's own shape, with the camera's exposure and curve, the scene's sprites and 4x MSAA, as the game will draw it, the scene itself in prefab mode too. Nothing is submitted while the panel is hidden behind another tab or closed, so the view keeps what it last showed and costs nothing. After the transform pass
void GamePanel::PrepareScene()
{
    if (!std::exchange(m_Shown, false))
    {
        return;
    }

    Trinity::Scene& l_Scene = m_Session.GetMainScene();
    const Trinity::Entity l_Camera = FindPrimaryCamera(l_Scene);
    if (!l_Camera)
    {
        return;
    }

    Trinity::Renderer& l_Renderer = Trinity::Application::Get().GetRenderer();
    const float l_AspectRatio = static_cast<float>(l_Renderer.GetSceneWidth(m_View)) / static_cast<float>(std::max(l_Renderer.GetSceneHeight(m_View), 1u));
    const Trinity::CameraComponent& l_Component = l_Camera.Get<Trinity::CameraComponent>();

    Trinity::SceneOptions l_Options;
    l_Options.SampleCount = Trinity::Renderer3D::c_SampleCount;
    l_Options.Sprites = true;

    l_Renderer.SetToneMapping(l_Component.GetToneMapping(), m_View);
    l_Renderer.SubmitScene(l_Scene, Trinity::RenderView::FromCamera(l_Component, l_Camera.Get<Trinity::WorldTransformComponent>().Matrix, l_AspectRatio), l_Options, m_View);
}

// The toolbar under the tab, then the view's image on black in the space below it. The image is the view's own size, which catches up with the space once it settles, so meanwhile it is bordered or scaled down. While the scene plays the panel's keys are the game's, so the keyboard does not move between the toolbar's items, and until there is an image the scene reads the mouse over the space at 1:1
void GamePanel::OnImGuiRender()
{
    m_Shown = true;
    SetNavigable(!m_Session.IsPlaying());
    DrawToolbar();

    const ImVec2 l_Start = ImGui::GetCursorScreenPos();
    const ImVec2 l_Available = ImGui::GetContentRegionAvail();
    FollowArea(static_cast<std::uint32_t>(std::max(std::floor(l_Available.x), 1.0f)), static_cast<std::uint32_t>(std::max(std::floor(l_Available.y), 1.0f)));

    ImDrawList& l_DrawList = *ImGui::GetWindowDrawList();
    l_DrawList.AddRectFilled(l_Start, ImVec2(l_Start.x + l_Available.x, l_Start.y + l_Available.y), IM_COL32(0, 0, 0, 255));

    ImageArea l_Image{ glm::vec2(l_Start.x, l_Start.y), glm::vec2(l_Available.x, l_Available.y), 1.0f };
    if (!FindPrimaryCamera(m_Session.GetMainScene()))
    {
        const ImVec2 l_TextSize = ImGui::CalcTextSize(c_NoCameraMessage);
        l_DrawList.AddText(ImVec2(l_Start.x + (l_Available.x - l_TextSize.x) * 0.5f, l_Start.y + (l_Available.y - l_TextSize.y) * 0.5f), IM_COL32(255, 255, 255, 200), c_NoCameraMessage);
    }
    else
    {
        Trinity::Application& l_Application = Trinity::Application::Get();
        const Trinity::RHI::TextureHandle l_Target = l_Application.GetRenderer().GetDisplayTarget(m_View);
        const std::uint32_t l_Index = l_Target ? l_Application.GetDevice().GetShaderResourceIndex(l_Target) : Trinity::RHI::c_NoBindlessIndex;
        if (l_Index != Trinity::RHI::c_NoBindlessIndex)
        {
            l_Image = DrawImage(l_Start, l_Available, l_Index);
            if (m_ShowStats)
            {
                DrawStats(l_Start, l_Available);
            }
        }
    }

    if (m_ShowActions)
    {
        DrawActions(l_Start, l_Available);
    }

    FilterSceneInput(l_Image, l_Start, l_Available);
}

// Nothing of either once the panel has not been drawn for a frame
Trinity::SceneInputFilter GamePanel::TakeSceneFilter()
{
    return std::exchange(m_SceneFilter, { .Keyboard = false, .Mouse = false, .MouseOver = false });
}

// A strip under the tab, as Unity's Game view has: the aspect choice, saved in imgui.ini, the size the view is drawn at, the scale it is shown at, and Actions and Stats at the right end. It starts where the panel's content does, below the dock's tab bar, which a docked window's own top lies under
void GamePanel::DrawToolbar()
{
    const ImGuiStyle& l_Style = ImGui::GetStyle();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(l_Style.ItemSpacing.x, l_Style.ItemSpacing.y * 0.5f));
    const ImGuiWindowFlags l_Flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings | (m_Session.IsPlaying() ? ImGuiWindowFlags_NoNavInputs : ImGuiWindowFlags_None);
    const bool l_Open = ImGui::BeginChild("##GameToolbar", ImVec2(0.0f, ImGui::GetFrameHeight() + l_Style.ItemSpacing.y), ImGuiChildFlags_AlwaysUseWindowPadding, l_Flags);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    if (!l_Open)
    {
        ImGui::EndChild();

        return;
    }

    // As wide as the longest name, so it keeps its width whatever is chosen
    float l_LabelWidth = 0.0f;
    for (const AspectChoice& it_Choice : c_Aspects)
    {
        l_LabelWidth = std::max(l_LabelWidth, ImGui::CalcTextSize(it_Choice.Label.data()).x);
    }

    ImGui::SetNextItemWidth(l_LabelWidth + l_Style.FramePadding.x * 2.0f + ImGui::GetFrameHeight());
    if (ImGui::BeginCombo("##Aspect", c_Aspects[static_cast<std::size_t>(m_Aspect)].Label.data()))
    {
        for (std::size_t it_Choice = 0; it_Choice < c_Aspects.size(); ++it_Choice)
        {
            const bool l_Current = static_cast<std::size_t>(m_Aspect) == it_Choice;
            if (ImGui::Selectable(c_Aspects[it_Choice].Label.data(), l_Current) && !l_Current)
            {
                SetAspect(static_cast<Aspect>(it_Choice));
            }

            if (l_Current)
            {
                ImGui::SetItemDefaultFocus();
            }

            // The ratios and the fixed resolutions apart
            if (!c_Aspects[it_Choice].Fixed && it_Choice + 1 < c_Aspects.size() && c_Aspects[it_Choice + 1].Fixed)
            {
                ImGui::Separator();
            }
        }

        ImGui::EndCombo();
    }

    ImGui::SetItemTooltip("The game's shape: free to fill the panel, the largest rectangle of an aspect ratio that fits, or a fixed resolution");

    const Trinity::Renderer& l_Renderer = Trinity::Application::Get().GetRenderer();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", std::format("{}x{}", l_Renderer.GetSceneWidth(m_View), l_Renderer.GetSceneHeight(m_View)).c_str());
    ImGui::SetItemTooltip("The size the game is drawn at");

    // From the whole image fitted to the space, up to five times its size
    ImGui::SameLine(0.0f, l_Style.ItemSpacing.x * 3.0f);
    ImGui::TextUnformatted("Scale");
    ImGui::SameLine();
    const float l_Fit = GetFitScale();
    float l_Scale = GetScale();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
    if (ImGui::SliderFloat("##Scale", &l_Scale, l_Fit, c_MaximumScale, "", ImGuiSliderFlags_AlwaysClamp))
    {
        SetScale(l_Scale);
    }

    ImGui::SetItemTooltip("Zooms in to look at the game's pixels, or out to see all of a resolution larger than the panel. While the scene is stopped or paused, the wheel zooms towards the mouse and the middle button pans");
    ImGui::SameLine();
    ImGui::TextUnformatted(std::format("{:.1f}x", GetScale()).c_str());

    // Mute Audio, Actions and Stats at the right end, when there is room for them
    const std::string l_MuteLabel = std::format("{} Mute Audio", Trinity::Icons::c_VolumeOff);
    constexpr const char* c_ActionsLabel = "Actions";
    constexpr const char* c_StatsLabel = "Stats";
    const float l_ButtonsWidth = ImGui::CalcTextSize(l_MuteLabel.c_str()).x + ImGui::CalcTextSize(c_ActionsLabel).x + ImGui::CalcTextSize(c_StatsLabel).x + l_Style.FramePadding.x * 6.0f + l_Style.ItemSpacing.x * 2.0f;
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(ImGui::GetContentRegionAvail().x - l_ButtonsWidth, 0.0f));
    DrawToggle(l_MuteLabel.c_str(), m_MuteAudio);
    ImGui::SetItemTooltip("Silences the game while it plays, every sound playing on unheard and its meters moving, as Unity's Mute Audio does. Kept from one play to the next");
    ImGui::SameLine();
    DrawToggle(c_ActionsLabel, m_ShowActions);
    ImGui::SetItemTooltip("Each player's actions over the image while the scene plays, by the maps it has enabled: lit while held, with its value, and a Hold's or a MultiTap's progress along its foot");
    ImGui::SameLine();
    DrawToggle(c_StatsLabel, m_ShowStats);
    ImGui::SetItemTooltip("The frame rate and the game's size over the image, and its sound: the voices mixed and those kept unheard, the sound events playing, the streams read from files, and how long each period of sound takes to mix");
    ImGui::EndChild();
}

// The image at its scale and pan, clipped to the space, with sharp pixels from 1:1 up as Unity shows them, and smooth ones when scaled down. The wheel zooms towards the mouse and the middle button pans only while the scene is stopped or paused, as in Unity, so a running game keeps them
GamePanel::ImageArea GamePanel::DrawImage(ImVec2 start, ImVec2 available, std::uint32_t index)
{
    const ImGuiIO& l_IO = ImGui::GetIO();
    const Trinity::Renderer& l_Renderer = Trinity::Application::Get().GetRenderer();
    const glm::vec2 l_Size(static_cast<float>(l_Renderer.GetSceneWidth(m_View)), static_cast<float>(l_Renderer.GetSceneHeight(m_View)));
    const glm::vec2 l_Centre(start.x + available.x * 0.5f, start.y + available.y * 0.5f);

    ImGui::SetCursorScreenPos(start);
    ImGui::InvisibleButton("##GameImage", ImVec2(std::max(available.x, 1.0f), std::max(available.y, 1.0f)), ImGuiButtonFlags_MouseButtonMiddle);
    const bool l_CanZoom = !m_Session.IsPlaying() || m_Session.IsPaused();
    if (l_CanZoom && ImGui::IsItemHovered() && l_IO.MouseWheel != 0.0f)
    {
        // The point under the mouse stays under it
        const glm::vec2 l_Mouse = glm::vec2(l_IO.MousePos.x, l_IO.MousePos.y) - l_Centre;
        const float l_Before = GetScale();
        SetScale(l_Before * std::pow(c_WheelZoom, l_IO.MouseWheel));
        const glm::vec2 l_Point = (l_Mouse - m_Pan) / l_Before;
        m_Pan = l_Mouse - l_Point * GetScale();
    }

    if (l_CanZoom && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))
    {
        m_Pan += glm::vec2(l_IO.MouseDelta.x, l_IO.MouseDelta.y);
    }

    // Centred on an axis it fits, and never leaving a gap on one it overflows
    const float l_Scale = GetScale();
    const glm::vec2 l_Shown = glm::max(glm::floor(l_Size * l_Scale), glm::vec2(1.0f));
    const glm::vec2 l_Slack = glm::max((l_Shown - glm::vec2(available.x, available.y)) * 0.5f, glm::vec2(0.0f));
    m_Pan = glm::clamp(m_Pan, -l_Slack, l_Slack);
    const glm::vec2 l_Min = glm::floor(l_Centre + m_Pan - l_Shown * 0.5f);

    ImDrawList& l_DrawList = *ImGui::GetWindowDrawList();
    const ImGuiPlatformIO& l_PlatformIO = ImGui::GetPlatformIO();
    const bool l_Sharp = l_Scale >= 1.0f && l_PlatformIO.DrawCallback_SetSamplerNearest != nullptr && l_PlatformIO.DrawCallback_SetSamplerLinear != nullptr;
    l_DrawList.PushClipRect(start, ImVec2(start.x + available.x, start.y + available.y), true);
    if (l_Sharp)
    {
        l_DrawList.AddCallback(l_PlatformIO.DrawCallback_SetSamplerNearest, nullptr);
    }

    l_DrawList.AddImage(ImTextureRef(static_cast<ImTextureID>(index)), ImVec2(l_Min.x, l_Min.y), ImVec2(l_Min.x + l_Shown.x, l_Min.y + l_Shown.y));
    if (l_Sharp)
    {
        l_DrawList.AddCallback(l_PlatformIO.DrawCallback_SetSamplerLinear, nullptr);
    }

    l_DrawList.PopClipRect();

    return { l_Min, l_Shown, l_Scale };
}

// Over the image's top-right corner, as Unity's statistics are
void GamePanel::DrawStats(ImVec2 start, ImVec2 available) const
{
    const ImGuiIO& l_IO = ImGui::GetIO();
    const ImGuiStyle& l_Style = ImGui::GetStyle();
    const Trinity::Renderer& l_Renderer = Trinity::Application::Get().GetRenderer();
    std::vector<std::string> l_Lines{ std::format("{:.0f} fps, {:.2f} ms", l_IO.Framerate, l_IO.Framerate > 0.0f ? 1000.0f / l_IO.Framerate : 0.0f), std::format("Game {}x{}", l_Renderer.GetSceneWidth(m_View), l_Renderer.GetSceneHeight(m_View)), std::format("Shown at {:.1f}x in {}x{}", GetScale(), m_AreaWidth, m_AreaHeight) };
    Trinity::SceneRuntime* l_Runtime = m_Session.GetRuntime();
    AppendAudioStats(l_Lines, Trinity::Application::Get().GetAudioEngine(), l_Runtime != nullptr ? &l_Runtime->GetAudio() : nullptr);

    float l_Width = 0.0f;
    for (const std::string& it_Line : l_Lines)
    {
        l_Width = std::max(l_Width, ImGui::CalcTextSize(it_Line.c_str()).x);
    }

    const float l_LineHeight = ImGui::GetTextLineHeightWithSpacing();
    const ImVec2 l_Box(l_Width + l_Style.WindowPadding.x * 2.0f, static_cast<float>(l_Lines.size()) * l_LineHeight - l_Style.ItemSpacing.y + l_Style.WindowPadding.y * 2.0f);
    const ImVec2 l_Min(std::max(start.x + available.x - l_Box.x - c_StatsMargin, start.x), start.y + c_StatsMargin);

    ImDrawList& l_DrawList = *ImGui::GetWindowDrawList();
    l_DrawList.PushClipRect(start, ImVec2(start.x + available.x, start.y + available.y), true);
    l_DrawList.AddRectFilled(l_Min, ImVec2(l_Min.x + l_Box.x, l_Min.y + l_Box.y), ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.55f)), l_Style.ChildRounding);
    for (std::size_t it_Line = 0; it_Line < l_Lines.size(); ++it_Line)
    {
        l_DrawList.AddText(ImVec2(l_Min.x + l_Style.WindowPadding.x, l_Min.y + l_Style.WindowPadding.y + static_cast<float>(it_Line) * l_LineHeight), ImGui::GetColorU32(ImGuiCol_Text), l_Lines[it_Line].c_str());
    }

    l_DrawList.PopClipRect();
}

// Over the image's top-left corner, as the Stats are over its top-right: each player in use with the devices it reads, each map it has enabled and each of the map's actions, lit while held, with its value drawn and written. Only a playing scene reads its actions, so until one does it says so
void GamePanel::DrawActions(ImVec2 start, ImVec2 available) const
{
    // A player's line, a map's or an action's, by how far it is indented, or a note
    struct Line
    {
        std::string Text{};
        int Depth = 0;
        bool Dim = false;
        const Trinity::InputActionDefinition* Action = nullptr;
        Trinity::InputActions::ActionReading Reading{};
        std::string Value{};
    };

    std::vector<Line> l_Lines;
    const Trinity::InputActionsData* l_Data = m_Session.IsPlaying() ? Trinity::InputActions::GetData() : nullptr;
    if (!m_Session.IsPlaying())
    {
        l_Lines.push_back({ .Text = "The actions show here while the scene plays", .Dim = true });
    }
    else if (l_Data == nullptr)
    {
        l_Lines.push_back({ .Text = Trinity::InputActions::GetAsset() ? "The input actions asset is not ready" : "No input actions asset is set", .Dim = true });
    }
    else
    {
        for (std::uint32_t it_Player = 0; it_Player < Trinity::InputActions::c_MaxPlayers; ++it_Player)
        {
            if (!Trinity::InputActions::IsPlayer(it_Player))
            {
                continue;
            }

            l_Lines.push_back({ .Text = std::format("Player {}: {}", it_Player, DescribeDevices(it_Player)) });
            const std::size_t l_PlayerLine = l_Lines.size();
            std::uint32_t l_First = 0;
            for (const Trinity::InputActionMap& it_Map : l_Data->Maps)
            {
                const std::uint32_t l_MapFirst = std::exchange(l_First, l_First + static_cast<std::uint32_t>(it_Map.Actions.size()));
                if (!Trinity::InputActions::IsMapEnabled(std::string_view(it_Map.Name), it_Player))
                {
                    continue;
                }

                l_Lines.push_back({ .Text = std::string(std::string_view(it_Map.Name)), .Depth = 1, .Dim = true });
                for (std::uint32_t it_Action = 0; it_Action < it_Map.Actions.size(); ++it_Action)
                {
                    const Trinity::InputActionDefinition& l_Action = it_Map.Actions[it_Action];
                    const std::optional<Trinity::InputActions::ActionReading> l_Reading = Trinity::InputActions::ReadAction(l_MapFirst + it_Action, it_Player);
                    if (l_Reading)
                    {
                        l_Lines.push_back({ .Text = std::string(std::string_view(l_Action.Name)), .Depth = 2, .Action = &l_Action, .Reading = *l_Reading, .Value = FormatValue(l_Action.Type, l_Reading->Value) });
                    }
                }
            }

            if (l_Lines.size() == l_PlayerLine)
            {
                l_Lines.push_back({ .Text = "No map is enabled", .Depth = 1, .Dim = true });
            }
        }
    }

    const ImGuiStyle& l_Style = ImGui::GetStyle();
    const float l_Indent = ImGui::GetFontSize();
    const float l_BarWidth = std::floor(ImGui::GetFontSize() * c_ValueWidth);
    const float l_SquareSize = std::floor(ImGui::GetFontSize() * c_SquareSize);
    const float l_TextHeight = ImGui::GetTextLineHeight();
    // A Vector2's line is as tall as its square
    const auto a_GetHeight = [l_TextHeight, l_SquareSize](const Line& line) { return line.Action != nullptr && line.Action->Type == Trinity::InputActionType::Vector2 ? std::max(l_SquareSize, l_TextHeight) : l_TextHeight; };

    float l_NameWidth = 0.0f;
    float l_ValueWidth = 0.0f;
    float l_TextWidth = 0.0f;
    float l_Height = 0.0f;
    for (const Line& it_Line : l_Lines)
    {
        const float l_Width = static_cast<float>(it_Line.Depth) * l_Indent + ImGui::CalcTextSize(it_Line.Text.c_str()).x;
        if (it_Line.Action != nullptr)
        {
            l_NameWidth = std::max(l_NameWidth, l_Width);
            l_ValueWidth = std::max(l_ValueWidth, ImGui::CalcTextSize(it_Line.Value.c_str()).x);
        }
        else
        {
            l_TextWidth = std::max(l_TextWidth, l_Width);
        }

        l_Height += a_GetHeight(it_Line) + l_Style.ItemSpacing.y;
    }

    const float l_ActionsWidth = l_NameWidth > 0.0f ? l_NameWidth + l_BarWidth + l_ValueWidth + l_Style.ItemSpacing.x * 2.0f : 0.0f;
    const ImVec2 l_Box(std::max(l_TextWidth, l_ActionsWidth) + l_Style.WindowPadding.x * 2.0f, l_Height - l_Style.ItemSpacing.y + l_Style.WindowPadding.y * 2.0f);
    const ImVec2 l_Min(start.x + c_StatsMargin, start.y + c_StatsMargin);

    ImDrawList& l_DrawList = *ImGui::GetWindowDrawList();
    l_DrawList.PushClipRect(start, ImVec2(start.x + available.x, start.y + available.y), true);
    l_DrawList.AddRectFilled(l_Min, ImVec2(l_Min.x + l_Box.x, l_Min.y + l_Box.y), ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.55f)), l_Style.ChildRounding);
    const float l_Left = l_Min.x + l_Style.WindowPadding.x;
    float l_Top = l_Min.y + l_Style.WindowPadding.y;
    for (const Line& it_Line : l_Lines)
    {
        const float l_LineHeight = a_GetHeight(it_Line);
        const float l_TextTop = l_Top + (l_LineHeight - l_TextHeight) * 0.5f;
        const ImGuiCol l_Color = it_Line.Action != nullptr && it_Line.Reading.Held ? ImGuiCol_CheckMark : it_Line.Dim ? ImGuiCol_TextDisabled : ImGuiCol_Text;
        l_DrawList.AddText(ImVec2(l_Left + static_cast<float>(it_Line.Depth) * l_Indent, l_TextTop), ImGui::GetColorU32(l_Color), it_Line.Text.c_str());
        if (it_Line.Action != nullptr)
        {
            const float l_ValueLeft = l_Left + l_NameWidth + l_Style.ItemSpacing.x;
            const bool l_Square = it_Line.Action->Type == Trinity::InputActionType::Vector2;
            const float l_ValueHeight = l_Square ? l_SquareSize : std::floor(l_TextHeight * 0.6f);
            const ImVec2 l_ValueMin(l_ValueLeft, std::floor(l_Top + (l_LineHeight - l_ValueHeight) * 0.5f));
            DrawActionValue(l_DrawList, l_ValueMin, ImVec2(l_ValueLeft + (l_Square ? l_SquareSize : l_BarWidth), l_ValueMin.y + l_ValueHeight), *it_Line.Action, it_Line.Reading);
            l_DrawList.AddText(ImVec2(l_ValueLeft + l_BarWidth + l_Style.ItemSpacing.x, l_TextTop), ImGui::GetColorU32(ImGuiCol_Text), it_Line.Value.c_str());
        }

        l_Top += l_LineHeight + l_Style.ItemSpacing.y;
    }

    l_DrawList.PopClipRect();
}

// The scene's keyboard while the panel has the focus, a popup of it aside, and no field takes typing, and its mouse with it, read in the image's pixels, with a new press or the wheel only while the pointer is over what shows of the image and no window lies over it. ImGui places everything on the screen while windows can float, and the window's events are from its own top left, the main viewport's
void GamePanel::FilterSceneInput(const ImageArea& image, ImVec2 start, ImVec2 available)
{
    const bool l_Focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows | ImGuiFocusedFlags_NoPopupHierarchy);
    const ImVec2 l_Min(std::max(image.Min.x, start.x), std::max(image.Min.y, start.y));
    const ImVec2 l_Max(std::min(image.Min.x + image.Size.x, start.x + available.x), std::min(image.Min.y + image.Size.y, start.y + available.y));
    const bool l_Over = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(l_Min, l_Max, false);
    const ImVec2 l_Origin = ImGui::GetMainViewport()->Pos;

    m_SceneFilter.Keyboard = l_Focused && !ImGui::GetIO().WantTextInput;
    m_SceneFilter.Mouse = l_Focused;
    m_SceneFilter.MouseOver = l_Focused && l_Over;
    m_SceneFilter.MouseOrigin = image.Min - glm::vec2(l_Origin.x, l_Origin.y);
    m_SceneFilter.MouseScale = image.Scale;
}

// A size is taken once the space has held it for c_SettleSeconds, as the Scene panel's is, so dragging a splitter rebuilds the view's targets once. A new aspect choice is taken at once
void GamePanel::FollowArea(std::uint32_t width, std::uint32_t height)
{
    const double l_Now = ImGui::GetTime();
    if (width != m_AreaWidth || height != m_AreaHeight)
    {
        m_AreaWidth = width;
        m_AreaHeight = height;
        m_AreaTime = l_Now;
    }

    if (l_Now - m_AreaTime < c_SettleSeconds)
    {
        return;
    }

    Trinity::Renderer& l_Renderer = Trinity::Application::Get().GetRenderer();
    const glm::uvec2 l_Size = GetWantedSize();
    if (l_Size.x != l_Renderer.GetSceneWidth(m_View) || l_Size.y != l_Renderer.GetSceneHeight(m_View))
    {
        l_Renderer.SetSceneSize(l_Size.x, l_Size.y, m_View);
    }
}

// A new shape starts fitted and centred, as in Unity
void GamePanel::SetAspect(Aspect aspect)
{
    m_Aspect = aspect;
    m_Scale.reset();
    m_Pan = glm::vec2(0.0f);
    ImGui::MarkIniSettingsDirty();
}

// Fitted again when it comes back down to the fit, so from there it follows the space as it changes, as it does until it is first zoomed
void GamePanel::SetScale(float scale)
{
    const float l_Fit = GetFitScale();
    const float l_Scale = std::clamp(scale, l_Fit, c_MaximumScale);
    if (l_Scale <= l_Fit * 1.001f)
    {
        m_Scale.reset();
        m_Pan = glm::vec2(0.0f);

        return;
    }

    m_Scale = l_Scale;
}

float GamePanel::GetScale() const
{
    return std::clamp(m_Scale.value_or(0.0f), GetFitScale(), c_MaximumScale);
}

// 1:1, or smaller to show the whole of a view larger than the space, which only a fixed resolution is
float GamePanel::GetFitScale() const
{
    const Trinity::Renderer& l_Renderer = Trinity::Application::Get().GetRenderer();
    const float l_Width = static_cast<float>(std::max(l_Renderer.GetSceneWidth(m_View), 1u));
    const float l_Height = static_cast<float>(std::max(l_Renderer.GetSceneHeight(m_View), 1u));

    return std::min({ 1.0f, static_cast<float>(m_AreaWidth) / l_Width, static_cast<float>(m_AreaHeight) / l_Height });
}

// The whole space when free, the largest rectangle of the ratio's shape inside it, or the fixed resolution whatever the space
glm::uvec2 GamePanel::GetWantedSize() const
{
    const AspectChoice& l_Choice = c_Aspects[static_cast<std::size_t>(m_Aspect)];
    if (l_Choice.Fixed)
    {
        return { l_Choice.Width, l_Choice.Height };
    }

    if (l_Choice.Width == 0)
    {
        return { m_AreaWidth, m_AreaHeight };
    }

    const double l_Scale = std::min(static_cast<double>(m_AreaWidth) / l_Choice.Width, static_cast<double>(m_AreaHeight) / l_Choice.Height);
    const std::uint32_t l_Width = static_cast<std::uint32_t>(std::round(l_Choice.Width * l_Scale));
    const std::uint32_t l_Height = static_cast<std::uint32_t>(std::round(l_Choice.Height * l_Scale));

    return { std::clamp(l_Width, 1u, m_AreaWidth), std::clamp(l_Height, 1u, m_AreaHeight) };
}
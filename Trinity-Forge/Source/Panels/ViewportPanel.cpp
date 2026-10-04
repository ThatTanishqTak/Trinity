#include "Panels/ViewportPanel.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <string_view>

namespace
{
    constexpr double c_SettleSeconds = 0.1;
    constexpr float c_StatsMargin = 8.0f;

    void TextLine(std::string_view text)
    {
        ImGui::TextUnformatted(text.data(), text.data() + text.size());
    }
}

ViewportPanel::ViewportPanel(Trinity::ImGuiLayer& imGui, const SceneLayer& scene) : Panel("Viewport", Trinity::Icons::c_Monitor, DockSlot::Centre), m_ImGui(imGui), m_Scene(scene)
{
    SetBorderless(true);
}

// Saved in imgui.ini with the panels
void ViewportPanel::SetShowingStats(bool show)
{
    if (show != m_ShowStats)
    {
        m_ShowStats = show;
        ImGui::MarkIniSettingsDirty();
    }
}

// The image is drawn at the scene target's own size, which only catches up with the panel once its size settles, so it is cropped or bordered meanwhile but never stretched
void ViewportPanel::OnImGuiRender()
{
    const ImVec2 l_Available = ImGui::GetContentRegionAvail();
    const std::uint32_t l_Width = static_cast<std::uint32_t>(std::max(std::floor(l_Available.x), 1.0f));
    const std::uint32_t l_Height = static_cast<std::uint32_t>(std::max(std::floor(l_Available.y), 1.0f));
    FollowPanelSize(l_Width, l_Height);

    Trinity::Application& l_Application = Trinity::Application::Get();
    const Trinity::Renderer& l_Renderer = l_Application.GetRenderer();
    const Trinity::RHI::TextureHandle l_Target = l_Renderer.GetSceneTarget();
    const std::uint32_t l_Index = l_Target ? l_Application.GetDevice().GetShaderResourceIndex(l_Target) : Trinity::RHI::c_NoBindlessIndex;
    if (l_Index != Trinity::RHI::c_NoBindlessIndex)
    {
        ImGui::Image(ImTextureRef(static_cast<ImTextureID>(l_Index)), ImVec2(static_cast<float>(l_Renderer.GetSceneWidth()), static_cast<float>(l_Renderer.GetSceneHeight())));
    }

    const bool l_Mouse = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    const bool l_Keyboard = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
    m_ImGui.SetSceneInput(l_Mouse, l_Keyboard);

    if (m_ShowStats)
    {
        DrawStats(l_Mouse, l_Keyboard);
    }
}

// A size is taken once it has held for c_SettleSeconds, so dragging a splitter rebuilds the target once rather than every frame, and the sizes a panel passes through as the layout is built are skipped
void ViewportPanel::FollowPanelSize(std::uint32_t width, std::uint32_t height)
{
    const double l_Now = ImGui::GetTime();
    if (width != m_PanelWidth || height != m_PanelHeight)
    {
        m_PanelWidth = width;
        m_PanelHeight = height;
        m_PanelSizeTime = l_Now;
    }

    Trinity::Renderer& l_Renderer = Trinity::Application::Get().GetRenderer();
    const bool l_Changed = width != l_Renderer.GetSceneWidth() || height != l_Renderer.GetSceneHeight();
    if (l_Changed && l_Now - m_PanelSizeTime >= c_SettleSeconds)
    {
        l_Renderer.SetSceneSize(width, height);
    }
}

// Over the image's top-left corner. The overlay takes no input, so the scene under it keeps the mouse
void ViewportPanel::DrawStats(bool mouse, bool keyboard) const
{
    const ImGuiIO& l_IO = ImGui::GetIO();
    const Trinity::RHI::DeviceInfo& l_Device = Trinity::Application::Get().GetDevice().GetInfo();
    const Trinity::Renderer& l_Renderer = Trinity::Application::Get().GetRenderer();

    const ImVec2 l_Start = ImGui::GetCursorStartPos();
    ImGui::SetCursorPos(ImVec2(l_Start.x + c_StatsMargin, l_Start.y + c_StatsMargin));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.55f));
    const ImGuiChildFlags l_ChildFlags = ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding;
    const ImGuiWindowFlags l_WindowFlags = ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::BeginChild("##Stats", ImVec2(0.0f, 0.0f), l_ChildFlags, l_WindowFlags))
    {
        TextLine(std::format("{:.0f} fps, {:.2f} ms", l_IO.Framerate, l_IO.Framerate > 0.0f ? 1000.0f / l_IO.Framerate : 0.0f));
        TextLine(std::format("{} on {}", Trinity::ToString(l_Device.API), l_Device.AdapterName.empty() ? "no adapter" : l_Device.AdapterName));
        TextLine(std::format("Scene {}x{}", l_Renderer.GetSceneWidth(), l_Renderer.GetSceneHeight()));
        TextLine(std::format("Input to the scene: mouse {}, keys {}", mouse ? "on" : "off", keyboard ? "on" : "off"));
        TextLine(std::format("The scene saw {} mouse and {} key events", m_Scene.GetMouseEventCount(), m_Scene.GetKeyEventCount()));
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}
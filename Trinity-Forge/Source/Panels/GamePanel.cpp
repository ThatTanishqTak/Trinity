#include "Panels/GamePanel.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <string>
#include <utility>

namespace
{
    constexpr double c_SettleSeconds = 0.1;
    constexpr const char* c_NoCameraMessage = "The scene has no primary camera to show it through";

    // How a choice is named in the menu and in imgui.ini, and the shape it keeps: none when free, a ratio, or a fixed resolution
    struct AspectChoice
    {
        std::string_view Name;
        std::uint32_t Width = 0;
        std::uint32_t Height = 0;
        bool Fixed = false;
    };

    constexpr std::array<AspectChoice, 8> c_Aspects{ {
        { "Free", 0, 0, false },
        { "16:9", 16, 9, false },
        { "16:10", 16, 10, false },
        { "4:3", 4, 3, false },
        { "1280x720", 1280, 720, true },
        { "1920x1080", 1920, 1080, true },
        { "2560x1440", 2560, 1440, true },
        { "3840x2160", 3840, 2160, true }
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

// The panel's line in imgui.ini: the aspect choice, by its name
bool GamePanel::ReadSetting(std::string_view key, std::string_view value)
{
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
    const std::string l_Line = std::format("GameAspect={}\n", c_Aspects[static_cast<std::size_t>(m_Aspect)].Name);
    buffer.append(l_Line.c_str(), l_Line.c_str() + l_Line.size());
}

// Through the scene's primary camera, for the view's own shape, with the camera's exposure and curve, the scene's sprites and 4x MSAA, as the game will draw it. Nothing is submitted while the panel is hidden behind another tab or closed, so the view keeps what it last showed and costs nothing. After the transform pass
void GamePanel::PrepareScene()
{
    if (!std::exchange(m_Shown, false))
    {
        return;
    }

    Trinity::Scene& l_Scene = m_Session.GetScene();
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

// The toolbar, then under it the view's image centred on black: at 1:1, or scaled down to fit a space smaller than it, and never stretched. The image is the view's own size, which catches up with the space once it settles, so meanwhile it is bordered or scaled down
void GamePanel::OnImGuiRender()
{
    m_Shown = true;
    DrawToolbar();

    const ImVec2 l_Start = ImGui::GetCursorScreenPos();
    const ImVec2 l_Available = ImGui::GetContentRegionAvail();
    FollowArea(static_cast<std::uint32_t>(std::max(std::floor(l_Available.x), 1.0f)), static_cast<std::uint32_t>(std::max(std::floor(l_Available.y), 1.0f)));

    ImDrawList& l_DrawList = *ImGui::GetWindowDrawList();
    l_DrawList.AddRectFilled(l_Start, ImVec2(l_Start.x + l_Available.x, l_Start.y + l_Available.y), IM_COL32(0, 0, 0, 255));

    if (!FindPrimaryCamera(m_Session.GetScene()))
    {
        const ImVec2 l_TextSize = ImGui::CalcTextSize(c_NoCameraMessage);
        l_DrawList.AddText(ImVec2(l_Start.x + (l_Available.x - l_TextSize.x) * 0.5f, l_Start.y + (l_Available.y - l_TextSize.y) * 0.5f), IM_COL32(255, 255, 255, 200), c_NoCameraMessage);

        return;
    }

    Trinity::Application& l_Application = Trinity::Application::Get();
    const Trinity::Renderer& l_Renderer = l_Application.GetRenderer();
    const Trinity::RHI::TextureHandle l_Target = l_Renderer.GetDisplayTarget(m_View);
    const std::uint32_t l_Index = l_Target ? l_Application.GetDevice().GetShaderResourceIndex(l_Target) : Trinity::RHI::c_NoBindlessIndex;
    if (l_Index == Trinity::RHI::c_NoBindlessIndex)
    {
        return;
    }

    const float l_Width = static_cast<float>(l_Renderer.GetSceneWidth(m_View));
    const float l_Height = static_cast<float>(l_Renderer.GetSceneHeight(m_View));
    const float l_Scale = std::min({ 1.0f, l_Available.x / l_Width, l_Available.y / l_Height });
    const ImVec2 l_Size(std::max(std::floor(l_Width * l_Scale), 1.0f), std::max(std::floor(l_Height * l_Scale), 1.0f));
    ImGui::SetCursorScreenPos(ImVec2(std::floor(l_Start.x + (l_Available.x - l_Size.x) * 0.5f), std::floor(l_Start.y + (l_Available.y - l_Size.y) * 0.5f)));
    ImGui::Image(ImTextureRef(static_cast<ImTextureID>(l_Index)), l_Size);
}

// A row over the image: the aspect choice, saved in imgui.ini, and the size the view is drawn at
void GamePanel::DrawToolbar()
{
    const ImGuiStyle& l_Style = ImGui::GetStyle();
    ImGui::SetCursorPos(ImVec2(l_Style.ItemSpacing.x, l_Style.ItemSpacing.y));
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
    if (ImGui::BeginCombo("##Aspect", c_Aspects[static_cast<std::size_t>(m_Aspect)].Name.data()))
    {
        for (std::size_t it_Choice = 0; it_Choice < c_Aspects.size(); ++it_Choice)
        {
            const bool l_Current = static_cast<std::size_t>(m_Aspect) == it_Choice;
            if (ImGui::Selectable(c_Aspects[it_Choice].Name.data(), l_Current) && !l_Current)
            {
                m_Aspect = static_cast<Aspect>(it_Choice);
                ImGui::MarkIniSettingsDirty();
            }

            if (l_Current)
            {
                ImGui::SetItemDefaultFocus();
            }

            // A ratio and a fixed resolution apart
            if (it_Choice == static_cast<std::size_t>(Aspect::Ratio4x3))
            {
                ImGui::Separator();
            }
        }

        ImGui::EndCombo();
    }

    ImGui::SetItemTooltip("The game's shape: free to fill the panel, the largest rectangle of an aspect ratio that fits, or a fixed resolution, scaled down to fit a smaller panel");

    const Trinity::Renderer& l_Renderer = Trinity::Application::Get().GetRenderer();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", std::format("{}x{}", l_Renderer.GetSceneWidth(m_View), l_Renderer.GetSceneHeight(m_View)).c_str());
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
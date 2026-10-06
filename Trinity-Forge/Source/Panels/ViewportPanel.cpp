#include "Panels/ViewportPanel.hpp"

#include "EditorCommands.hpp"
#include "EditorPayloads.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>

#include <stb_image.h>

namespace
{
    constexpr double c_SettleSeconds = 0.1;
    constexpr double c_CameraSaveSeconds = 0.5;
    constexpr float c_StatsMargin = 8.0f;

    constexpr ImU32 c_CameraOutlineColor = IM_COL32(255, 255, 255, 200);
    constexpr ImU32 c_SelectionOutlineColor = IM_COL32(255, 160, 40, 255);
    constexpr float c_SelectionThickness = 2.0f;

    // A dropped texture's sprite is this many of its pixels to a world unit
    constexpr float c_PixelsPerUnit = 100.0f;

    void TextLine(std::string_view text)
    {
        ImGui::TextUnformatted(text.data(), text.data() + text.size());
    }

    std::string GetCameraPath(Trinity::UUID scene)
    {
        return std::format("{}/Editor/Views/{}.view", Trinity::Project::c_CacheMount, scene);
    }

    // The unit quad a sprite is drawn on, through its world transform, in X and Y
    std::array<glm::vec2, 4> GetSpriteCorners(const glm::mat4& world)
    {
        constexpr std::array<glm::vec2, 4> c_Local{ glm::vec2(-0.5f, -0.5f), glm::vec2(0.5f, -0.5f), glm::vec2(0.5f, 0.5f), glm::vec2(-0.5f, 0.5f) };

        std::array<glm::vec2, 4> l_Corners{};
        for (std::size_t it_Corner = 0; it_Corner < c_Local.size(); ++it_Corner)
        {
            l_Corners[it_Corner] = glm::vec2(world * glm::vec4(c_Local[it_Corner], 0.0f, 1.0f));
        }

        return l_Corners;
    }

    // Three numbers: the camera's X and Y, and the world height it shows
    bool ParseCamera(std::string_view text, glm::vec2& position, float& height)
    {
        std::array<float, 3> l_Values{};
        const char* l_Cursor = text.data();
        const char* l_End = text.data() + text.size();
        for (float& it_Value : l_Values)
        {
            while (l_Cursor != l_End && (*l_Cursor == ' ' || *l_Cursor == '\n' || *l_Cursor == '\r'))
            {
                ++l_Cursor;
            }

            const std::from_chars_result l_Parsed = std::from_chars(l_Cursor, l_End, it_Value);
            if (l_Parsed.ec != std::errc() || !std::isfinite(it_Value))
            {
                return false;
            }

            l_Cursor = l_Parsed.ptr;
        }

        position = { l_Values[0], l_Values[1] };
        height = l_Values[2];

        return height > 0.0f;
    }
}

ViewportPanel::ViewportPanel(Trinity::ImGuiLayer& imGui, EditorSession& session) : Panel("Viewport", Trinity::Icons::c_Monitor, DockSlot::Centre), m_ImGui(imGui), m_Session(session)
{
    SetBorderless(true);
}

// The session is still open here, so a camera moved in the last moments is saved
ViewportPanel::~ViewportPanel()
{
    if (m_CameraDirty)
    {
        SaveCamera();
    }
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

// Into the scene target, which the Viewport shows: the grid, then the scene's sprites through the editor camera
void ViewportPanel::RenderScene(Trinity::RHI::CommandList& commands)
{
    const glm::vec2 l_ViewportSize = GetViewportSize();
    const Trinity::Renderer& l_Renderer = Trinity::Application::Get().GetRenderer();

    m_Grid.Draw(commands, m_Camera, l_ViewportSize);
    Trinity::Application::Get().GetRenderer().GetRenderer2D().DrawScene(commands, m_Session.GetScene(), m_Camera.GetViewProjection(l_ViewportSize), l_Renderer.GetSceneFormat(), l_Renderer.GetSceneWidth(), l_Renderer.GetSceneHeight());
}

// The image is drawn at the scene target's own size, which only catches up with the panel once its size settles, so it is cropped or bordered meanwhile but never stretched. Overlays follow the image, so they line up with what it shows
void ViewportPanel::OnImGuiRender()
{
    FollowScene();

    const ImVec2 l_Available = ImGui::GetContentRegionAvail();
    const std::uint32_t l_Width = static_cast<std::uint32_t>(std::max(std::floor(l_Available.x), 1.0f));
    const std::uint32_t l_Height = static_cast<std::uint32_t>(std::max(std::floor(l_Available.y), 1.0f));
    FollowPanelSize(l_Width, l_Height);

    Trinity::Application& l_Application = Trinity::Application::Get();
    const Trinity::RHI::TextureHandle l_Target = l_Application.GetRenderer().GetSceneTarget();
    const std::uint32_t l_Index = l_Target ? l_Application.GetDevice().GetShaderResourceIndex(l_Target) : Trinity::RHI::c_NoBindlessIndex;
    const glm::vec2 l_ViewportSize = GetViewportSize();
    const ImVec2 l_ImageMin = ImGui::GetCursorScreenPos();
    if (l_Index != Trinity::RHI::c_NoBindlessIndex)
    {
        ImGui::Image(ImTextureRef(static_cast<ImTextureID>(l_Index)), ImVec2(l_ViewportSize.x, l_ViewportSize.y));
        AcceptTextureDrop(glm::vec2(l_ImageMin.x, l_ImageMin.y), l_ViewportSize);
    }

    const bool l_Hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    const bool l_Focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
    m_ImGui.SetSceneInput(l_Hovered, l_Focused);

    HandleInput(glm::vec2(l_ImageMin.x, l_ImageMin.y), l_ViewportSize, l_Hovered, l_Focused);

    ImDrawList& l_DrawList = *ImGui::GetWindowDrawList();
    l_DrawList.PushClipRect(ImGui::GetWindowPos(), ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y), true);
    DrawOverlays(l_DrawList, glm::vec2(l_ImageMin.x, l_ImageMin.y), l_ViewportSize);
    l_DrawList.PopClipRect();

    if (m_ShowStats)
    {
        DrawStats(l_ViewportSize);
    }

    if (m_CameraDirty && ImGui::GetTime() - m_CameraChangeTime >= c_CameraSaveSeconds)
    {
        SaveCamera();
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

// Each saved scene keeps its camera. A scene saved for the first time keeps the view it had while untitled, and one without a saved camera starts at the origin
void ViewportPanel::FollowScene()
{
    const Trinity::UUID l_SceneID = m_Session.GetSceneID();
    if (l_SceneID == m_SceneID)
    {
        return;
    }

    if (m_CameraDirty)
    {
        SaveCamera();
    }

    const bool l_WasUntitled = !m_SceneID;
    m_SceneID = l_SceneID;
    m_CameraDirty = false;
    if (!m_SceneID)
    {
        m_Camera.Reset();

        return;
    }

    const Trinity::Expected<std::string, Trinity::FileError> l_Text = Trinity::FileSystem::ReadText(GetCameraPath(m_SceneID));
    glm::vec2 l_Position{ 0.0f };
    float l_Height = EditorCamera::c_DefaultHeight;
    if (l_Text && ParseCamera(*l_Text, l_Position, l_Height))
    {
        m_Camera.Set(l_Position, l_Height);
    }
    else if (!l_WasUntitled)
    {
        m_Camera.Reset();
    }
}

// Per user and per project, so it lives in Cache beside the cooked assets. An untitled scene's camera is not kept
void ViewportPanel::SaveCamera()
{
    m_CameraDirty = false;
    if (!m_SceneID || !m_Session.HasProject())
    {
        return;
    }

    const glm::vec2 l_Position = m_Camera.GetPosition();
    const Trinity::Expected<void, Trinity::FileError> l_Written = Trinity::FileSystem::WriteText(GetCameraPath(m_SceneID), std::format("{} {} {}\n", l_Position.x, l_Position.y, m_Camera.GetHeight()));
    if (!l_Written)
    {
        TR_WARN("Scene view: the camera of scene {} could not be saved: {}", m_SceneID, Trinity::ToString(l_Written.GetError()));
    }
}

void ViewportPanel::MarkCameraChanged()
{
    m_CameraDirty = true;
    m_CameraChangeTime = ImGui::GetTime();
}

// Middle drag pans, the wheel zooms towards the cursor, F frames the selection, and a left click picks the top-most sprite or clears the selection. A pan begun in the Viewport carries on outside it
void ViewportPanel::HandleInput(glm::vec2 imageMin, glm::vec2 viewportSize, bool hovered, bool focused)
{
    const ImGuiIO& l_IO = ImGui::GetIO();
    const glm::vec2 l_Mouse = glm::vec2(l_IO.MousePos.x, l_IO.MousePos.y) - imageMin;

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
    {
        m_Panning = true;
    }

    if (m_Panning && !ImGui::IsMouseDown(ImGuiMouseButton_Middle))
    {
        m_Panning = false;
    }

    if (m_Panning && (l_IO.MouseDelta.x != 0.0f || l_IO.MouseDelta.y != 0.0f))
    {
        m_Camera.Pan(glm::vec2(l_IO.MouseDelta.x, l_IO.MouseDelta.y), viewportSize);
        MarkCameraChanged();
    }

    if (hovered && l_IO.MouseWheel != 0.0f)
    {
        m_Camera.ZoomAt(l_Mouse, viewportSize, l_IO.MouseWheel);
        MarkCameraChanged();
    }

    if (focused && !l_IO.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F, false))
    {
        FrameSelection(viewportSize);
    }

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        const Trinity::Entity l_Picked = Trinity::PickSprite(m_Session.GetScene(), m_Camera.ScreenToWorld(l_Mouse, viewportSize));
        m_Session.SetSelection(l_Picked ? l_Picked.Get<Trinity::IDComponent>().ID : Trinity::UUID());
    }
}

// The selected sprite's rotated rectangle, or with nothing selected every sprite in the scene. An empty scene frames the origin
void ViewportPanel::FrameSelection(glm::vec2 viewportSize)
{
    Trinity::Scene& l_Scene = m_Session.GetScene();
    Trinity::SceneRegistry& l_Registry = l_Scene.GetRegistry();
    glm::vec2 l_Minimum(std::numeric_limits<float>::max());
    glm::vec2 l_Maximum(std::numeric_limits<float>::lowest());
    const auto a_Include = [&](entt::entity entity)
    {
        for (const glm::vec2& it_Corner : GetSpriteCorners(l_Registry.get<Trinity::WorldTransformComponent>(entity).Matrix))
        {
            l_Minimum = glm::min(l_Minimum, it_Corner);
            l_Maximum = glm::max(l_Maximum, it_Corner);
        }
    };

    const Trinity::Entity l_Selected = l_Scene.FindEntityByUUID(m_Session.GetSelection());
    if (l_Selected && l_Selected.Has<Trinity::SpriteRendererComponent>())
    {
        a_Include(l_Selected.GetHandle());
    }
    else
    {
        for (const entt::entity it_Entity : l_Registry.view<Trinity::SpriteRendererComponent>())
        {
            a_Include(it_Entity);
        }
    }

    if (l_Minimum.x > l_Maximum.x)
    {
        m_Camera.Reset();
    }
    else
    {
        m_Camera.Frame(l_Minimum, l_Maximum, viewportSize);
    }

    MarkCameraChanged();
}

// A texture dropped from the Content Browser becomes a sprite where it lands, at its pixel size, in one command
void ViewportPanel::AcceptTextureDrop(glm::vec2 imageMin, glm::vec2 viewportSize)
{
    if (!ImGui::BeginDragDropTarget())
    {
        return;
    }

    if (const ImGuiPayload* l_Payload = ImGui::AcceptDragDropPayload(c_AssetPayload))
    {
        std::uint64_t l_Value = 0;
        std::memcpy(&l_Value, l_Payload->Data, sizeof(l_Value));
        const Trinity::UUID l_ID(l_Value);
        const Trinity::AssetRegistry* l_Registry = m_Session.GetRegistry();
        const Trinity::AssetRecord* l_Record = l_Registry != nullptr ? l_Registry->Find(l_ID) : nullptr;
        if (l_Record == nullptr || l_Record->Importer != Trinity::TextureAsset::c_AssetType)
        {
            TR_WARN("Forge: only a texture dropped into the Viewport makes a sprite");
        }
        else
        {
            // From the texture when it is loaded, and otherwise from the source file's header
            glm::vec2 l_Pixels{ c_PixelsPerUnit };
            const Trinity::Asset* l_Asset = Trinity::AssetManager::GetState(l_ID) == Trinity::AssetState::Ready ? Trinity::AssetManager::GetAsset(l_ID) : nullptr;
            if (l_Asset != nullptr && l_Asset->GetAssetType() == Trinity::TextureAsset::c_AssetType)
            {
                const Trinity::TextureAsset& l_Texture = static_cast<const Trinity::TextureAsset&>(*l_Asset);
                l_Pixels = { static_cast<float>(l_Texture.GetWidth()), static_cast<float>(l_Texture.GetHeight()) };
            }
            else if (const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_Source = Trinity::FileSystem::ReadFile(l_Record->Path))
            {
                int l_Width = 0;
                int l_Height = 0;
                int l_Channels = 0;
                if (stbi_info_from_memory(reinterpret_cast<const stbi_uc*>(l_Source->data()), static_cast<int>(l_Source->size()), &l_Width, &l_Height, &l_Channels) != 0)
                {
                    l_Pixels = { static_cast<float>(l_Width), static_cast<float>(l_Height) };
                }
            }

            const ImVec2 l_Mouse = ImGui::GetMousePos();
            const glm::vec2 l_World = m_Camera.ScreenToWorld(glm::vec2(l_Mouse.x, l_Mouse.y) - imageMin, viewportSize);
            const std::string l_Name = std::filesystem::path(l_Record->Path).stem().string();
            m_Session.GetHistory().Execute(Trinity::CreateScope<CreateSpriteCommand>(l_Name, l_ID, glm::vec3(l_World, 0.0f), l_Pixels / c_PixelsPerUnit));
        }
    }

    ImGui::EndDragDropTarget();
}

// The primary camera's bounds, at the Viewport's aspect ratio as a game drawn here would show them, and the selected sprite's rectangle. Corners snap to pixel centres so one-pixel lines stay sharp
void ViewportPanel::DrawOverlays(ImDrawList& drawList, glm::vec2 imageMin, glm::vec2 viewportSize)
{
    Trinity::Scene& l_Scene = m_Session.GetScene();
    Trinity::SceneRegistry& l_Registry = l_Scene.GetRegistry();
    const auto a_ToScreen = [&](glm::vec2 world)
    {
        const glm::vec2 l_Pixel = imageMin + m_Camera.WorldToScreen(world, viewportSize);

        return ImVec2(std::floor(l_Pixel.x) + 0.5f, std::floor(l_Pixel.y) + 0.5f);
    };

    const auto a_DrawQuad = [&](const std::array<glm::vec2, 4>& corners, ImU32 color, float thickness)
    {
        const std::array<ImVec2, 4> l_Points{ a_ToScreen(corners[0]), a_ToScreen(corners[1]), a_ToScreen(corners[2]), a_ToScreen(corners[3]) };
        drawList.AddPolyline(l_Points.data(), static_cast<int>(l_Points.size()), color, thickness, ImDrawFlags_Closed);
    };

    for (Trinity::Entity it_Entity = l_Scene.GetFirstRoot(); it_Entity; it_Entity = l_Scene.GetNextInHierarchyOrder(it_Entity))
    {
        const Trinity::CameraComponent* l_Camera = l_Registry.try_get<Trinity::CameraComponent>(it_Entity.GetHandle());
        if (l_Camera == nullptr || !l_Camera->Primary)
        {
            continue;
        }

        const float l_HalfHeight = l_Camera->OrthographicSize * 0.5f;
        const float l_HalfWidth = l_HalfHeight * (viewportSize.y > 0.0f ? viewportSize.x / viewportSize.y : 1.0f);
        const glm::mat4 l_Bounds = l_Registry.get<Trinity::WorldTransformComponent>(it_Entity.GetHandle()).Matrix * glm::scale(glm::mat4(1.0f), glm::vec3(l_HalfWidth * 2.0f, l_HalfHeight * 2.0f, 1.0f));
        a_DrawQuad(GetSpriteCorners(l_Bounds), c_CameraOutlineColor, 1.0f);

        break;
    }

    const Trinity::Entity l_Selected = l_Scene.FindEntityByUUID(m_Session.GetSelection());
    if (l_Selected && l_Selected.Has<Trinity::SpriteRendererComponent>())
    {
        a_DrawQuad(GetSpriteCorners(l_Selected.Get<Trinity::WorldTransformComponent>().Matrix), c_SelectionOutlineColor, c_SelectionThickness);
    }
}

// Over the image's top-left corner. The overlay takes no input, so the scene under it keeps the mouse
void ViewportPanel::DrawStats(glm::vec2 viewportSize) const
{
    const ImGuiIO& l_IO = ImGui::GetIO();
    const Trinity::RHI::DeviceInfo& l_Device = Trinity::Application::Get().GetDevice().GetInfo();
    Trinity::Renderer& l_Renderer = Trinity::Application::Get().GetRenderer();
    const Trinity::Renderer2D::Statistics& l_Sprites = l_Renderer.GetRenderer2D().GetStatistics();
    const Trinity::Entity l_Selected = m_Session.GetScene().FindEntityByUUID(m_Session.GetSelection());
    const glm::vec2 l_Position = m_Camera.GetPosition();

    const ImVec2 l_Start = ImGui::GetCursorStartPos();
    ImGui::SetCursorPos(ImVec2(l_Start.x + c_StatsMargin, l_Start.y + c_StatsMargin));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.55f));
    const ImGuiChildFlags l_ChildFlags = ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding;
    const ImGuiWindowFlags l_WindowFlags = ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::BeginChild("##Stats", ImVec2(0.0f, 0.0f), l_ChildFlags, l_WindowFlags))
    {
        TextLine(std::format("{:.0f} fps, {:.2f} ms", l_IO.Framerate, l_IO.Framerate > 0.0f ? 1000.0f / l_IO.Framerate : 0.0f));
        TextLine(std::format("{} on {}", Trinity::ToString(l_Device.API), l_Device.AdapterName.empty() ? "no adapter" : l_Device.AdapterName));
        TextLine(std::format("Scene {}x{}, {} sprite(s) in {} draw call(s)", l_Renderer.GetSceneWidth(), l_Renderer.GetSceneHeight(), l_Sprites.Sprites, l_Sprites.DrawCalls));
        TextLine(std::format("Camera at ({:.2f}, {:.2f}), {:.3g} units high, grid every {:g}", l_Position.x, l_Position.y, m_Camera.GetHeight(), EditorGrid::GetSpacing(m_Camera, viewportSize)));
        TextLine(l_Selected ? std::format("Selected: {}", std::string_view(l_Selected.Get<Trinity::TagComponent>().Tag)) : std::string("Nothing selected"));
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// The scene target's size, which the image is drawn at and the camera maps through
glm::vec2 ViewportPanel::GetViewportSize() const
{
    const Trinity::Renderer& l_Renderer = Trinity::Application::Get().GetRenderer();

    return { static_cast<float>(l_Renderer.GetSceneWidth()), static_cast<float>(l_Renderer.GetSceneHeight()) };
}
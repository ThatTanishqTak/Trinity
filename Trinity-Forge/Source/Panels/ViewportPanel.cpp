#include "Panels/ViewportPanel.hpp"

#include "EditorCommands.hpp"
#include "EditorPayloads.hpp"
#include "ImGuizmoInclude.hpp"

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

#include <glm/gtc/type_ptr.hpp>
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

    // The gizmo's camera looks down Z from this far above the sprites, which sit near Z = 0
    constexpr float c_GizmoDistance = 10000.0f;
    constexpr float c_MinimumSnap = 0.001f;
    constexpr float c_MaximumSnap = 1000.0f;
    constexpr std::array<const char*, 3> c_GizmoFields{ "Position", "Rotation", "Scale" };
    constexpr std::array<std::string_view, 3> c_GizmoOperationNames{ "Translate", "Rotate", "Scale" };

    // A whole number of steps, so a snapped value is exactly k times the step
    float Snap(float value, float step)
    {
        return std::round(value / step) * step;
    }

    // The editor camera as ImGuizmo takes it, a view and a projection apart, with depth the usual way round
    glm::mat4 GetGizmoView(const EditorCamera& camera)
    {
        return glm::translate(glm::mat4(1.0f), glm::vec3(-camera.GetPosition(), -c_GizmoDistance));
    }

    glm::mat4 GetGizmoProjection(const EditorCamera& camera, glm::vec2 viewportSize)
    {
        const glm::vec2 l_HalfExtent = camera.GetHalfExtent(viewportSize);

        return glm::ortho(-l_HalfExtent.x, l_HalfExtent.x, -l_HalfExtent.y, l_HalfExtent.y, 1.0f, c_GizmoDistance * 2.0f);
    }

    // The angle of the X axis about Z. A negative X scale turns it round, but by the same amount before and after a rotation, so differences stay true
    float GetAngleZ(const glm::mat4& matrix)
    {
        return std::atan2(matrix[0][1], matrix[0][0]);
    }

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

// The session is still open here, so a camera moved in the last moments is saved. The Viewport is ImGuizmo's only user, so it lets go of ImGuizmo's memory too
ViewportPanel::~ViewportPanel()
{
    if (m_CameraDirty)
    {
        SaveCamera();
    }

    ReleaseImGuizmo();
}

// The Viewport's own lines in imgui.ini: the stats overlay, the gizmo's operation and axes, and the snap steps
bool ViewportPanel::ReadSetting(std::string_view key, std::string_view value)
{
    const auto a_ReadStep = [value](float& step)
    {
        float l_Value = 0.0f;
        const std::from_chars_result l_Parsed = std::from_chars(value.data(), value.data() + value.size(), l_Value);
        if (l_Parsed.ec == std::errc() && std::isfinite(l_Value))
        {
            step = std::clamp(l_Value, c_MinimumSnap, c_MaximumSnap);
        }
    };

    if (key == "ViewportStats")
    {
        m_ShowStats = value != "0";
    }
    else if (key == "GizmoOperation")
    {
        const auto a_Found = std::ranges::find(c_GizmoOperationNames, value);
        if (a_Found != c_GizmoOperationNames.end())
        {
            m_GizmoOperation = static_cast<GizmoOperation>(a_Found - c_GizmoOperationNames.begin());
        }
    }
    else if (key == "GizmoSpace")
    {
        m_GizmoLocal = value != "World";
    }
    else if (key == "SnapTranslate")
    {
        a_ReadStep(m_SnapSteps.x);
    }
    else if (key == "SnapRotate")
    {
        a_ReadStep(m_SnapSteps.y);
    }
    else if (key == "SnapScale")
    {
        a_ReadStep(m_SnapSteps.z);
    }
    else
    {
        return false;
    }

    return true;
}

void ViewportPanel::WriteSettings(ImGuiTextBuffer& buffer) const
{
    const std::string l_Lines = std::format("ViewportStats={}\nGizmoOperation={}\nGizmoSpace={}\nSnapTranslate={}\nSnapRotate={}\nSnapScale={}\n", m_ShowStats ? 1 : 0, c_GizmoOperationNames[static_cast<std::size_t>(m_GizmoOperation)], m_GizmoLocal ? "Local" : "World", m_SnapSteps.x, m_SnapSteps.y, m_SnapSteps.z);
    buffer.append(l_Lines.c_str(), l_Lines.c_str() + l_Lines.size());
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

    // The scene has input over the whole panel. Picking, panning and zooming are only for the image itself, not the toolbar over it
    const bool l_Hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    const bool l_Focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
    m_ImGui.SetSceneInput(l_Hovered, l_Focused);

    ImDrawList& l_DrawList = *ImGui::GetWindowDrawList();
    l_DrawList.PushClipRect(ImGui::GetWindowPos(), ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y), true);
    DrawOverlays(l_DrawList, glm::vec2(l_ImageMin.x, l_ImageMin.y), l_ViewportSize);
    DrawGizmo(glm::vec2(l_ImageMin.x, l_ImageMin.y), l_ViewportSize);
    l_DrawList.PopClipRect();

    HandleInput(glm::vec2(l_ImageMin.x, l_ImageMin.y), l_ViewportSize, ImGui::IsWindowHovered(), l_Focused);

    if (m_ShowStats)
    {
        DrawStats(l_ViewportSize);
    }

    DrawToolbar();

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

    // W, E and R pick the gizmo's operation and X its axes, as long as no drag is under way
    if (focused && !l_IO.WantTextInput && !m_GizmoDragging)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_F, false))
        {
            FrameSelection(viewportSize);
        }

        constexpr std::array<ImGuiKey, 3> c_OperationKeys{ ImGuiKey_W, ImGuiKey_E, ImGuiKey_R };
        for (std::size_t it_Operation = 0; it_Operation < c_OperationKeys.size(); ++it_Operation)
        {
            if (ImGui::IsKeyPressed(c_OperationKeys[it_Operation], false))
            {
                m_GizmoOperation = static_cast<GizmoOperation>(it_Operation);
                ImGui::MarkIniSettingsDirty();
            }
        }

        if (ImGui::IsKeyPressed(ImGuiKey_X, false))
        {
            m_GizmoLocal = !m_GizmoLocal;
            ImGui::MarkIniSettingsDirty();
        }
    }

    // A click on the gizmo is the gizmo's, not a pick
    if (hovered && !m_GizmoHovered && !m_GizmoDragging && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
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

// On the selected entity, editing its transform relative to its parent, so its children follow. ImGuizmo drags a matrix of its own, unsnapped, and the entity takes from it the part the operation changes. The drag is one command, and holds ImGui's active item while it lasts, so nothing else closes the command or takes the mouse
void ViewportPanel::DrawGizmo(glm::vec2 imageMin, glm::vec2 viewportSize)
{
    ImGuizmo::BeginFrame();
    m_GizmoHovered = false;
    m_GizmoID = ImGui::GetID("##Gizmo");

    Trinity::Scene& l_Scene = m_Session.GetScene();
    const Trinity::Entity l_Entity = l_Scene.FindEntityByUUID(m_Session.GetSelection());
    if (m_GizmoDragging && (!l_Entity || l_Entity.GetUUID() != m_GizmoEntity))
    {
        EndGizmoDrag();
    }

    if (!l_Entity || viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
    {
        return;
    }

    const Trinity::Entity l_Parent = l_Entity.GetParent();
    const glm::mat4 l_ParentWorld = l_Parent ? l_Scene.ComputeWorldMatrix(l_Parent) : glm::mat4(1.0f);
    if (!m_GizmoDragging)
    {
        m_GizmoMatrix = l_ParentWorld * l_Entity.Get<Trinity::TransformComponent>().GetMatrix();
    }

    // A 2D gizmo: moving in X and Y, turning about Z, and scaling X and Y
    const std::array<ImGuizmo::OPERATION, 3> l_Operations{ ImGuizmo::TRANSLATE_X | ImGuizmo::TRANSLATE_Y, ImGuizmo::ROTATE_Z, ImGuizmo::SCALE_X | ImGuizmo::SCALE_Y };
    const ImGuizmo::OPERATION l_Operation = l_Operations[static_cast<std::size_t>(m_GizmoOperation)];
    const glm::mat4 l_View = GetGizmoView(m_Camera);
    const glm::mat4 l_Projection = GetGizmoProjection(m_Camera, viewportSize);

    ImGuizmo::SetOrthographic(true);
    ImGuizmo::AllowAxisFlip(false);
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(imageMin.x, imageMin.y, viewportSize.x, viewportSize.y);
    const bool l_Changed = ImGuizmo::Manipulate(glm::value_ptr(l_View), glm::value_ptr(l_Projection), l_Operation, m_GizmoLocal ? ImGuizmo::LOCAL : ImGuizmo::WORLD, glm::value_ptr(m_GizmoMatrix));
    m_GizmoHovered = ImGuizmo::IsOver(l_Operation);

    if (ImGuizmo::IsUsing() && !m_GizmoDragging)
    {
        m_GizmoDragging = true;
        m_GizmoEntity = l_Entity.GetUUID();
        m_GizmoStart = l_Entity.Get<Trinity::TransformComponent>();
        m_Session.GetHistory().EndMerge();
    }

    if (!m_GizmoDragging)
    {
        return;
    }

    if (!ImGuizmo::IsUsing())
    {
        EndGizmoDrag();

        return;
    }

    if (ImGui::GetActiveID() != m_GizmoID)
    {
        ImGui::SetActiveID(m_GizmoID, ImGui::GetCurrentWindow());
    }

    ImGui::KeepAliveID(m_GizmoID);

    if (l_Changed)
    {
        Trinity::TransformComponent l_Value = ApplyGizmo(l_ParentWorld, ImGui::GetIO().KeyCtrl);
        m_Session.GetHistory().Execute(Trinity::CreateScope<SetComponentCommand<Trinity::TransformComponent>>(m_GizmoEntity, std::move(l_Value), c_GizmoFields[static_cast<std::size_t>(m_GizmoOperation)]));
    }
}

void ViewportPanel::EndGizmoDrag()
{
    m_GizmoDragging = false;
    m_GizmoEntity = {};
    m_Session.GetHistory().EndMerge();
    if (ImGui::GetActiveID() == m_GizmoID)
    {
        ImGui::ClearActiveID();
    }
}

// The change the drag has made so far, in the parent's space, applied to the transform the drag began with. Only the part the operation changes is taken, so the rest keeps its values exactly. Snapping puts the result on the grid of the step, not the change
Trinity::TransformComponent ViewportPanel::ApplyGizmo(const glm::mat4& parentWorld, bool snap) const
{
    const glm::mat4 l_Local = glm::inverse(parentWorld) * m_GizmoMatrix;
    const glm::mat4 l_Start = m_GizmoStart.GetMatrix();

    Trinity::TransformComponent l_Value = m_GizmoStart;
    switch (m_GizmoOperation)
    {
        case GizmoOperation::Translate:
        {
            l_Value.Position.x = snap ? Snap(l_Local[3].x, m_SnapSteps.x) : l_Local[3].x;
            l_Value.Position.y = snap ? Snap(l_Local[3].y, m_SnapSteps.x) : l_Local[3].y;
            break;
        }
        case GizmoOperation::Rotate:
        {
            l_Value.Rotation = glm::angleAxis(GetAngleZ(l_Local) - GetAngleZ(l_Start), glm::vec3(0.0f, 0.0f, 1.0f)) * m_GizmoStart.Rotation;
            if (snap)
            {
                glm::vec3 l_Euler = glm::eulerAngles(l_Value.Rotation);
                l_Euler.z = glm::radians(Snap(glm::degrees(l_Euler.z), m_SnapSteps.y));
                l_Value.Rotation = glm::quat(l_Euler);
            }

            break;
        }
        case GizmoOperation::Scale:
        {
            // As a ratio of each axis' length, which keeps a flip. Snapping never lands on zero, which would flatten the sprite for good
            for (glm::length_t it_Axis = 0; it_Axis < 2; ++it_Axis)
            {
                const float l_Before = glm::length(glm::vec3(l_Start[it_Axis]));
                if (l_Before > 1e-12f)
                {
                    l_Value.Scale[it_Axis] = m_GizmoStart.Scale[it_Axis] * glm::length(glm::vec3(l_Local[it_Axis])) / l_Before;
                }

                if (snap)
                {
                    const float l_Snapped = Snap(l_Value.Scale[it_Axis], m_SnapSteps.z);
                    l_Value.Scale[it_Axis] = l_Snapped != 0.0f ? l_Snapped : std::copysign(m_SnapSteps.z, m_GizmoStart.Scale[it_Axis]);
                }
            }

            break;
        }
    }

    return l_Value;
}

// Over the image's top-right corner: the operation, the axes, and the steps Ctrl snaps to. Placed by its width last frame, since it sizes itself to what it holds
void ViewportPanel::DrawToolbar()
{
    const ImVec2 l_Start = ImGui::GetCursorStartPos();
    ImGui::SetCursorPos(ImVec2(std::max(l_Start.x + c_StatsMargin, ImGui::GetWindowWidth() - m_ToolbarWidth - c_StatsMargin), l_Start.y + c_StatsMargin));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.55f));

    const ImGuiChildFlags l_ChildFlags = ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding;
    if (ImGui::BeginChild("##Toolbar", ImVec2(0.0f, 0.0f), l_ChildFlags, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings))
    {
        constexpr std::array<const char*, 3> c_Icons{ Trinity::Icons::c_Arrows, Trinity::Icons::c_RotateRight, Trinity::Icons::c_Expand };
        constexpr std::array<const char*, 3> c_Tips{ "Move (W)", "Rotate about Z (E)", "Scale (R)" };
        for (std::size_t it_Operation = 0; it_Operation < c_Icons.size(); ++it_Operation)
        {
            const bool l_Current = static_cast<std::size_t>(m_GizmoOperation) == it_Operation;
            if (l_Current)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            }

            if (ImGui::Button(std::format("{}##Operation{}", c_Icons[it_Operation], it_Operation).c_str()))
            {
                m_GizmoOperation = static_cast<GizmoOperation>(it_Operation);
                ImGui::MarkIniSettingsDirty();
            }

            if (l_Current)
            {
                ImGui::PopStyleColor();
            }

            ImGui::SetItemTooltip("%s", c_Tips[it_Operation]);
            ImGui::SameLine();
        }

        if (ImGui::Button(std::format("{} {}###Space", m_GizmoLocal ? Trinity::Icons::c_Cube : Trinity::Icons::c_Globe, m_GizmoLocal ? "Local" : "World").c_str()))
        {
            m_GizmoLocal = !m_GizmoLocal;
            ImGui::MarkIniSettingsDirty();
        }

        ImGui::SetItemTooltip("The gizmo's axes: the entity's own, or the world's (X)");

        const auto a_Step = [](const char* id, float& step, const char* format, const char* tip)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 3.5f);
            if (ImGui::DragFloat(id, &step, 0.01f, c_MinimumSnap, c_MaximumSnap, format, ImGuiSliderFlags_AlwaysClamp))
            {
                ImGui::MarkIniSettingsDirty();
            }

            ImGui::SetItemTooltip("%s", tip);
        };

        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(Trinity::Icons::c_Magnet);
        ImGui::SetItemTooltip("Hold Ctrl while dragging to snap to these steps");
        a_Step("##SnapTranslate", m_SnapSteps.x, "%g", "Move snap, in units");
        a_Step("##SnapRotate", m_SnapSteps.y, "%g\xC2\xB0", "Rotate snap, in degrees");
        a_Step("##SnapScale", m_SnapSteps.z, "%g", "Scale snap");

        m_ToolbarWidth = ImGui::GetWindowWidth();
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